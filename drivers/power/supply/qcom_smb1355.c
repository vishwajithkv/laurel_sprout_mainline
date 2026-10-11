// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (c) 2016-2018 The Linux Foundation. All rights reserved.
 * Adapted from LineageOS sm6125 smb1355-charger.c, revision
 * 77d2912bc00eda19182a95a24bbe23543c792814. Stock register sequencing retained;
 * downstream votables replaced by standard power_supply properties.
 */
#include <linux/bitops.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/power_supply.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>
#include <linux/property.h>

#define REVID_BASE 0x0100
#define I2C_SS_DIG_BASE 0x0E00
#define CHGR_BASE 0x1000
#define ANA2_BASE 0x1100
#define BATIF_BASE 0x1200
#define USBIN_BASE 0x1300
#define ANA1_BASE 0x1400
#define MISC_BASE 0x1600

#define REVID_MFG_ID_SPARE_REG (REVID_BASE + 0xFF)

#define I2C_SS_DIG_PMIC_SID_REG (I2C_SS_DIG_BASE + 0x45)
#define PMIC_SID_MASK GENMASK(3, 0)
#define PMIC_SID0_BIT BIT(0)

#define BATTERY_STATUS_2_REG (CHGR_BASE + 0x0B)
#define DISABLE_CHARGING_BIT BIT(3)

#define BATTERY_STATUS_3_REG (CHGR_BASE + 0x0C)
#define BATT_GT_PRE_TO_FAST_BIT BIT(4)
#define ENABLE_CHARGING_BIT BIT(3)

#define CHGR_CHARGING_ENABLE_CMD_REG (CHGR_BASE + 0x42)
#define CHARGING_ENABLE_CMD_BIT BIT(0)

#define CHGR_CFG2_REG (CHGR_BASE + 0x51)
#define CHG_EN_SRC_BIT BIT(7)
#define CHG_EN_POLARITY_BIT BIT(6)

#define CFG_REG (CHGR_BASE + 0x53)
#define CHG_OPTION_PIN_TRIM_BIT BIT(7)
#define BATN_SNS_CFG_BIT BIT(4)
#define CFG_TAPER_DIS_AFVC_BIT BIT(3)
#define BATFET_SHUTDOWN_CFG_BIT BIT(2)
#define VDISCHG_EN_CFG_BIT BIT(1)
#define VCHG_EN_CFG_BIT BIT(0)

#define FAST_CHARGE_CURRENT_CFG_REG (CHGR_BASE + 0x61)
#define FAST_CHARGE_CURRENT_SETTING_MASK GENMASK(7, 0)

#define CHGR_BATTOV_CFG_REG (CHGR_BASE + 0x70)
#define BATTOV_SETTING_MASK GENMASK(7, 0)

#define CHGR_PRE_TO_FAST_THRESHOLD_CFG_REG (CHGR_BASE + 0x74)
#define PRE_TO_FAST_CHARGE_THRESHOLD_MASK GENMASK(2, 0)

#define ANA2_TR_SBQ_ICL_1X_REF_OFFSET_REG (ANA2_BASE + 0xF5)
#define TR_SBQ_ICL_1X_REF_OFFSET GENMASK(4, 0)

#define POWER_MODE_HICCUP_CFG (BATIF_BASE + 0x72)
#define MAX_HICCUP_DUETO_BATDIS_MASK GENMASK(5, 2)
#define HICCUP_TIMEOUT_CFG_MASK GENMASK(1, 0)

#define BATIF_CFG_SMISC_BATID_REG (BATIF_BASE + 0x73)
#define CFG_SMISC_RBIAS_EXT_CTRL_BIT BIT(2)

#define SMB2CHG_BATIF_ENG_SMISC_DIETEMP (BATIF_BASE + 0xC0)
#define TDIE_COMPARATOR_THRESHOLD GENMASK(5, 0)
#define DIE_LOW_RANGE_BASE_DEGC 34
#define DIE_LOW_RANGE_DELTA 16
#define DIE_LOW_RANGE_MAX_DEGC 97
#define DIE_LOW_RANGE_SHIFT 4

#define BATIF_ENG_SCMISC_SPARE1_REG (BATIF_BASE + 0xC2)
#define EXT_BIAS_PIN_BIT BIT(2)
#define DIE_TEMP_COMP_HYST_BIT BIT(1)

#define ANA1_ENG_SREFGEN_CFG2_REG (ANA1_BASE + 0xC1)
#define VALLEY_COMPARATOR_EN_BIT BIT(0)

#define TEMP_COMP_STATUS_REG (MISC_BASE + 0x07)
#define TEMP_RST_HOT_BIT BIT(2)
#define TEMP_UB_HOT_BIT BIT(1)
#define TEMP_LB_HOT_BIT BIT(0)
#define SKIN_TEMP_SHIFT 4

