// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016-2019 The Linux Foundation. All rights reserved.
 * Copyright (c) 2026, Marc Lainez <marc.lainez@gmail.com>
 *
 * This driver is for the SMB5 switch-mode battery charger found in the
 * Qualcomm PMI632 PMIC.  It is modeled on the SMB2 driver
 * (qcom_pmi8998_charger.c by Caleb Connolly) but adapted for the SMB5
 * register layout:
 *
 *   - Charge status enum is reordered (INHIBIT=0 .. DISABLE=7)
 *   - ICL and power-path status live in the DCDC block (+0x100)
 *   - Current steps are 50 mA (not 25 mA)
 *   - Float voltage steps are 10 mV from 3600 mV
 *   - BAT_OV is at BIT(1) in CHARGER_STATUS_2
 *   - Type-C is handled by a separate driver; we don't touch it
 */

#include <linux/bits.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/iio/consumer.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/minmax.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_wakeirq.h>
#include <linux/property.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>
#include <linux/workqueue.h>

/* ---------- CHGR block (base + 0x000) ----------------------------------- */
#define BATTERY_CHARGER_STATUS_1		0x06
#define BATTERY_CHARGER_STATUS_MASK		GENMASK(2, 0)

#define BATTERY_CHARGER_STATUS_2		0x07
#define CHARGER_ERROR_STATUS_BAT_OV_BIT		BIT(1)
#define BAT_TEMP_STATUS_TOO_HOT_BIT		BIT(3)
#define BAT_TEMP_STATUS_TOO_COLD_BIT		BIT(2)

#define BATTERY_CHARGER_STATUS_7		0x0D

#define CHARGING_ENABLE_CMD			0x42
#define CHARGING_ENABLE_CMD_BIT			BIT(0)

#define CHGR_CFG2				0x51
#define RECHG_MASK				GENMASK(2, 1)
#define VBAT_BASED_RECHG_BIT			BIT(2)
#define CHARGER_INHIBIT_BIT			BIT(0)

#define FAST_CHARGE_CURRENT_CFG			0x61
#define FAST_CHARGE_CURRENT_SETTING_MASK	GENMASK(7, 0)

#define ADC_ITERM_UP_THRESHOLD			0x67
#define CHGR_ENG_CHARGING_CFG			0xc0
#define ITERM_USE_ANALOG_BIT			BIT(3)
#define FAST_CHARGE_SAFETY_TIMER_CFG		0xa2

#define FLOAT_VOLTAGE_CFG			0x70
#define FLOAT_VOLTAGE_SETTING_MASK		GENMASK(7, 0)

#define JEITA_HARD_HOT_THRESHOLD		0x98
#define JEITA_HARD_COLD_THRESHOLD	0x9a

#define JEITA_EN_CFG				0x90
#define JEITA_EN_HOT_SL_FCV_BIT		BIT(3)
#define JEITA_EN_COLD_SL_FCV_BIT		BIT(2)
#define JEITA_EN_HOT_SL_CCC_BIT		BIT(1)
#define JEITA_EN_COLD_SL_CCC_BIT		BIT(0)

/* ---------- DCDC block (base + 0x100) ----------------------------------- */
#define DCDC_ICL_STATUS				0x107
#define DCDC_POWER_PATH_STATUS			0x10B
#define P_PATH_USBIN_SUSPEND_STS_BIT		BIT(6)
#define P_PATH_USE_USBIN_BIT			BIT(4)
#define P_PATH_POWER_PATH_MASK			GENMASK(2, 1)
#define P_PATH_VALID_INPUT_POWER_SOURCE_STS_BIT	BIT(0)

/* ---------- USBIN block (base + 0x300) ---------------------------------- */
#define APSD_STATUS				0x307
#define APSD_DTC_STATUS_DONE_BIT		BIT(0)

#define APSD_RESULT_STATUS			0x308
#define APSD_RESULT_STATUS_MASK			GENMASK(6, 0)
#define QC_3P0_BIT				BIT(6)
#define QC_2P0_BIT				BIT(5)
#define FLOAT_CHARGER_BIT			BIT(4)
#define DCP_CHARGER_BIT				BIT(3)
#define CDP_CHARGER_BIT				BIT(2)
#define OCP_CHARGER_BIT				BIT(1)
#define SDP_CHARGER_BIT				BIT(0)

#define USBIN_CMD_IL				0x340
#define USBIN_SUSPEND_BIT			BIT(0)

#define CMD_ICL_OVERRIDE			0x342
#define ICL_OVERRIDE_BIT			BIT(0)
#define USBIN_LOAD_CFG				0x365
#define ICL_OVERRIDE_AFTER_APSD_BIT		BIT(4)

#define CMD_APSD				0x341
#define APSD_RERUN_BIT				BIT(0)

#define USBIN_OPTIONS_1_CFG			0x362
#define HVDCP_AUTH_ALG_EN_BIT		BIT(6)
#define HVDCP_AUTONOMOUS_EN_BIT		BIT(5)
#define HVDCP_EN_BIT				BIT(2)
#define BC1P2_SRC_DETECT_BIT			BIT(3)

#define USBIN_OPTIONS_2_CFG			0x363
#define DCD_TIMEOUT_SEL_BIT			BIT(5)
#define FLOAT_OPTIONS_MASK			GENMASK(2, 0)
#define FLOAT_DIS_CHGING_CFG_BIT		BIT(2)
#define FORCE_FLOAT_SDP_CFG_BIT		BIT(0)

#define USBIN_ICL_OPTIONS			0x366
#define CFG_USB3P0_SEL_BIT			BIT(2)
#define USB51_MODE_BIT				BIT(1)
#define USBIN_MODE_CHG_BIT			BIT(0)

#define USBIN_CURRENT_LIMIT_CFG			0x370
#define USBIN_CURRENT_LIMIT_MASK		GENMASK(7, 0)

#define USBIN_AICL_OPTIONS_CFG			0x380
#define SUSPEND_ON_COLLAPSE_USBIN_BIT		BIT(7)
#define USBIN_AICL_START_AT_MAX_BIT		BIT(5)
#define USBIN_AICL_ADC_EN_BIT			BIT(3)
#define USBIN_AICL_EN_BIT			BIT(2)
#define USBIN_HV_COLLAPSE_RESPONSE_BIT		BIT(1)
#define USBIN_LV_COLLAPSE_RESPONSE_BIT		BIT(0)



/* ---------- MISC block (base + 0x600) ----------------------------------- */
#define BARK_BITE_WDOG_PET			0x643
#define BARK_BITE_WDOG_PET_BIT			BIT(0)

#define WD_CFG					0x651
#define WATCHDOG_TRIGGER_AFP_EN_BIT		BIT(7)
#define BARK_WDOG_INT_EN_BIT			BIT(6)
#define WDOG_TIMER_EN_ON_PLUGIN_BIT		BIT(1)

#define SNARL_BARK_BITE_WD_CFG			0x653
#define BITE_WDOG_DISABLE_CHARGING_CFG_BIT	BIT(7)
#define BARK_WDOG_TIMEOUT_MASK			GENMASK(3, 2)
#define BITE_WDOG_TIMEOUT_MASK			GENMASK(1, 0)

/* ---------- Scale factors ----------------------------------------------- */
#define SDP_CURRENT_UA				500000
#define CDP_CURRENT_UA				1500000
#define DCP_CURRENT_UA				1500000

/* PMI632/SMB5 uses 50 mA steps (SMB2 uses 25 mA) */
#define CURRENT_SCALE_FACTOR			50000

/* PMI632/SMB5: float voltage = 3600 mV + (reg_val * 10 mV) */
#define FLOAT_VOLTAGE_BASE_UV			3600000
#define FLOAT_VOLTAGE_STEP_UV			10000

/*
 * SMB5 charge status values — note the different ordering from SMB2.
 * SMB2: TRICKLE=0,PRE=1,FAST=2,FULLON=3,TAPER=4,TERMINATE=5,INHIBIT=6,DISABLE=7
 * SMB5: INHIBIT=0,TRICKLE=1,PRE=2,FULLON=3,TAPER=4,TERMINATE=5,PAUSE=6,DISABLE=7
 */
enum smb5_charger_status {
	INHIBIT_CHARGE = 0,
	TRICKLE_CHARGE,
	PRE_CHARGE,
	FULLON_CHARGE,
	TAPER_CHARGE,
	TERMINATE_CHARGE,
	PAUSE_CHARGE,
	DISABLE_CHARGE,
};

struct smb5_register {
	u16 addr;
	u8 mask;
	u8 val;
};

/**
 * struct smb5_chip - PMI632 SMB5 charger chip data
 * @dev:		Device reference
 * @name:		Platform device name
 * @base:		Base address for charger registers
 * @regmap:		Parent SPMI regmap
 * @batt_info:		Battery data from DT
 * @status_change_work:	Worker to handle plug/unplug events
 * @cable_irq:		USB plugin IRQ
 * @usb_in_i_chan:	USB-in current IIO channel
 * @usb_in_v_chan:	USB-in voltage IIO channel
 * @chg_psy:		Charger power supply
 */
struct smb5_chip {
	struct device *dev;
	const char *name;
	unsigned int base;
	struct regmap *regmap;
	struct power_supply_battery_info *batt_info;

	struct delayed_work status_change_work;
	struct mutex lock;
	bool stopping;
	bool ready;
	bool recharge_pending;
	bool full_latched;
	struct regulator *dpdm;
	bool dpdm_enabled, qc_started, qc_failed, qc2_requested, qc_baseline;
	unsigned int qc_steps, qc_waits;
	unsigned int negotiated_uv, parallel_icl_ua;
	unsigned int parallel_total_icl, parallel_fcc, parallel_fv;
	unsigned int apsd_retries;
	unsigned int input_limit_ua;
	unsigned int user_limit_ua;
	int charge_current_ua;
	int charge_voltage_uv;
	struct iio_channel *batt_therm;
	struct iio_channel *chg_therm;
	int cable_irq;

	struct iio_channel *usb_in_i_chan;
	struct iio_channel *usb_in_v_chan;

	struct power_supply *chg_psy;
};

static enum power_supply_property smb5_properties[] = {
	POWER_SUPPLY_PROP_MANUFACTURER,
	POWER_SUPPLY_PROP_MODEL_NAME,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_USB_TYPE,
};

/* Processed IIO reads return a nonnegative value-format code on success
 * (ADC5 returns IIO_VAL_INT == 1), unlike regmap/power_supply's zero.
 * Normalize at the consumer boundary so positive success never reaches
 * the charging-disable error path or a power_supply property callback.
 */
static int smb5_read_processed(struct iio_channel *chan, int *val)
{
	int ret = iio_read_channel_processed(chan, val);

	return ret < 0 ? ret : 0;
}

static int smb5_get_prop_usb_online(struct smb5_chip *chip, int *val)
{
	unsigned int stat;
	int rc;

	/* On SMB5 the power-path status is in the DCDC block (+0x100) */
	rc = regmap_read(chip->regmap,
			 chip->base + DCDC_POWER_PATH_STATUS, &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read power path status: %d\n", rc);
		return rc;
	}

	*val = (stat & P_PATH_USE_USBIN_BIT) &&
	       (stat & P_PATH_VALID_INPUT_POWER_SOURCE_STS_BIT);
	return 0;
}

static int smb5_apsd_get_charger_type(struct smb5_chip *chip, int *val)
{
	unsigned int apsd_stat, stat;
	int usb_online = 0;
	int rc;

	rc = smb5_get_prop_usb_online(chip, &usb_online);
	if (!usb_online) {
		*val = POWER_SUPPLY_USB_TYPE_UNKNOWN;
		return rc;
	}

	rc = regmap_read(chip->regmap, chip->base + APSD_STATUS, &apsd_stat);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read APSD status: %d\n", rc);
		return rc;
	}
	if (!(apsd_stat & APSD_DTC_STATUS_DONE_BIT)) {
		dev_dbg(chip->dev, "APSD not ready\n");
		return -EAGAIN;
	}

	rc = regmap_read(chip->regmap,
			 chip->base + APSD_RESULT_STATUS, &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read APSD result: %d\n", rc);
		return rc;
	}

	stat &= APSD_RESULT_STATUS_MASK;

	if (stat & CDP_CHARGER_BIT)
		*val = POWER_SUPPLY_USB_TYPE_CDP;
	else if (stat & (DCP_CHARGER_BIT | OCP_CHARGER_BIT | QC_2P0_BIT | QC_3P0_BIT))
		*val = POWER_SUPPLY_USB_TYPE_DCP;
	else if (stat & SDP_CHARGER_BIT)
		*val = POWER_SUPPLY_USB_TYPE_SDP;
	else
		*val = POWER_SUPPLY_USB_TYPE_UNKNOWN;

	return 0;
}

static int smb5_get_prop_status(struct smb5_chip *chip, int *val)
{
	unsigned char stat[2];
	unsigned int enabled;
	int usb_online = 0;
	int rc;

	rc = smb5_get_prop_usb_online(chip, &usb_online);
	if (!usb_online) {
		*val = POWER_SUPPLY_STATUS_DISCHARGING;
		return rc;
	}

	rc = regmap_bulk_read(chip->regmap,
			      chip->base + BATTERY_CHARGER_STATUS_1, stat, 2);
	if (rc < 0) {
		dev_err(chip->dev, "Failed to read charging status: %d\n", rc);
		return rc;
	}

	if (stat[1] & CHARGER_ERROR_STATUS_BAT_OV_BIT) {
		*val = POWER_SUPPLY_STATUS_NOT_CHARGING;
		return 0;
	}

	rc = regmap_read(chip->regmap, chip->base + CHARGING_ENABLE_CMD, &enabled);
	if (rc)
		return rc;
	if (!(enabled & CHARGING_ENABLE_CMD_BIT)) {
		*val = POWER_SUPPLY_STATUS_NOT_CHARGING;
		return 0;
	}

	switch (stat[0] & BATTERY_CHARGER_STATUS_MASK) {
	case TRICKLE_CHARGE:
	case PRE_CHARGE:
	case FULLON_CHARGE:
	case TAPER_CHARGE:
		*val = POWER_SUPPLY_STATUS_CHARGING;
		return 0;
	case DISABLE_CHARGE:
	case PAUSE_CHARGE:
		*val = POWER_SUPPLY_STATUS_NOT_CHARGING;
		return 0;
	case INHIBIT_CHARGE:
		*val = POWER_SUPPLY_STATUS_NOT_CHARGING;
		return 0;
	case TERMINATE_CHARGE:
		*val = POWER_SUPPLY_STATUS_FULL;
		return 0;
	default:
		*val = POWER_SUPPLY_STATUS_UNKNOWN;
		return 0;
	}
}

static int smb5_get_current_limit(struct smb5_chip *chip, int *val)
{
	unsigned int raw;
	int rc;

	rc = regmap_read(chip->regmap, chip->base + DCDC_ICL_STATUS, &raw);
	if (rc >= 0)
		*val = raw * CURRENT_SCALE_FACTOR;
	return rc;
}