#define MISC_RT_STS_REG (MISC_BASE + 0x10)
#define HARD_ILIMIT_RT_STS_BIT BIT(5)

#define BANDGAP_ENABLE_REG (MISC_BASE + 0x42)
#define BANDGAP_ENABLE_CMD_BIT BIT(0)

#define BARK_BITE_WDOG_PET_REG (MISC_BASE + 0x43)
#define BARK_BITE_WDOG_PET_BIT BIT(0)

#define CLOCK_REQUEST_REG (MISC_BASE + 0x44)
#define CLOCK_REQUEST_CMD_BIT BIT(0)

#define WD_CFG_REG (MISC_BASE + 0x51)
#define WATCHDOG_TRIGGER_AFP_EN_BIT BIT(7)
#define BARK_WDOG_INT_EN_BIT BIT(6)
#define BITE_WDOG_INT_EN_BIT BIT(5)
#define WDOG_IRQ_SFT_BIT BIT(2)
#define WDOG_TIMER_EN_ON_PLUGIN_BIT BIT(1)
#define WDOG_TIMER_EN_BIT BIT(0)

#define MISC_CUST_SDCDC_CLK_CFG_REG (MISC_BASE + 0xA0)
#define SWITCHER_CLK_FREQ_MASK GENMASK(3, 0)

#define MISC_CUST_SDCDC_ILIMIT_CFG_REG (MISC_BASE + 0xA1)
#define LS_VALLEY_THRESH_PCT_BIT BIT(3)
#define PCL_LIMIT_MASK GENMASK(1, 0)

#define SNARL_BARK_BITE_WD_CFG_REG (MISC_BASE + 0x53)
#define BITE_WDOG_DISABLE_CHARGING_CFG_BIT BIT(7)
#define SNARL_WDOG_TIMEOUT_MASK GENMASK(6, 4)
#define BARK_WDOG_TIMEOUT_MASK GENMASK(3, 2)
#define BITE_WDOG_TIMEOUT_MASK GENMASK(1, 0)

#define MISC_THERMREG_SRC_CFG_REG (MISC_BASE + 0x70)
#define BYP_THERM_CHG_CURR_ADJUST_BIT BIT(2)
#define THERMREG_SKIN_CMP_SRC_EN_BIT BIT(1)
#define THERMREG_DIE_CMP_SRC_EN_BIT BIT(0)

#define MISC_CHGR_TRIM_OPTIONS_REG (MISC_BASE + 0x55)
#define CMD_RBIAS_EN_BIT BIT(2)

#define MISC_ENG_SDCDC_INPUT_CURRENT_CFG1_REG (MISC_BASE + 0xC8)
#define PROLONG_ISENSE_MASK GENMASK(7, 6)
#define PROLONG_ISENSEM_SHIFT 6
#define SAMPLE_HOLD_DELAY_MASK GENMASK(5, 2)
#define SAMPLE_HOLD_DELAY_SHIFT 2
#define DISABLE_ILIMIT_BIT BIT(0)

#define MISC_ENG_SDCDC_INPUT_CURRENT_CFG2_REG (MISC_BASE + 0xC9)
#define INPUT_CURRENT_LIMIT_SOURCE_BIT BIT(7)
#define TC_ISENSE_AMPLIFIER_MASK GENMASK(6, 4)
#define TC_ISENSE_AMPLIFIER_SHIFT 4
#define HS_II_CORRECTION_MASK GENMASK(3, 0)

#define MISC_ENG_SDCDC_RESERVE3_REG (MISC_BASE + 0xCB)
#define VDDCAP_SHORT_DISABLE_TRISTATE_BIT BIT(7)
#define PCL_SHUTDOWN_BUCK_BIT BIT(6)
#define ISENSE_TC_CORRECTION_BIT BIT(5)
#define II_SOURCE_BIT BIT(4)
#define SCALE_SLOPE_COMP_MASK GENMASK(3, 0)

#define USBIN_CURRENT_LIMIT_CFG_REG (USBIN_BASE + 0x70)
#define USB_TR_SCPATH_ICL_1X_GAIN_REG (USBIN_BASE + 0xF2)
#define TR_SCPATH_ICL_1X_GAIN_MASK GENMASK(5, 0)

struct smb_chg_param {
	const char *name;
	u16 reg;
	int min_u;
	int max_u;
	int step_u;
};

struct smb_params {
	struct smb_chg_param fcc;
	struct smb_chg_param ov;
	struct smb_chg_param usb_icl;
};

static struct smb_params v1_params = {
	.fcc		= {
		.name	= "fast charge current",
		.reg	= FAST_CHARGE_CURRENT_CFG_REG,
		.min_u	= 0,
		.max_u	= 6000000,
		.step_u	= 25000,
	},
	.ov		= {
		.name	= "battery over voltage",
		.reg	= CHGR_BATTOV_CFG_REG,
		.min_u	= 2450000,
		.max_u	= 5000000,
		.step_u	= 10000,
	},
	.usb_icl	= {
		.name   = "usb input current limit",
		.reg    = USBIN_CURRENT_LIMIT_CFG_REG,
		.min_u  = 100000,
		.max_u  = 5000000,
		.step_u = 30000,
	},
};