static int smb5_set_current_limit(struct smb5_chip *chip, unsigned int val)
{
	unsigned char val_raw;
	int rc;

	if (val > chip->input_limit_ua) {
		dev_err(chip->dev,
			"Input current exceeds board limit\n");
		return -EINVAL;
	}
	val_raw = val / CURRENT_SCALE_FACTOR;

	/* Stock SW_OVERRIDE_HC_MODE: program the limit then select HC mode. */
	rc = regmap_write(chip->regmap, chip->base + USBIN_CURRENT_LIMIT_CFG, val_raw);
	if (rc)
		return rc;
	rc = regmap_update_bits(chip->regmap, chip->base + USBIN_ICL_OPTIONS,
				USBIN_MODE_CHG_BIT, USBIN_MODE_CHG_BIT);
	if (rc)
		return rc;
	rc = regmap_update_bits(chip->regmap, chip->base + CMD_ICL_OVERRIDE,
				ICL_OVERRIDE_BIT, 0);
	if (rc)
		return rc;
	return regmap_update_bits(chip->regmap, chip->base + USBIN_LOAD_CFG,
				  ICL_OVERRIDE_AFTER_APSD_BIT,
				  ICL_OVERRIDE_AFTER_APSD_BIT);
}

/* Stock Laurel hard JEITA: 0--60 C; software FV drops at 44.1 C.
 * A missing/failed temperature reading must never enable charging.
 */
/* Explicit fallback follows the active JEITA float voltage, rather than
 * inheriting a bootloader threshold of unknown provenance. Stock programs
 * the VBAT ADC threshold big-endian and uses three qualifying samples.
 */
static int smb5_set_recharge(struct smb5_chip *chip, int fv, bool soc_ready)
{
	__be16 raw;
	int ret;

	/* Keep full precision of stock VBAT_TO_VRAW_ADC (input is mV). */
	raw = cpu_to_be16(div_u64((u64)(fv - 100000) * 1000, 194637));
	ret = regmap_bulk_write(chip->regmap, chip->base + 0x7e, &raw, sizeof(raw));
	if (ret)
		return ret;
	ret = regmap_write(chip->regmap, chip->base + 0x7d,
			DIV_ROUND_CLOSEST(99 * 255, 100));
	if (ret)
		return ret;
	ret = regmap_update_bits(chip->regmap, chip->base + 0x6b,
			GENMASK(3, 2), soc_ready ? 0 : 2 << 2);
	if (ret)
		return ret;
	return regmap_update_bits(chip->regmap, chip->base + CHGR_CFG2,
		RECHG_MASK, soc_ready ? RECHG_MASK : VBAT_BASED_RECHG_BIT);
}

/* Bounded QC2/QC3 policy, adapted from stock smblib DP/DM commands and
 * frequency mapping. Only authenticated DCP sources may request >5 V.
 * No high-voltage requests on SDP/CDP, PD or an unclassified source.
 */
static void smb5_qc_reset(struct smb5_chip *chip)
{
	if (chip->qc_started) {
		regmap_write(chip->regmap, chip->base + 0x343, BIT(3)); /* FORCE_5V */
		regmap_update_bits(chip->regmap, chip->base + USBIN_OPTIONS_1_CFG,
			HVDCP_EN_BIT | HVDCP_AUTH_ALG_EN_BIT | HVDCP_AUTONOMOUS_EN_BIT, 0);
	}
	if (chip->dpdm_enabled && !regulator_disable(chip->dpdm))
		chip->dpdm_enabled = false;
	chip->qc_started = chip->qc2_requested = chip->qc_baseline = false;
	chip->qc_steps = chip->qc_waits = 0;
	/* Only a subsequent ADC read may confirm that FORCE_5V succeeded. */
}

static int smb5_qc_update(struct smb5_chip *chip, int type)
{
	unsigned int apsd, result;
	int temp, die_temp, uv, ret;

	if (type != POWER_SUPPLY_USB_TYPE_DCP) {
		if (chip->qc_started)
			smb5_qc_reset(chip);
		return 0;
	}
	if (!chip->dpdm || chip->qc_failed)
		return 0;
	ret = smb5_read_processed(chip->batt_therm, &temp);
	if (!ret)
		ret = smb5_read_processed(chip->chg_therm, &die_temp);
	if (ret || temp <= 10000 || temp >= 44100 || die_temp >= 70000) {
		smb5_qc_reset(chip);
		return 0;
	}
	if (!chip->qc_started) {
		if (!chip->dpdm_enabled) {
			ret = regulator_enable(chip->dpdm);
			if (ret)
				goto fallback;
			chip->dpdm_enabled = true;
		}
		/* Stock QC2 maximum is 9 V; manual QC3 is capped at 20 pulses. */
		ret = regmap_update_bits(chip->regmap, chip->base + 0x35b, GENMASK(7, 6), BIT(6));
		if (!ret)
			ret = regmap_update_bits(chip->regmap, chip->base + USBIN_OPTIONS_1_CFG,
				HVDCP_EN_BIT | HVDCP_AUTH_ALG_EN_BIT | HVDCP_AUTONOMOUS_EN_BIT,
				HVDCP_EN_BIT | HVDCP_AUTH_ALG_EN_BIT);
		/* Set started before cleanup, even if enable only partly succeeded. */
		chip->qc_started = true;
		if (!ret)
			ret = regmap_write(chip->regmap, chip->base + 0x343, BIT(3));
		if (!ret)
			ret = regmap_write(chip->regmap, chip->base + CMD_APSD, APSD_RERUN_BIT);
		if (ret)
			goto fallback;
	}
	ret = regmap_read(chip->regmap, chip->base + APSD_STATUS, &apsd);
	if (!ret)
		ret = regmap_read(chip->regmap, chip->base + APSD_RESULT_STATUS, &result);
	if (ret)
		goto fallback;
	/* QC_AUTH_DONE is bit 2, distinct from detection completion. */
	if (!(apsd & BIT(2)) || !(result & (QC_2P0_BIT | QC_3P0_BIT))) {
		if (++chip->qc_waits < 30)
			return 0;
		goto fallback;
	}
	ret = smb5_read_processed(chip->usb_in_v_chan, &uv);
	if (ret || uv < 4400000 || uv > 9500000)
		goto fallback;
	/* Start manual pulses only from a measured 5 V baseline, including
	 * adapters left at a higher voltage by a previous boot.
	 */
	if (!chip->qc_baseline) {
		if (uv > 5500000) {
			if (++chip->qc_waits >= 60)
				goto fallback;
			return 0;
		}
		chip->qc_baseline = true;
	}
	/* Correct FSW before raising adapter voltage: stock 600/800/1050 kHz. */
	ret = regmap_write(chip->regmap, chip->base + 0x150,
			  (result & QC_2P0_BIT) && !(result & QC_3P0_BIT) ? 8 :
			  uv + 200000 >= 8500000 ? 8 : uv + 200000 >= 5500000 ? 11 : 15);
	if (ret)
		goto fallback;
	chip->negotiated_uv = clamp(uv, 5000000, 9000000);
	if (uv >= 8500000)
		return 0;
	if (++chip->qc_waits >= 60)
		goto fallback;
	if (result & QC_3P0_BIT) {
		if (chip->qc_steps >= 20)
			goto fallback;
		/* Stock QC3 5 V + 20 * 200 mV = 9 V maximum. */
		ret = regmap_write(chip->regmap, chip->base + 0x343, BIT(0));
		if (!ret)
			chip->qc_steps++;
	} else if (!chip->qc2_requested) {
		ret = regmap_write(chip->regmap, chip->base + 0x343, BIT(4)); /* FORCE_9V */
		chip->qc2_requested = !ret;
	}
	if (!ret)
		return 0;
fallback:
	smb5_qc_reset(chip);
	chip->qc_failed = true;
	dev_warn_ratelimited(chip->dev, "QC negotiation unavailable; requesting 5 V fallback\n");
	return 0;
}

/* Program a disabled secondary before handing it a current budget. Reduce
 * primary limits first, so aggregate USB ICL and battery FCC never exceed
 * the board limits during handoff. A failed secondary returns its budget.
 */
static int smb5_parallel_update(struct smb5_chip *chip, unsigned int total_icl)
{
	struct power_supply *parallel;
	union power_supply_propval val, health;
	unsigned int state, enabled;
	int ret = 0, fcc, icl;
	bool allow = false;

	parallel = power_supply_get_by_reference(dev_fwnode(chip->dev), "qcom,parallel-charger");
	if (IS_ERR_OR_NULL(parallel)) {
		/* Do not restore the primary budget while an unresponsive
		 * secondary could still be charging.
		 */
		ret = regmap_update_bits(chip->regmap, chip->base + 0x648,
					 BIT(3) | BIT(4) | BIT(2), BIT(3));
		if (!ret) {
			chip->parallel_icl_ua = 0;
			ret = smb5_set_current_limit(chip, total_icl);
		}
		if (!ret)
			ret = regmap_write(chip->regmap, chip->base + FAST_CHARGE_CURRENT_CFG,
					   chip->charge_current_ua / CURRENT_SCALE_FACTOR);
		return ret;
	}
	if (!regmap_read(chip->regmap, chip->base + BATTERY_CHARGER_STATUS_1, &state) &&
	    !regmap_read(chip->regmap, chip->base + CHARGING_ENABLE_CMD, &enabled) &&
	    !power_supply_get_property(parallel, POWER_SUPPLY_PROP_HEALTH, &health))
		allow = health.intval == POWER_SUPPLY_HEALTH_GOOD &&
			(enabled & CHARGING_ENABLE_CMD_BIT) &&
			(state & BATTERY_CHARGER_STATUS_MASK) == FULLON_CHARGE &&
			total_icl >= 1500000 && chip->charge_current_ua >= 2000000;
/* Leave an unchanged, healthy split running; do not reset its
	 * sensing mode and charge state at every watchdog poll.
	 */
	if (allow && chip->parallel_icl_ua &&
	    chip->parallel_total_icl == total_icl &&
	    chip->parallel_fcc == chip->charge_current_ua &&
	    chip->parallel_fv == chip->charge_voltage_uv &&
	    !power_supply_get_property(parallel, POWER_SUPPLY_PROP_ONLINE, &val) && val.intval)
		goto out;
	/* Stock SMB_EN override gives the primary an independent shutoff
	 * even when the secondary's I2C bus stops responding.
	 */
	ret = regmap_update_bits(chip->regmap, chip->base + 0x648,
				 BIT(3) | BIT(4) | BIT(2), BIT(3));
	val.intval = 0;
	if (!ret)
		ret = power_supply_set_property(parallel, POWER_SUPPLY_PROP_ONLINE, &val);
	if (ret)
		goto out;
	chip->parallel_icl_ua = 0;
	if (!allow)
		goto out;
	fcc = chip->charge_current_ua / 2;
	icl = total_icl / 2;
	ret = regmap_write(chip->regmap, chip->base + FAST_CHARGE_CURRENT_CFG,
			  (chip->charge_current_ua - fcc) / CURRENT_SCALE_FACTOR);
	if (!ret)
		ret = smb5_set_current_limit(chip, total_icl - icl);
	val.intval = chip->charge_voltage_uv;
	if (!ret)
		ret = power_supply_set_property(parallel, POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX, &val);
	val.intval = fcc;
	if (!ret)
		ret = power_supply_set_property(parallel, POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX, &val);
	val.intval = icl;
	if (!ret)
		ret = power_supply_set_property(parallel, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &val);
	if (!ret)
		ret = regmap_update_bits(chip->regmap, chip->base + 0x690, BIT(4), 0);
	if (!ret)
		ret = regmap_update_bits(chip->regmap, chip->base + 0x648,
					BIT(3) | BIT(4) | BIT(2), BIT(2));
	val.intval = 1;
	if (!ret)
		ret = power_supply_set_property(parallel, POWER_SUPPLY_PROP_ONLINE, &val);
	if (!ret) {
		ret = power_supply_get_property(parallel, POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT, &val);
		if (!ret) {
			chip->parallel_icl_ua = val.intval;
			chip->parallel_total_icl = total_icl;
			chip->parallel_fcc = chip->charge_current_ua;
			chip->parallel_fv = chip->charge_voltage_uv;
		}
	}
	if (ret) {
		regmap_update_bits(chip->regmap, chip->base + 0x648,
				   BIT(3) | BIT(4) | BIT(2), BIT(3));
		val.intval = 0;
		power_supply_set_property(parallel, POWER_SUPPLY_PROP_ONLINE, &val);
	}
out:
	power_supply_put(parallel);
	/* Restore the primary when the secondary is disabled or unavailable. */
	if (!chip->parallel_icl_ua && !ret) {
		ret = smb5_set_current_limit(chip, total_icl);
		if (!ret)
			ret = regmap_write(chip->regmap, chip->base + FAST_CHARGE_CURRENT_CFG,
					   chip->charge_current_ua / CURRENT_SCALE_FACTOR);
	}
	if (ret)
		regmap_update_bits(chip->regmap, chip->base + CHARGING_ENABLE_CMD,
				   CHARGING_ENABLE_CMD_BIT, 0);
	return ret;
}

static int smb5_apply_charge_limits(struct smb5_chip *chip)
{
	int temp, die_temp, rc;
	int fcc = chip->batt_info->constant_charge_current_max_ua;
	int fv = chip->batt_info->constant_charge_voltage_max_uv;
	bool enable;

	rc = smb5_read_processed(chip->batt_therm, &temp);
	if (rc < 0)
		goto disable;
	rc = smb5_read_processed(chip->chg_therm, &die_temp);
	if (rc < 0)
		goto disable;

	/* Additional conservative cool/warm derating until stock policy parity. */
	enable = temp > 0 && temp < 58000 && die_temp < 80000;
	if (temp <= 10000 || temp >= 44100 || die_temp >= 70000)
		fcc = min(fcc, 1000000);
	if (temp >= 44100)
		fv = min(fv, 4100000);

	if (!enable || fcc != chip->charge_current_ua || fv != chip->charge_voltage_uv) {
		rc = smb5_parallel_update(chip, 0);
		if (rc)
			goto disable;
	}
	if (fcc == chip->charge_current_ua && fv == chip->charge_voltage_uv)
		return regmap_update_bits(chip->regmap,
				chip->base + CHARGING_ENABLE_CMD,
				CHARGING_ENABLE_CMD_BIT,
				enable ? CHARGING_ENABLE_CMD_BIT : 0);

	/* Change limits with charging stopped, rather than raising FV/FCC live. */
	rc = regmap_update_bits(chip->regmap,
				chip->base + CHARGING_ENABLE_CMD,
				CHARGING_ENABLE_CMD_BIT, 0);
	if (rc)
		return rc;
	rc = regmap_write(chip->regmap, chip->base + FLOAT_VOLTAGE_CFG,
			  (fv - FLOAT_VOLTAGE_BASE_UV) / FLOAT_VOLTAGE_STEP_UV);
	if (rc)
		return rc;
	rc = regmap_write(chip->regmap, chip->base + FAST_CHARGE_CURRENT_CFG,
			  fcc / CURRENT_SCALE_FACTOR);
	if (rc)
		return rc;
	chip->charge_current_ua = fcc;
	chip->charge_voltage_uv = fv;
	return regmap_update_bits(chip->regmap,
				  chip->base + CHARGING_ENABLE_CMD,
				  CHARGING_ENABLE_CMD_BIT,
				  enable ? CHARGING_ENABLE_CMD_BIT : 0);
disable:
	regmap_update_bits(chip->regmap, chip->base + CHARGING_ENABLE_CMD,
			   CHARGING_ENABLE_CMD_BIT, 0);
	return rc;
}