#define IS_USBIN(mode) ((mode) == 1)
struct smb1355 {
	struct device *dev;
	struct regmap *regmap;
	struct mutex write_lock, lock;
	struct power_supply *psy;
	struct delayed_work watchdog;
	struct smb_params param;
	struct {
		bool disable_ctm, hw_die_temp_mitigation;
		u32 die_temp_threshold;
		int pl_mode;
	} dt;
	const char *name;
	bool disabled, stopping, fault;
	int fcc, icl, fv;
};

static int smb1355_masked_write(struct smb1355 *chip, u16 addr, u8 mask, u8 val)
{
	int ret;
	mutex_lock(&chip->write_lock);
	if ((addr & 0xff) >= 0xa0 || addr == CLOCK_REQUEST_REG ||
	    addr == I2C_SS_DIG_PMIC_SID_REG) {
		ret = regmap_write(chip->regmap, (addr & 0xff00) | 0xd0, 0xa5);
		if (ret)
			goto out;
	}
	/* Force the bus write: clock requests and interrupt strobes are commands. */
	ret = regmap_write_bits(chip->regmap, addr, mask, val);
out:
	mutex_unlock(&chip->write_lock);
	return ret;
}
static int smb1355_write(struct smb1355 *chip, u16 addr, u8 val)
{
	return smb1355_masked_write(chip, addr, 0xff, val);
}
static int smb1355_clk_request(struct smb1355 *chip, bool enable)
{
	return smb1355_masked_write(chip, CLOCK_REQUEST_REG,
				    CLOCK_REQUEST_CMD_BIT,
				    enable ? CLOCK_REQUEST_CMD_BIT : 0);
}
static int smb1355_set_parallel_charging(struct smb1355 *chip, bool disable)
{
	int ret;
	if (!disable && (chip->fault || chip->stopping))
		return -EIO;
	ret = smb1355_masked_write(chip, CHGR_CFG2_REG,
				   CHG_EN_POLARITY_BIT | CHG_EN_SRC_BIT,
				   disable ? 0 : CHG_EN_SRC_BIT);
	if (!ret)
		ret = smb1355_masked_write(chip, WD_CFG_REG, WDOG_TIMER_EN_BIT,
					   disable ? 0 : WDOG_TIMER_EN_BIT);
	if (!ret)
		ret = smb1355_masked_write(
			chip, BANDGAP_ENABLE_REG, BANDGAP_ENABLE_CMD_BIT,
			disable ? 0 : BANDGAP_ENABLE_CMD_BIT);
	if (ret) {
		smb1355_masked_write(chip, CHGR_CFG2_REG, CHG_EN_SRC_BIT, 0);
		chip->disabled = true;
	} else
		chip->disabled = disable;
	return ret;
}
static int smb1355_set_charge_param(struct smb1355 *chip,
				    struct smb_chg_param *param, int val_u)
{
	int rc;
	u8 val_raw;

	if (val_u > param->max_u || val_u < param->min_u) {
		pr_err("%s: %d is out of range [%d, %d]\n", param->name, val_u,
		       param->min_u, param->max_u);
		return -EINVAL;
	}

	val_raw = (val_u - param->min_u) / param->step_u;

	rc = smb1355_write(chip, param->reg, val_raw);
	if (rc < 0) {
		pr_err("%s: Couldn't write 0x%02x to 0x%04x rc=%d\n",
		       param->name, val_raw, param->reg, rc);
		return rc;
	}