static void smb5_status_change_work(struct work_struct *work)
{
	struct smb5_chip *chip = container_of(work, struct smb5_chip,
					    status_change_work.work);
	int charger_type = POWER_SUPPLY_USB_TYPE_UNKNOWN;
	unsigned int current_ua = SDP_CURRENT_UA;
	int usb_online = 0, rc, input_uv;
	struct power_supply *qg;
	union power_supply_propval calibrated, soc, battery_status;
	unsigned int state, enabled;
	bool soc_ready = false;

	pm_stay_awake(chip->dev);
	mutex_lock(&chip->lock);
	if (chip->stopping || !chip->ready)
		goto out;

	rc = smb5_get_prop_usb_online(chip, &usb_online);
	if (rc || !usb_online) {
		smb5_parallel_update(chip, 0);
		smb5_qc_reset(chip);
		chip->qc_failed = false;
		regmap_update_bits(chip->regmap, chip->base + CHARGING_ENABLE_CMD,
				   CHARGING_ENABLE_CMD_BIT, 0);
		chip->apsd_retries = 0;
		chip->recharge_pending = false;
		chip->full_latched = false;
		pm_relax(chip->dev);
		goto notify;
	}

	rc = smb5_apsd_get_charger_type(chip, &charger_type);
	/* APSD is intentionally rerun for authentication. A temporarily
	 * incomplete result must not tear down the already classified DCP.
	 */
	if (rc == -EAGAIN && chip->qc_started)
		charger_type = POWER_SUPPLY_USB_TYPE_DCP;
	if (rc == -EAGAIN && !chip->qc_started && chip->apsd_retries++ < 3)
		regmap_update_bits(chip->regmap, chip->base + CMD_APSD,
				   APSD_RERUN_BIT, APSD_RERUN_BIT);
	/* Never raise ICL for an unclassified/float source or a failed read. */
	if (!rc) {
		if (charger_type == POWER_SUPPLY_USB_TYPE_CDP)
			current_ua = CDP_CURRENT_UA;
		else if (charger_type == POWER_SUPPLY_USB_TYPE_DCP)
			current_ua = chip->input_limit_ua;
	}
	/* Disable the secondary before negotiating or reducing its budget. */
	if (chip->parallel_icl_ua &&
	    (charger_type != POWER_SUPPLY_USB_TYPE_DCP ||
	     (chip->dpdm && !chip->qc_failed && !chip->qc_started))) {
		rc = smb5_parallel_update(chip, 0);
		if (rc)
			goto charging_error;
	}
	smb5_qc_update(chip, charger_type);
	/* Negotiation commands are not voltage measurements. Bound input
	 * power using the physical ADC even on the failure/fallback path.
	 */
	rc = smb5_read_processed(chip->usb_in_v_chan, &input_uv);
	if (rc || input_uv < 4400000 || input_uv > 9500000) {
		rc = rc ?: -ERANGE;
		goto charging_error;
	}
	chip->negotiated_uv = input_uv;
	if ((chip->qc_started && input_uv < 8500000) ||
	    (!chip->qc_started && input_uv > 5500000))
		current_ua = min(current_ua, SDP_CURRENT_UA);
	current_ua = min(current_ua, chip->user_limit_ua);
	current_ua = min(current_ua, (unsigned int)div_u64(18000000ULL * 1000000, input_uv));
	/* Match the primary's 50 mA register resolution before comparing
	 * budgets, rather than reconfiguring for every ADC microvolt.
	 */
	current_ua = current_ua / CURRENT_SCALE_FACTOR * CURRENT_SCALE_FACTOR;
	if (chip->parallel_icl_ua && chip->parallel_total_icl != current_ua) {
		rc = smb5_parallel_update(chip, 0);
		if (rc)
			goto charging_error;
	}
	/* A running secondary already owns part of this aggregate budget. */
	if (!chip->parallel_icl_ua)
		rc = smb5_set_current_limit(chip, current_ua);
	else
		rc = 0;
	if (!rc)
		rc = smb5_apply_charge_limits(chip);
	qg = power_supply_get_by_name("qg-battery");
	if (qg) {
		soc_ready = !power_supply_get_property(qg, POWER_SUPPLY_PROP_CALIBRATE, &calibrated) &&
			calibrated.intval && !power_supply_get_property(qg, POWER_SUPPLY_PROP_CAPACITY, &soc);
		if (soc_ready && !power_supply_get_property(qg, POWER_SUPPLY_PROP_STATUS,
						       &battery_status) &&
		    battery_status.intval == POWER_SUPPLY_STATUS_FULL)
			chip->full_latched = true;
		power_supply_put(qg);
	}
	/* A calibrated gauge can still underestimate SOC during initial charge.
	 * Selecting the 99% hardware comparator at 98-99% then retriggers
	 * charging at every termination, before QG can qualify full. Keep the
	 * stock VBAT fallback until full has actually been established.
	 */
	if (!rc)
		rc = smb5_set_recharge(chip, chip->charge_voltage_uv,
				       soc_ready && chip->full_latched);
	/* PMI632 QG_RECHARGE_SOC_WA: one restart per termination episode,
	 * only after qualified full, with a live SOC producer and thermal control.
	 * Initial voltage/SOC disagreement must not force repeated restarts.
	 * Never repeatedly pulse a disabled/paused or hot charger.
	 */
	if (!rc && !regmap_read(chip->regmap, chip->base + BATTERY_CHARGER_STATUS_1, &state) &&
	    !regmap_read(chip->regmap, chip->base + CHARGING_ENABLE_CMD, &enabled)) {
		state &= BATTERY_CHARGER_STATUS_MASK;
		if (state >= TRICKLE_CHARGE && state <= TAPER_CHARGE)
			chip->recharge_pending = false;
		if (soc_ready && chip->full_latched && soc.intval <= 99 &&
		    state == TERMINATE_CHARGE &&
		    (enabled & CHARGING_ENABLE_CMD_BIT) && !chip->recharge_pending) {
			rc = regmap_update_bits(chip->regmap, chip->base + CHARGING_ENABLE_CMD,
				CHARGING_ENABLE_CMD_BIT, 0);
			if (!rc)
				rc = regmap_update_bits(chip->regmap, chip->base + CHARGING_ENABLE_CMD,
					CHARGING_ENABLE_CMD_BIT, CHARGING_ENABLE_CMD_BIT);
			chip->recharge_pending = true;
			chip->full_latched = false;
		}
	}
	if (!rc)
		rc = smb5_parallel_update(chip, current_ua);
charging_error:
	if (rc) {
		smb5_parallel_update(chip, 0);
		/* Keep the system input conservative too, not only the battery
		 * switcher, when the source/ADC/secondary state is uncertain.
		 */
		smb5_set_current_limit(chip, SDP_CURRENT_UA);
		regmap_update_bits(chip->regmap, chip->base + CHARGING_ENABLE_CMD,
				   CHARGING_ENABLE_CMD_BIT, 0);
		dev_err_ratelimited(chip->dev, "Charging update failed: %d\n", rc);
	}
	/* Keep the watchdog charging-disable failsafe active. */
	regmap_write(chip->regmap, chip->base + BARK_BITE_WDOG_PET,
		     BARK_BITE_WDOG_PET_BIT);
	if (!chip->stopping)
		mod_delayed_work(system_wq, &chip->status_change_work,
				 msecs_to_jiffies(chip->qc_started && !chip->qc_failed &&
					 chip->negotiated_uv < 8500000 ? 500 : 5000));
notify:
	power_supply_changed(chip->chg_psy);
out:
	mutex_unlock(&chip->lock);
	/* Hold wake while attached: polling must also protect suspended charging. */
}

static int smb5_get_iio_chan(struct smb5_chip *chip, struct iio_channel *chan,
			     int *val)
{
	int rc;
	int online;

	rc = smb5_get_prop_usb_online(chip, &online);
	if (rc)
		return rc;
	if (!online) {
		*val = 0;
		return 0;
	}

	if (IS_ERR(chan))
		return PTR_ERR(chan);

	rc = smb5_read_processed(chan, val);
	if (rc < 0)
		return rc;
	/* ADC5 USB_IN_I is a voltage: PMI632 buck sense is 0.4 V/A. */
	if (chan == chip->usb_in_i_chan)
		*val = DIV_ROUND_CLOSEST(*val * 5, 2);
	return 0;
}

static int smb5_get_prop_health(struct smb5_chip *chip, int *val)
{
	unsigned int stat;
	int rc;

	rc = regmap_read(chip->regmap,
			 chip->base + BATTERY_CHARGER_STATUS_2, &stat);
	if (rc < 0) {
		dev_err(chip->dev, "Couldn't read charger status: %d\n", rc);
		return rc;
	}

	if (stat & CHARGER_ERROR_STATUS_BAT_OV_BIT) {
		*val = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		return 0;
	}

	rc = regmap_read(chip->regmap, chip->base + BATTERY_CHARGER_STATUS_7, &stat);
	if (rc)
		return rc;
	if (stat & BAT_TEMP_STATUS_TOO_HOT_BIT) {
		*val = POWER_SUPPLY_HEALTH_OVERHEAT;
		return 0;
	}
	if (stat & BAT_TEMP_STATUS_TOO_COLD_BIT) {
		*val = POWER_SUPPLY_HEALTH_COLD;
		return 0;
	}

	*val = POWER_SUPPLY_HEALTH_GOOD;
	return 0;
}

static int smb5_get_property(struct power_supply *psy,
			     enum power_supply_property psp,
			     union power_supply_propval *val)
{
	struct smb5_chip *chip = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_MANUFACTURER:
		val->strval = "Qualcomm";
		return 0;
	case POWER_SUPPLY_PROP_MODEL_NAME:
		val->strval = chip->name;
		return 0;
	case POWER_SUPPLY_PROP_CURRENT_MAX: {
		int ret = smb5_get_current_limit(chip, &val->intval);

		if (!ret)
			val->intval += READ_ONCE(chip->parallel_icl_ua);
		return ret;
	}
	case POWER_SUPPLY_PROP_CURRENT_NOW:
		return smb5_get_iio_chan(chip, chip->usb_in_i_chan,
					&val->intval);
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = READ_ONCE(chip->negotiated_uv) ?: 5000000;
		return 0;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		return smb5_get_iio_chan(chip, chip->usb_in_v_chan,
					&val->intval);
	case POWER_SUPPLY_PROP_ONLINE:
		return smb5_get_prop_usb_online(chip, &val->intval);
	case POWER_SUPPLY_PROP_STATUS:
		return smb5_get_prop_status(chip, &val->intval);
	case POWER_SUPPLY_PROP_HEALTH:
		return smb5_get_prop_health(chip, &val->intval);
	case POWER_SUPPLY_PROP_USB_TYPE:
		return smb5_apsd_get_charger_type(chip, &val->intval);
	default:
		dev_err(chip->dev, "invalid property: %d\n", psp);
		return -EINVAL;
	}
}

static int smb5_set_property(struct power_supply *psy,
			     enum power_supply_property psp,
			     const union power_supply_propval *val)
{
	struct smb5_chip *chip = power_supply_get_drvdata(psy);

	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		if (val->intval < 0 || val->intval > chip->input_limit_ua)
			return -EINVAL;
		mutex_lock(&chip->lock);
		/* A zero limit suspends input; a later nonzero request resumes it. */
		{
			int rc = regmap_update_bits(chip->regmap,
					chip->base + USBIN_CMD_IL,
					USBIN_SUSPEND_BIT,
					val->intval ? 0 : USBIN_SUSPEND_BIT);
			if (rc) {
				mutex_unlock(&chip->lock);
				return rc;
			}
		}
		chip->user_limit_ua = val->intval;
		mutex_unlock(&chip->lock);
		mod_delayed_work(system_wq, &chip->status_change_work, 0);
		return 0;
	default:
		dev_err(chip->dev, "No setter for property: %d\n", psp);
		return -EINVAL;
	}
}

static int smb5_property_is_writable(struct power_supply *psy,
				     enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		return 1;
	default:
		return 0;
	}
}

static irqreturn_t smb5_handle_batt_overvoltage(int irq, void *data)
{
	struct smb5_chip *chip = data;
	unsigned int status;

	if (regmap_read(chip->regmap,
			chip->base + BATTERY_CHARGER_STATUS_2, &status))
		return IRQ_HANDLED;

	if (status & CHARGER_ERROR_STATUS_BAT_OV_BIT) {
		dev_err(chip->dev, "battery overvoltage detected\n");
		power_supply_changed(chip->chg_psy);
	}

	return IRQ_HANDLED;
}

static irqreturn_t smb5_handle_usb_plugin(int irq, void *data)
{
	struct smb5_chip *chip = data;

	power_supply_changed(chip->chg_psy);
	mod_delayed_work(system_wq, &chip->status_change_work, 0);

	return IRQ_HANDLED;
}

static irqreturn_t smb5_handle_usb_icl_change(int irq, void *data)
{
	struct smb5_chip *chip = data;

	power_supply_changed(chip->chg_psy);

	return IRQ_HANDLED;
}

static irqreturn_t smb5_handle_wdog_bark(int irq, void *data)
{
	struct smb5_chip *chip = data;

	power_supply_changed(chip->chg_psy);
	/* Only the worker pets after temperature supervision. A stuck worker
	 * must allow watchdog bite to stop charging, even if IRQs still run.
	 */
	mod_delayed_work(system_wq, &chip->status_change_work, 0);

	return IRQ_HANDLED;
}

static const struct power_supply_desc smb5_psy_desc = {
	.name = "pmi632_charger",
	.type = POWER_SUPPLY_TYPE_USB,
	.usb_types = BIT(POWER_SUPPLY_USB_TYPE_SDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_CDP) |
		     BIT(POWER_SUPPLY_USB_TYPE_DCP) |
		     BIT(POWER_SUPPLY_USB_TYPE_UNKNOWN),
	.properties = smb5_properties,
	.num_properties = ARRAY_SIZE(smb5_properties),
	.get_property = smb5_get_property,
	.set_property = smb5_set_property,
	.property_is_writeable = smb5_property_is_writable,
};

/*
 * Hardware init sequence for PMI632 SMB5.
 *
 * Compared to SMB2, we skip registers that don't exist on SMB5:
 *   - AICL_RERUN_TIME_CFG
 *   - TYPE_C_INTRPT_ENB_SOFTWARE_CTRL (Type-C handled by separate driver)
 *   - TYPE_C_CFG
 *   - OTG_CFG (different offset, not needed for sink-only)
 *   - FG_UPDATE_CFG_2_SEL
 *   - PRE_CHARGE_CURRENT_CFG
 *   - OTG_ENG_OTG_CFG / DC_ENG_SSUPPLY_CFG2
 *   - STAT_CFG
 */