	return rc;
}
static int smb1355_tskin_sensor_config(struct smb1355 *chip)
{
	int rc;

	if (chip->dt.disable_ctm) {
		/*
		 * the TSKIN sensor with external resistor needs a bias,
		 * disable it here.
		 */
		rc = smb1355_masked_write(chip, BATIF_ENG_SCMISC_SPARE1_REG,
					  EXT_BIAS_PIN_BIT, 0);
		if (rc < 0) {
			pr_err("Couldn't enable ext bias pin path rc=%d\n", rc);
			return rc;
		}

		rc = smb1355_masked_write(chip, BATIF_CFG_SMISC_BATID_REG,
					  CFG_SMISC_RBIAS_EXT_CTRL_BIT, 0);
		if (rc < 0) {
			pr_err("Couldn't set  BATIF_CFG_SMISC_BATID rc=%d\n",
			       rc);
			return rc;
		}

		rc = smb1355_masked_write(chip, MISC_CHGR_TRIM_OPTIONS_REG,
					  CMD_RBIAS_EN_BIT, 0);
		if (rc < 0) {
			pr_err("Couldn't set MISC_CHGR_TRIM_OPTIONS rc=%d\n",
			       rc);
			return rc;
		}

		/* disable skin temperature comparator source */
		rc = smb1355_masked_write(chip, MISC_THERMREG_SRC_CFG_REG,
					  THERMREG_SKIN_CMP_SRC_EN_BIT, 0);
		if (rc < 0) {
			pr_err("Couldn't set Skin temp comparator src rc=%d\n",
			       rc);
			return rc;
		}
	} else {
		/*
		 * the TSKIN sensor with external resistor needs a bias,
		 * enable it here.
		 */
		rc = smb1355_masked_write(chip, BATIF_ENG_SCMISC_SPARE1_REG,
					  EXT_BIAS_PIN_BIT, EXT_BIAS_PIN_BIT);
		if (rc < 0) {
			pr_err("Couldn't enable ext bias pin path rc=%d\n", rc);
			return rc;
		}

		rc = smb1355_masked_write(chip, BATIF_CFG_SMISC_BATID_REG,
					  CFG_SMISC_RBIAS_EXT_CTRL_BIT,
					  CFG_SMISC_RBIAS_EXT_CTRL_BIT);
		if (rc < 0) {
			pr_err("Couldn't set  BATIF_CFG_SMISC_BATID rc=%d\n",
			       rc);
			return rc;
		}

		rc = smb1355_masked_write(chip, MISC_CHGR_TRIM_OPTIONS_REG,
					  CMD_RBIAS_EN_BIT, CMD_RBIAS_EN_BIT);
		if (rc < 0) {
			pr_err("Couldn't set MISC_CHGR_TRIM_OPTIONS rc=%d\n",
			       rc);
			return rc;
		}

		/* Enable skin temperature comparator source */
		rc = smb1355_masked_write(chip, MISC_THERMREG_SRC_CFG_REG,
					  THERMREG_SKIN_CMP_SRC_EN_BIT,
					  THERMREG_SKIN_CMP_SRC_EN_BIT);
		if (rc < 0) {
			pr_err("Couldn't set Skin temp comparator src rc=%d\n",
			       rc);
			return rc;
		}
	}

	return rc;
}
static int smb1355_init_hw(struct smb1355 *chip)
{
	int rc;
	u8 val, range;

	/* request clock always on */
	rc = smb1355_clk_request(chip, true);
	if (rc < 0)
		return rc;

	/* Change to let SMB1355 only respond to address 0x0C  */
	rc = smb1355_masked_write(chip, I2C_SS_DIG_PMIC_SID_REG, PMIC_SID_MASK,
				  PMIC_SID0_BIT);
	if (rc < 0) {
		pr_err("Couldn't configure the I2C_SS_DIG_PMIC_SID_REG rc=%d\n",
		       rc);
		return rc;
	}

	/* enable watchdog bark and bite interrupts, and disable the watchdog */
	rc = smb1355_masked_write(
		chip, WD_CFG_REG,
		WDOG_TIMER_EN_BIT | WDOG_TIMER_EN_ON_PLUGIN_BIT |
			BITE_WDOG_INT_EN_BIT | BARK_WDOG_INT_EN_BIT,
		BITE_WDOG_INT_EN_BIT | BARK_WDOG_INT_EN_BIT);
	if (rc < 0) {
		pr_err("Couldn't configure the watchdog rc=%d\n", rc);
		return rc;
	}

	/* disable charging when watchdog bites */
	rc = smb1355_masked_write(chip, SNARL_BARK_BITE_WD_CFG_REG,
				  BITE_WDOG_DISABLE_CHARGING_CFG_BIT,
				  BITE_WDOG_DISABLE_CHARGING_CFG_BIT);
	if (rc < 0) {
		pr_err("Couldn't configure the watchdog bite rc=%d\n", rc);
		return rc;
	}

	/*
	 * Disable command based SMB1355 enablement and disable parallel
	 * charging path by switching to command based mode.
	 */
	rc = smb1355_masked_write(chip, CHGR_CHARGING_ENABLE_CMD_REG,
				  CHARGING_ENABLE_CMD_BIT, 0);
	if (rc < 0) {
		pr_err("Coudln't configure command bit, rc=%d\n", rc);
		return rc;
	}

	rc = smb1355_set_parallel_charging(chip, true);
	if (rc < 0) {
		pr_err("Couldn't disable parallel path rc=%d\n", rc);
		return rc;
	}

	/* initialize FCC to 0 */
	rc = smb1355_set_charge_param(chip, &chip->param.fcc, 0);
	if (rc < 0) {
		pr_err("Couldn't set 0 FCC rc=%d\n", rc);
		return rc;
	}

	/* HICCUP setting, unlimited retry with 250ms interval */
	rc = smb1355_masked_write(
		chip, POWER_MODE_HICCUP_CFG,
		HICCUP_TIMEOUT_CFG_MASK | MAX_HICCUP_DUETO_BATDIS_MASK, 0);
	if (rc < 0) {
		pr_err("Couldn't set HICCUP interval rc=%d\n", rc);
		return rc;
	}

	/* enable parallel current sensing */
	rc = smb1355_masked_write(chip, CFG_REG, VCHG_EN_CFG_BIT,
				  VCHG_EN_CFG_BIT);
	if (rc < 0) {
		pr_err("Couldn't enable parallel current sensing rc=%d\n", rc);
		return rc;
	}

	/* set Pre-to-Fast Charging Threshold 2.6V */
	rc = smb1355_masked_write(chip, CHGR_PRE_TO_FAST_THRESHOLD_CFG_REG,
				  PRE_TO_FAST_CHARGE_THRESHOLD_MASK, 0);
	if (rc < 0) {
		pr_err("Couldn't set PRE_TO_FAST_CHARGE_THRESHOLD rc=%d\n", rc);
		return rc;
	}

	/* Configure DIE temp Low threshold */
	if (chip->dt.hw_die_temp_mitigation) {
		range = (chip->dt.die_temp_threshold -
			 DIE_LOW_RANGE_BASE_DEGC) /
			(DIE_LOW_RANGE_DELTA);
		val = (chip->dt.die_temp_threshold -
		       ((range * DIE_LOW_RANGE_DELTA) +
			DIE_LOW_RANGE_BASE_DEGC)) %
		      DIE_LOW_RANGE_DELTA;

		rc = smb1355_masked_write(chip, SMB2CHG_BATIF_ENG_SMISC_DIETEMP,
					  TDIE_COMPARATOR_THRESHOLD,
					  (range << DIE_LOW_RANGE_SHIFT) | val);
		if (rc < 0) {
			pr_err("Couldn't set temp comp threshold rc=%d\n", rc);
			return rc;
		}
	}

	/*
	 * Enable thermal Die temperature comparator source and
	 * enable hardware controlled current adjustment for die temp
	 * if charger is configured in h/w controlled die temp mitigation.
	 */
	val = THERMREG_DIE_CMP_SRC_EN_BIT;
	if (!chip->dt.hw_die_temp_mitigation)
		val |= BYP_THERM_CHG_CURR_ADJUST_BIT;
	rc = smb1355_masked_write(chip, MISC_THERMREG_SRC_CFG_REG,
				  THERMREG_DIE_CMP_SRC_EN_BIT |
					  BYP_THERM_CHG_CURR_ADJUST_BIT,
				  val);
	if (rc < 0) {
		pr_err("Couldn't set Skin temperature comparator src rc=%d\n",
		       rc);
		return rc;
	}

	/*
	 * Disable hysterisis for die temperature. This is so that sw can run
	 * stepping scheme quickly
	 */
	val = chip->dt.hw_die_temp_mitigation ? DIE_TEMP_COMP_HYST_BIT : 0;
	rc = smb1355_masked_write(chip, BATIF_ENG_SCMISC_SPARE1_REG,
				  DIE_TEMP_COMP_HYST_BIT, val);
	if (rc < 0) {
		pr_err("Couldn't disable hyst. for die rc=%d\n", rc);
		return rc;
	}

	/* Enable valley current comparator all the time */
	rc = smb1355_masked_write(chip, ANA1_ENG_SREFGEN_CFG2_REG,
				  VALLEY_COMPARATOR_EN_BIT,
				  VALLEY_COMPARATOR_EN_BIT);
	if (rc < 0) {
		pr_err("Couldn't enable valley current comparator rc=%d\n", rc);
		return rc;
	}

	/* Set LS_VALLEY threshold to 85% */
	rc = smb1355_masked_write(chip, MISC_CUST_SDCDC_ILIMIT_CFG_REG,
				  LS_VALLEY_THRESH_PCT_BIT,
				  LS_VALLEY_THRESH_PCT_BIT);
	if (rc < 0) {
		pr_err("Couldn't set LS valley threshold to 85pc rc=%d\n", rc);
		return rc;
	}

	/* For SMB1354, set PCL to 8.6 A */
	if (!strcmp(chip->name, "smb1354")) {
		rc = smb1355_masked_write(chip, MISC_CUST_SDCDC_ILIMIT_CFG_REG,
					  PCL_LIMIT_MASK, PCL_LIMIT_MASK);
		if (rc < 0) {
			pr_err("Couldn't set PCL limit to 8.6A rc=%d\n", rc);
			return rc;
		}
	}

	rc = smb1355_tskin_sensor_config(chip);
	if (rc < 0) {
		pr_err("Couldn't configure tskin regs rc=%d\n", rc);
		return rc;
	}

	/* USBIN-USBIN configuration */
	if (IS_USBIN(chip->dt.pl_mode)) {
		/* set swicther clock frequency to 700kHz */
		rc = smb1355_masked_write(chip, MISC_CUST_SDCDC_CLK_CFG_REG,
					  SWITCHER_CLK_FREQ_MASK, 0x03);
		if (rc < 0) {
			pr_err("Couldn't set MISC_CUST_SDCDC_CLK_CFG rc=%d\n",
			       rc);
			return rc;
		}

		/*
		 * configure compensation for input current limit (ICL) loop
		 * accuracy, scale slope compensation using 30k resistor.
		 */
		rc = smb1355_masked_write(chip, MISC_ENG_SDCDC_RESERVE3_REG,
					  II_SOURCE_BIT | SCALE_SLOPE_COMP_MASK,
					  II_SOURCE_BIT);
		if (rc < 0) {
			pr_err("Couldn't set MISC_ENG_SDCDC_RESERVE3_REG rc=%d\n",
			       rc);
			return rc;
		}

		/* configuration to improve ICL accuracy */
		rc = smb1355_masked_write(
			chip, MISC_ENG_SDCDC_INPUT_CURRENT_CFG1_REG,
			PROLONG_ISENSE_MASK | SAMPLE_HOLD_DELAY_MASK,
			((u8)0x0C << SAMPLE_HOLD_DELAY_SHIFT));
		if (rc < 0) {
			pr_err("Couldn't set MISC_ENG_SDCDC_INPUT_CURRENT_CFG1_REG rc=%d\n",
			       rc);
			return rc;
		}

		rc = smb1355_masked_write(
			chip, MISC_ENG_SDCDC_INPUT_CURRENT_CFG2_REG,
			INPUT_CURRENT_LIMIT_SOURCE_BIT | HS_II_CORRECTION_MASK,
			INPUT_CURRENT_LIMIT_SOURCE_BIT | 0xC);

		if (rc < 0) {
			pr_err("Couldn't set MISC_ENG_SDCDC_INPUT_CURRENT_CFG2_REG rc=%d\n",
			       rc);
			return rc;
		}

		/* configure DAC offset */
		rc = smb1355_masked_write(chip,
					  ANA2_TR_SBQ_ICL_1X_REF_OFFSET_REG,
					  TR_SBQ_ICL_1X_REF_OFFSET, 0x00);
		if (rc < 0) {
			pr_err("Couldn't set ANA2_TR_SBQ_ICL_1X_REF_OFFSET_REG rc=%d\n",
			       rc);
			return rc;
		}

		/* configure DAC gain */
		rc = smb1355_masked_write(chip, USB_TR_SCPATH_ICL_1X_GAIN_REG,
					  TR_SCPATH_ICL_1X_GAIN_MASK, 0x22);
		if (rc < 0) {
			pr_err("Couldn't set USB_TR_SCPATH_ICL_1X_GAIN_REG rc=%d\n",
			       rc);
			return rc;
		}
	}

	return 0;
}