static const struct smb5_register smb5_init_seq[] = {
	/* Laurel stock fast-charge safety timeout: 768 minutes. */
	{ .addr = FAST_CHARGE_SAFETY_TIMER_CFG, .mask = GENMASK(1, 0), .val = 2 },
	/* Software compensation runs after verified stock hard thresholds. */
	{ .addr = JEITA_EN_CFG, .mask = GENMASK(3, 0), .val = 0 },
	/* High-voltage negotiation requires PHY DP/DM ownership, not yet wired. */
	{ .addr = USBIN_OPTIONS_1_CFG,
	  .mask = HVDCP_EN_BIT | HVDCP_AUTH_ALG_EN_BIT | HVDCP_AUTONOMOUS_EN_BIT |
		  BC1P2_SRC_DETECT_BIT,
	  .val = BC1P2_SRC_DETECT_BIT },
	/* Both Laurel 4.14 references select FLOAT_SDP and a 300 ms DCD
	 * timeout. Clear inherited float suspend/disable options as stock does;
	 * the worker still limits unclassified sources to 500 mA.
	 */
	{ .addr = USBIN_OPTIONS_2_CFG,
	  .mask = FLOAT_OPTIONS_MASK | DCD_TIMEOUT_SEL_BIT,
	  .val = FORCE_FLOAT_SDP_CFG_BIT },
	/*
	 * Keep the existing VBAT recharge fallback, but disable charge inhibit
	 * as Laurel's stock SMB5 initialization does when its DT supplies no
	 * qcom,chg-inhibit-threshold-mv. Enabling inhibit here can suppress a
	 * new charge cycle near float voltage and reports that state as Full.
	 * Stock's SOC-based recharge requires the QG MSOC producer; the voltage
	 * recharge threshold remains inherited and is not stock recharge parity.
	 * On SMB5, CHGR_CFG2 upper bits differ from SMB2 —
	 * only the recharge and inhibit bits are safe to touch.
	 */
	{ .addr = CHGR_CFG2,
	  .mask = RECHG_MASK | CHARGER_INHIBIT_BIT,
	  .val = VBAT_BASED_RECHG_BIT },
	/* Set SDP default to 500mA USB 2.0 port */
	{ .addr = USBIN_ICL_OPTIONS,
	  .mask = USB51_MODE_BIT | USBIN_MODE_CHG_BIT,
	  .val = USB51_MODE_BIT },
	/* Stop charging on watchdog bite; poll/pet while connected. */
	{ .addr = SNARL_BARK_BITE_WD_CFG,
	  .mask = BITE_WDOG_DISABLE_CHARGING_CFG_BIT,
	  .val = BITE_WDOG_DISABLE_CHARGING_CFG_BIT },
	{ .addr = WD_CFG,
	  .mask = WDOG_TIMER_EN_ON_PLUGIN_BIT | BARK_WDOG_INT_EN_BIT,
	  .val = WDOG_TIMER_EN_ON_PLUGIN_BIT | BARK_WDOG_INT_EN_BIT },
	/* Enable AICL */
	{ .addr = USBIN_AICL_OPTIONS_CFG,
	  .mask = USBIN_AICL_START_AT_MAX_BIT | USBIN_AICL_ADC_EN_BIT |
		  USBIN_AICL_EN_BIT | SUSPEND_ON_COLLAPSE_USBIN_BIT |
		  USBIN_HV_COLLAPSE_RESPONSE_BIT |
		  USBIN_LV_COLLAPSE_RESPONSE_BIT,
	  .val = USBIN_HV_COLLAPSE_RESPONSE_BIT |
		 USBIN_LV_COLLAPSE_RESPONSE_BIT | USBIN_AICL_EN_BIT },
	/*
	 * Limit fast-charge current to 1A as a safe default.
	 * SMB5 uses 50 mA steps: 1000000 / 50000 = 20.
	 */
	{ .addr = FAST_CHARGE_CURRENT_CFG,
	  .mask = FAST_CHARGE_CURRENT_SETTING_MASK,
	  .val = 1000000 / CURRENT_SCALE_FACTOR },
};

static int smb5_init_hw(struct smb5_chip *chip)
{
	int rc, i;

	rc = regmap_update_bits(chip->regmap, chip->base + 0x648,
				BIT(3) | BIT(4) | BIT(2), BIT(3));
	if (rc)
		return rc;
	for (i = 0; i < ARRAY_SIZE(smb5_init_seq); i++) {
		dev_dbg(chip->dev, "%d: writing 0x%02x to 0x%04x\n", i,
			smb5_init_seq[i].val,
			chip->base + smb5_init_seq[i].addr);
		rc = regmap_update_bits(chip->regmap,
					chip->base + smb5_init_seq[i].addr,
					smb5_init_seq[i].mask,
					smb5_init_seq[i].val);
		if (rc < 0)
			return dev_err_probe(chip->dev, rc,
					     "init command %d failed\n", i);
	}

	return 0;
}

static int smb5_init_irq(struct smb5_chip *chip, int *irq, const char *name,
			 irqreturn_t (*handler)(int irq, void *data))
{
	int irqnum;
	int rc;

	irqnum = platform_get_irq_byname(to_platform_device(chip->dev), name);
	if (irqnum < 0)
		return irqnum;

	rc = devm_request_threaded_irq(chip->dev, irqnum, NULL, handler,
				       IRQF_ONESHOT, name, chip);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc,
				     "Couldn't request irq %s\n", name);

	if (irq)
		*irq = irqnum;

	return 0;
}

static void smb5_cleanup(void *data)
{
	struct smb5_chip *chip = data;

	WRITE_ONCE(chip->stopping, true);
	cancel_delayed_work_sync(&chip->status_change_work);
	smb5_parallel_update(chip, 0);
	smb5_qc_reset(chip);
	regmap_update_bits(chip->regmap, chip->base + CHARGING_ENABLE_CMD,
			   CHARGING_ENABLE_CMD_BIT, 0);
	pm_relax(chip->dev);
	dev_pm_clear_wake_irq(chip->dev);
	device_init_wakeup(chip->dev, false);
	power_supply_put_battery_info(chip->chg_psy, chip->batt_info);
}