static int smb1355_check(struct smb1355 *chip)
{
	unsigned int temp;
	int ret = regmap_read(chip->regmap, TEMP_COMP_STATUS_REG, &temp);
	if (ret || (temp & TEMP_RST_HOT_BIT)) {
		chip->fault = true;
		smb1355_set_parallel_charging(chip, true);
		return ret ?: -EOVERFLOW;
	}
	ret = smb1355_write(chip, BARK_BITE_WDOG_PET_REG,
			    BARK_BITE_WDOG_PET_BIT);
	if (ret) {
		chip->fault = true;
		smb1355_set_parallel_charging(chip, true);
	}
	return ret;
}
static void smb1355_watchdog(struct work_struct *work)
{
	struct smb1355 *chip =
		container_of(to_delayed_work(work), struct smb1355, watchdog);
	mutex_lock(&chip->lock);
	if (!chip->stopping) {
		smb1355_check(chip);
		schedule_delayed_work(&chip->watchdog, msecs_to_jiffies(5000));
	}
	mutex_unlock(&chip->lock);
	power_supply_changed(chip->psy);
}
static irqreturn_t smb1355_irq(int irq, void *data)
{
	struct smb1355 *chip = data;
	unsigned int latched;
	int ret, i;
	const u16 banks[] = { CHGR_BASE, BATIF_BASE, USBIN_BASE, MISC_BASE };
	mutex_lock(&chip->lock);
	for (i = 0; i < ARRAY_SIZE(banks); i++) {
		ret = regmap_read(chip->regmap, banks[i] + 0x18, &latched);
		if (ret) {
			chip->fault = true;
			break;
		}
		if (latched) {
			ret = regmap_write(chip->regmap, banks[i] + 0x14,
					   latched);
			if (ret) {
				chip->fault = true;
				break;
			}
		}
	}
	smb1355_check(chip);
	if (chip->fault)
		smb1355_set_parallel_charging(chip, true);
	mutex_unlock(&chip->lock);
	power_supply_changed(chip->psy);
	return IRQ_HANDLED;
}
static enum power_supply_property smb1355_props[] = {
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX,
};
static int smb1355_get(struct power_supply *psy,
		       enum power_supply_property prop,
		       union power_supply_propval *val)
{
	struct smb1355 *chip = power_supply_get_drvdata(psy);
	int ret = 0;
	mutex_lock(&chip->lock);
	switch (prop) {
	case POWER_SUPPLY_PROP_ONLINE:
		val->intval = !chip->disabled && !chip->fault;
		break;
	case POWER_SUPPLY_PROP_HEALTH:
		val->intval = chip->fault ? POWER_SUPPLY_HEALTH_UNSPEC_FAILURE :
					    POWER_SUPPLY_HEALTH_GOOD;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		val->intval = chip->icl;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = chip->fcc;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		val->intval = chip->fv;
		break;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&chip->lock);
	return ret;
}
static int smb1355_set(struct power_supply *psy,
		       enum power_supply_property prop,
		       const union power_supply_propval *val)
{
	struct smb1355 *chip = power_supply_get_drvdata(psy);
	bool was_disabled;
	int ret = 0;

	mutex_lock(&chip->lock);
	was_disabled = chip->disabled;
	if (chip->stopping) {
		ret = -ENODEV;
		goto out;
	}
	switch (prop) {
	case POWER_SUPPLY_PROP_ONLINE:
		if (val->intval &&
		    (chip->fault || chip->fcc <= 0 || chip->icl < 250000)) {
			ret = -EINVAL;
			break;
		}
		ret = smb1355_set_parallel_charging(chip, !val->intval);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT:
		if (val->intval < 250000 || val->intval > 2000000) {
			ret = -EINVAL;
			break;
		}
		ret = smb1355_set_charge_param(chip, &chip->param.usb_icl,
					       val->intval);
		if (!ret)
			chip->icl = chip->param.usb_icl.min_u +
				    (val->intval - chip->param.usb_icl.min_u) /
					    chip->param.usb_icl.step_u *
					    chip->param.usb_icl.step_u;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		if (val->intval < 0 || val->intval > 3000000) {
			ret = -EINVAL;
			break;
		}
		ret = smb1355_set_charge_param(chip, &chip->param.fcc,
					       val->intval);
		if (!ret)
			chip->fcc = val->intval / chip->param.fcc.step_u *
				    chip->param.fcc.step_u;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX:
		if (val->intval < 3600000 || val->intval > 4400000) {
			ret = -EINVAL;
			break;
		}
		ret = smb1355_set_charge_param(chip, &chip->param.ov,
					       val->intval);
		if (!ret)
			chip->fv = chip->param.ov.min_u +
				   (val->intval - chip->param.ov.min_u) /
					   chip->param.ov.step_u *
					   chip->param.ov.step_u;
		break;
	default:
		ret = -EINVAL;
	}
	if (ret)
		smb1355_set_parallel_charging(chip, true);
out:
	mutex_unlock(&chip->lock);
	/* power_supply_set_property() does not emit this notification. QG
	 * must switch its external current-sense mode after a path change.
	 */
	if (was_disabled != READ_ONCE(chip->disabled))
		power_supply_changed(psy);
	return ret;
}
static int smb1355_writable(struct power_supply *psy,
			    enum power_supply_property prop)
{
	return prop == POWER_SUPPLY_PROP_ONLINE ||
	       prop == POWER_SUPPLY_PROP_INPUT_CURRENT_LIMIT ||
	       prop == POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX ||
	       prop == POWER_SUPPLY_PROP_CONSTANT_CHARGE_VOLTAGE_MAX;
}
static const struct power_supply_desc smb1355_desc = {
	.name = "smb1355-parallel",
	.type = POWER_SUPPLY_TYPE_UNKNOWN,
	.properties = smb1355_props,
	.num_properties = ARRAY_SIZE(smb1355_props),
	.get_property = smb1355_get,
	.set_property = smb1355_set,
	.property_is_writeable = smb1355_writable,
};
static void smb1355_stop(void *data)
{
	struct smb1355 *chip = data;
	mutex_lock(&chip->lock);
	chip->stopping = true;
	regmap_write(chip->regmap, CHGR_BASE + 0x16, 0xff);
	regmap_write(chip->regmap, BATIF_BASE + 0x16, 0xff);
	regmap_write(chip->regmap, USBIN_BASE + 0x16, 0xff);
	regmap_write(chip->regmap, MISC_BASE + 0x16, 0xff);
	smb1355_set_parallel_charging(chip, true);
	mutex_unlock(&chip->lock);
	cancel_delayed_work_sync(&chip->watchdog);
	smb1355_clk_request(chip, false);
}
static int smb1355_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct power_supply_config pcfg = {};
	struct smb1355 *chip;
	unsigned int id;
	int ret;
	static const struct regmap_config rcfg = { .reg_bits = 16,
						   .val_bits = 8,
						   .max_register = 0xffff };
	chip = devm_kzalloc(dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;
	chip->dev = dev;
	chip->name = "smb1355";
	chip->param = v1_params;
	chip->dt.pl_mode = 1;
	chip->dt.disable_ctm = true;
	chip->dt.hw_die_temp_mitigation = true;
	chip->dt.die_temp_threshold = 90;
	mutex_init(&chip->lock);
	mutex_init(&chip->write_lock);
	INIT_DELAYED_WORK(&chip->watchdog, smb1355_watchdog);
	chip->regmap = devm_regmap_init_i2c(client, &rcfg);
	if (IS_ERR(chip->regmap))
		return dev_err_probe(dev, PTR_ERR(chip->regmap),
				     "I2C regmap unavailable\n");
	ret = regmap_read(chip->regmap, REVID_MFG_ID_SPARE_REG, &id);
	if (ret || id != 0xff)
		return dev_err_probe(dev, ret ?: -ENODEV,
				     "SMB1355 ID unavailable\n");
	ret = smb1355_init_hw(chip);
	if (ret) {
		smb1355_stop(chip);
		return dev_err_probe(dev, ret, "Initialization failed\n");
	}
	pcfg.drv_data = chip;
	pcfg.fwnode = dev_fwnode(dev);
	chip->psy = devm_power_supply_register(dev, &smb1355_desc, &pcfg);
	if (IS_ERR(chip->psy)) {
		smb1355_stop(chip);
		return PTR_ERR(chip->psy);
	}
	ret = devm_add_action_or_reset(dev, smb1355_stop, chip);
	if (ret)
		return ret;
	/* Stock peripheral IRQ banks. Mask inherited sources and select rising
  * charge-state, watchdog-bark and die-temperature events explicitly.
  */
	ret = regmap_write(chip->regmap, CHGR_BASE + 0x16, 0xff);
	if (!ret)
		ret = regmap_write(chip->regmap, BATIF_BASE + 0x16, 0xff);
	if (!ret)
		ret = regmap_write(chip->regmap, USBIN_BASE + 0x16, 0xff);
	if (!ret)
		ret = regmap_write(chip->regmap, MISC_BASE + 0x16, 0xff);
	if (!ret)
		ret = regmap_write(chip->regmap, CHGR_BASE + 0x11, BIT(1));
	if (!ret)
		ret = regmap_write(chip->regmap, CHGR_BASE + 0x12, BIT(1));
	if (!ret)
		ret = regmap_write(chip->regmap, CHGR_BASE + 0x13, 0);
	if (!ret)
		ret = regmap_write(chip->regmap, CHGR_BASE + 0x14, 0xff);
	if (!ret)
		ret = regmap_write(chip->regmap, MISC_BASE + 0x11,
				   BIT(1) | BIT(6));
	if (!ret)
		ret = regmap_write(chip->regmap, MISC_BASE + 0x12,
				   BIT(1) | BIT(6));
	if (!ret)
		ret = regmap_write(chip->regmap, MISC_BASE + 0x13, 0);
	if (!ret)
		ret = regmap_write(chip->regmap, MISC_BASE + 0x14, 0xff);
	if (ret)
		return ret;
	if (client->irq <= 0)
		return -EINVAL;
	ret = devm_request_threaded_irq(dev, client->irq, NULL, smb1355_irq,
					IRQF_ONESHOT, dev_name(dev), chip);
	if (!ret)
		ret = regmap_write(chip->regmap, CHGR_BASE + 0x15, BIT(1));
	if (!ret)
		ret = regmap_write(chip->regmap, MISC_BASE + 0x15,
				   BIT(1) | BIT(6));
	if (ret)
		return ret;
	i2c_set_clientdata(client, chip);
	schedule_delayed_work(&chip->watchdog, 0);
	dev_info(
		dev,
		"SMB1355 initialized; parallel path disabled until primary grants budget\n");
	return 0;
}
static void smb1355_shutdown(struct i2c_client *client)
{
	smb1355_stop(i2c_get_clientdata(client));
}
static const struct of_device_id smb1355_of_match[] = {
	{ .compatible = "qcom,smb1355" },
	{}
};
MODULE_DEVICE_TABLE(of, smb1355_of_match);
static struct i2c_driver smb1355_driver = {
	.probe = smb1355_probe,
	.shutdown = smb1355_shutdown,
	.driver = { .name = "qcom-smb1355",
		    .of_match_table = smb1355_of_match },
};
module_i2c_driver(smb1355_driver);
MODULE_DESCRIPTION("Qualcomm SMB1355 parallel charger");
MODULE_LICENSE("GPL");