static int smb5_probe(struct platform_device *pdev)
{
	struct power_supply_config supply_config = {};
	struct power_supply_desc *desc;
	struct smb5_chip *chip;
	int rc, irq;
	u32 thresholds[2];
	__be16 raw;

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;

	chip->dev = &pdev->dev;
	chip->negotiated_uv = 5000000;
	if (device_property_read_bool(chip->dev, "qcom,enable-hvdcp")) {
		chip->dpdm = devm_regulator_get(chip->dev, "dpdm");
		if (IS_ERR(chip->dpdm))
			return dev_err_probe(chip->dev, PTR_ERR(chip->dpdm), "Missing PHY DPDM control\n");
	}
	chip->name = pdev->name;
	mutex_init(&chip->lock);
	INIT_DELAYED_WORK(&chip->status_change_work, smb5_status_change_work);
	rc = device_property_read_u32(chip->dev, "input-current-limit-microamp",
				      &chip->input_limit_ua);
	if (rc || chip->input_limit_ua < SDP_CURRENT_UA ||
	    chip->input_limit_ua > 2000000)
		return dev_err_probe(chip->dev, -EINVAL, "Invalid input limit\n");
	chip->user_limit_ua = chip->input_limit_ua;

	chip->regmap = dev_get_regmap(pdev->dev.parent, NULL);
	if (!chip->regmap)
		return dev_err_probe(chip->dev, -ENODEV,
				     "failed to locate the regmap\n");

	rc = device_property_read_u32(chip->dev, "reg", &chip->base);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc,
				     "Couldn't read base address\n");

	chip->usb_in_v_chan = devm_iio_channel_get(chip->dev, "usbin_v");
	if (IS_ERR(chip->usb_in_v_chan))
		return dev_err_probe(chip->dev, PTR_ERR(chip->usb_in_v_chan),
				     "Couldn't get usbin_v IIO channel\n");

	chip->usb_in_i_chan = devm_iio_channel_get(chip->dev, "usbin_i");
	if (IS_ERR(chip->usb_in_i_chan))
		return dev_err_probe(chip->dev, PTR_ERR(chip->usb_in_i_chan),
				     "Couldn't get usbin_i IIO channel\n");

	chip->batt_therm = devm_iio_channel_get(chip->dev, "batt-therm");
	if (IS_ERR(chip->batt_therm))
		return dev_err_probe(chip->dev, PTR_ERR(chip->batt_therm),
				     "Missing battery temperature\n");
	chip->chg_therm = devm_iio_channel_get(chip->dev, "chg-temp");
	if (IS_ERR(chip->chg_therm))
		return dev_err_probe(chip->dev, PTR_ERR(chip->chg_therm),
				     "Missing charger temperature\n");

	/* Fail closed before registering the supply or programming limits. */
	rc = regmap_update_bits(chip->regmap, chip->base + CHARGING_ENABLE_CMD,
				CHARGING_ENABLE_CMD_BIT, 0);
	if (rc)
		return rc;

	supply_config.drv_data = chip;
	supply_config.fwnode = dev_fwnode(&pdev->dev);

	desc = devm_kzalloc(chip->dev, sizeof(smb5_psy_desc), GFP_KERNEL);
	if (!desc)
		return -ENOMEM;
	memcpy(desc, &smb5_psy_desc, sizeof(smb5_psy_desc));
	desc->name = devm_kasprintf(chip->dev, GFP_KERNEL, "%s-charger",
				    (const char *)device_get_match_data(chip->dev));
	if (!desc->name)
		return -ENOMEM;

	chip->chg_psy =
		devm_power_supply_register(chip->dev, desc, &supply_config);
	if (IS_ERR(chip->chg_psy))
		return dev_err_probe(chip->dev, PTR_ERR(chip->chg_psy),
				     "failed to register power supply\n");

	rc = power_supply_get_battery_info(chip->chg_psy, &chip->batt_info);
	if (rc)
		return dev_err_probe(chip->dev, rc,
				     "Failed to get battery info\n");

	rc = devm_add_action_or_reset(chip->dev, smb5_cleanup, chip);
	if (rc)
		return rc;
	if (chip->batt_info->constant_charge_current_max_ua <= 0 ||
	    chip->batt_info->constant_charge_current_max_ua > 3000000 ||
	    chip->batt_info->constant_charge_voltage_max_uv < 3600000 ||
	    chip->batt_info->constant_charge_voltage_max_uv > 4400000 ||
	    chip->batt_info->charge_term_current_ua <= 0 ||
	    chip->batt_info->charge_term_current_ua > 5000000)
		return dev_err_probe(chip->dev, -EINVAL, "Invalid battery limits\n");

	rc = device_property_read_u32_array(chip->dev, "qcom,jeita-hard-thresholds",
					    thresholds, ARRAY_SIZE(thresholds));
	if (rc || thresholds[0] > U16_MAX || thresholds[1] > U16_MAX)
		return dev_err_probe(chip->dev, -EINVAL, "Missing JEITA thresholds\n");
	/* Stock writes hot then cold, in big-endian register order. */
	raw = cpu_to_be16(thresholds[1]);
	rc = regmap_bulk_write(chip->regmap,
			       chip->base + JEITA_HARD_HOT_THRESHOLD, &raw, sizeof(raw));
	if (rc)
		return rc;
	raw = cpu_to_be16(thresholds[0]);
	rc = regmap_bulk_write(chip->regmap,
			       chip->base + JEITA_HARD_COLD_THRESHOLD, &raw, sizeof(raw));
	if (rc)
		return rc;
	rc = smb5_init_hw(chip);
	if (rc)
		return rc;
	/* Stock PMI632 ADC termination uses a signed, negative charging current
	 * with a 5 A full scale, big-endian upper threshold. Lower threshold
	 * is not provided by Laurel DT and remains at its hardware setting.
	 */
	raw = cpu_to_be16((s16)div_s64(-32767LL *
				chip->batt_info->charge_term_current_ua, 5000000));
	rc = regmap_bulk_write(chip->regmap, chip->base + ADC_ITERM_UP_THRESHOLD,
			       &raw, sizeof(raw));
	if (rc)
		return rc;
	rc = regmap_update_bits(chip->regmap, chip->base + CHGR_ENG_CHARGING_CFG,
				ITERM_USE_ANALOG_BIT, 0);
	if (rc)
		return rc;

	rc = smb5_init_irq(chip, &irq, "bat-ov",
			   smb5_handle_batt_overvoltage);
	if (rc < 0)
		return rc;

	rc = smb5_init_irq(chip, &chip->cable_irq, "usb-plugin",
			   smb5_handle_usb_plugin);
	if (rc < 0)
		return rc;

	rc = smb5_init_irq(chip, &irq, "usbin-icl-change",
			   smb5_handle_usb_icl_change);
	if (rc < 0)
		return rc;

	rc = smb5_init_irq(chip, &irq, "wdog-bark",
			   smb5_handle_wdog_bark);
	if (rc < 0)
		return rc;

	rc = device_init_wakeup(chip->dev, true);
	if (rc)
		return rc;
	rc = dev_pm_set_wake_irq(chip->dev, chip->cable_irq);
	if (rc < 0)
		return dev_err_probe(chip->dev, rc,
				     "Couldn't set wake irq\n");

	platform_set_drvdata(pdev, chip);

	rc = smb5_set_current_limit(chip, SDP_CURRENT_UA);
	if (rc)
		return rc;
	rc = regmap_update_bits(chip->regmap, chip->base + USBIN_CMD_IL,
				USBIN_SUSPEND_BIT, 0);
	if (rc)
		return rc;
	WRITE_ONCE(chip->ready, true);

	/* Kick off initial charger state detection */
	schedule_delayed_work(&chip->status_change_work, 0);

	return 0;
}

static void smb5_shutdown(struct platform_device *pdev)
{
	struct smb5_chip *chip = platform_get_drvdata(pdev);

	WRITE_ONCE(chip->stopping, true);
	cancel_delayed_work_sync(&chip->status_change_work);
	regmap_update_bits(chip->regmap, chip->base + CHARGING_ENABLE_CMD,
			   CHARGING_ENABLE_CMD_BIT, 0);
	pm_relax(chip->dev);
}

static const struct of_device_id smb5_match_id_table[] = {
	{ .compatible = "qcom,pmi632-charger", .data = "pmi632" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, smb5_match_id_table);

static struct platform_driver qcom_spmi_smb5 = {
	.probe = smb5_probe,
	.shutdown = smb5_shutdown,
	.driver = {
		.name = "qcom-pmi632-charger",
		.of_match_table = smb5_match_id_table,
	},
};

module_platform_driver(qcom_spmi_smb5);

MODULE_AUTHOR("Marc Lainez <marc.lainez@gmail.com>");
MODULE_DESCRIPTION("Qualcomm PMI632 SMB5 Charger Driver");
MODULE_LICENSE("GPL");
