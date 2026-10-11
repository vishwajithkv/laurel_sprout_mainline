// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2025, Marc Lainez <marc.lainez@gmail.com>
 * SDAM/SOC policy reference: Copyright (c) 2018-2020, The Linux Foundation.
 * Full-charge hold policy reference: Copyright (C) 2020 XiaoMi, Inc.
 *
 * Qualcomm PMI632 QGauge (QG) fuel gauge driver.
 *
 * The QG hardware block provides battery voltage and current measurement
 * via FIFO-based sampling, open circuit voltage (OCV) measurement during
 * sleep states, and coulomb counting through V/I accumulators.
 *
 * This driver exposes battery state via the power_supply subsystem.
 * IRQ-latched current samples are integrated in-kernel. The versioned
 * measurement interface lets an open Android/recovery service estimate SOC;
 * stock OCV tables provide an initial fallback before that service starts.
 */

#include <linux/bitops.h>
#include <linux/compat.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/limits.h>
#include <linux/math64.h>
#include <linux/log2.h>
#include <linux/iio/consumer.h>
#include <linux/interrupt.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/nvmem-consumer.h>
#include <linux/platform_device.h>
#include <linux/power_supply.h>
#include <linux/property.h>
#include <linux/regmap.h>
#include <linux/random.h>
#include <linux/rtc.h>
#include <linux/unaligned.h>
#include <linux/slab.h>
#include <linux/timekeeping.h>
#include <linux/uaccess.h>
#include <uapi/linux/qcom_qg.h>

/* Peripheral type register */
#define QG_PERPH_TYPE			0x04
#define QG_PERPH_SUBTYPE		0x05
#define QG_SUBTYPE_5A			0x03
#define QG_SUBTYPE_10A			0x04
#define QG_PERPH_TYPE_VALUE		0x0D

/* Status registers */
#define QG_STATUS1			0x08
#define  QG_STATUS1_BATT_PRESENT	BIT(0)
#define  QG_STATUS1_QG_OK		BIT(7)

#define QG_STATUS2			0x09
#define  QG_STATUS2_GOOD_OCV		BIT(1)

#define QG_STATUS3			0x0A
#define  QG_STATUS3_FIFO_RT_COUNT	GENMASK(3, 0)

/* Data control */
#define QG_DATA_CTL1			0x41
#define  QG_DATA_CTL1_MASTER_HOLD	BIT(0)

#define QG_DATA_CTL2			0x42
#define  QG_DATA_CTL2_BURST_AVG_HOLD	BIT(0)

/* FIFO / measurement control */
#define QG_S2_NORMAL_MEAS_CTL2		0x51
#define  QG_S2_FIFO_LENGTH_MASK		GENMASK(5, 3)
#define  QG_S2_FIFO_LENGTH_SHIFT	3
#define  QG_S2_NUM_ACCUM_MASK		GENMASK(2, 0)
#define QG_S2_NORMAL_MEAS_CTL3		0x52
#define QG_V_ACCUM			0x88
#define QG_I_ACCUM			0x8b
#define QG_ACCUM_COUNT			0x8e

/* OCV data registers (16-bit little-endian) */
#define QG_S7_PON_OCV_V		0x70
#define QG_S3_GOOD_OCV_V		0x74

/* FIFO data registers */
#define QG_V_FIFO0			0x90
#define QG_I_FIFO0			0xA0

/* Last ADC data registers */
#define QG_LAST_ADC_V			0xC0
#define QG_LAST_ADC_I			0xC2
#define QG_LAST_BURST_AVG_I		0xC6

/* Sentinel value indicating FIFO entry is not yet written */
#define QG_FIFO_RESET_VAL		0x8000

/* Maximum hardware FIFO depth */
#define QG_MAX_FIFO_LENGTH		8

/*
 * Raw ADC to physical unit conversion.
 *   Voltage: raw * 194637 / 1000 = microvolts
 *   Current: raw * 152588 / 1000 = microamps (signed)
 */
#define QG_V_RAW_TO_UV(raw)	div_u64(194637ULL * (u64)(raw), 1000)

/*
 * OCV staleness timeout: if the last measured OCV is older than this
 * many seconds, we consider it stale and re-derive SOC only on demand.
 */
#define QG_OCV_STALE_SECS		180
/* Stock qg-soc.c uses a 20-second minimum for ordinary SOC steps. */
#define QG_CAPACITY_STEP_SECS		20
#define QG_ESTIMATE_TIMEOUT_MS		30000
#define QG_SDAM_MAGIC			0x12345678
#define QG_CYCLE_BUCKETS			8

struct pmi632_qg {
	struct device *dev;
	struct regmap *regmap;
	unsigned int base;
	unsigned int current_scale;

	struct power_supply *psy;
	struct power_supply_battery_info *batt_info;

	struct iio_channel *batt_therm;
	struct nvmem_cell *history_magic;
	struct nvmem_cell *history_cycles;
	struct nvmem_cell *history_capacity;
	struct nvmem_cell *soc_valid, *soc_state;
	bool restore_checked, soc_restored, full_held;
	u64 last_soc_save_ms;
	bool profile_known;

	struct mutex lock; /* protects register sequences and reported capacity */

	/* Cached measurements (updated from IRQs and on-demand reads) */
	unsigned int vbat_uv;
	int ibat_ua;
	unsigned int ocv_uv;
	time64_t ocv_time;
	bool batt_present;
	bool capacity_initialized;
	int reported_capacity;
	time64_t capacity_update_time;
	struct miscdevice misc;
	atomic_t writer;
	u64 generation, sequence, sample_ms, last_fifo_ms;
	s64 charge_nc;
	u32 gaps, profile_ohm;
	bool fifo_valid, sampling_ready, parallel_sense;
	struct qcom_qg_estimate estimate;
	u64 estimate_ms;
};

static bool qg_estimate_valid(struct pmi632_qg *qg)
{
	return qg->estimate_ms &&
		ktime_to_ms(ktime_get_boottime()) - qg->estimate_ms <=
		QG_ESTIMATE_TIMEOUT_MS;
}

/* --- Low-level register helpers --- */

static int qg_read16(struct pmi632_qg *qg, unsigned int reg, u16 *val)
{
	int ret;
	__le16 raw;

	ret = regmap_bulk_read(qg->regmap, qg->base + reg, &raw, sizeof(raw));
	if (ret)
		return ret;

	*val = le16_to_cpu(raw);
	return 0;
}

/**
 * qg_master_hold() - Assert/release the FIFO master hold.
 *
 * The master hold freezes the FIFO data registers so they can be read
 * coherently. The hardware requires a 0->1 transition to assert, and
 * writing 0 to release.
 */
static int qg_master_hold(struct pmi632_qg *qg, bool hold)
{
	int ret;

	/* Clear first (required for 0->1 transition to latch) */
	ret = regmap_update_bits(qg->regmap, qg->base + QG_DATA_CTL1,
				 QG_DATA_CTL1_MASTER_HOLD, 0);
	if (ret)
		return ret;

	if (hold)
		ret = regmap_update_bits(qg->regmap, qg->base + QG_DATA_CTL1,
					 QG_DATA_CTL1_MASTER_HOLD,
					 QG_DATA_CTL1_MASTER_HOLD);

	return ret;
}

/* --- Measurement reading functions --- */

static int qg_read_battery_present(struct pmi632_qg *qg, bool *present)
{
	unsigned int val;
	int ret;

	ret = regmap_read(qg->regmap, qg->base + QG_STATUS1, &val);
	if (ret)
		return ret;

	mutex_lock(&qg->lock);
	if (qg->batt_present != !!(val & QG_STATUS1_BATT_PRESENT)) {
		qg->generation = get_random_u64();
		qg->estimate_ms = 0;
		qg->charge_nc = 0;
		qg->sample_ms = 0;
		qg->last_fifo_ms = 0;
		qg->fifo_valid = false;
		qg->capacity_initialized = false;
		qg->soc_restored = qg->full_held = false;
		/* A physical removal must not restore the previous pack record. */
		if (qg->soc_valid) {
			u8 valid = 0;

			nvmem_cell_write(qg->soc_valid, &valid, sizeof(valid));
			qg->restore_checked = true;
		}
	}
	qg->batt_present = !!(val & QG_STATUS1_BATT_PRESENT);
	*present = qg->batt_present;
	mutex_unlock(&qg->lock);
	return 0;
}

static int qg_read_vbat(struct pmi632_qg *qg, unsigned int *vbat_uv)
{
	u16 raw;
	int ret;

	ret = qg_read16(qg, QG_LAST_ADC_V, &raw);
	if (ret)
		return ret;

	if (!raw || raw == U16_MAX || raw == QG_FIFO_RESET_VAL)
		return -ENODATA;
	*vbat_uv = QG_V_RAW_TO_UV(raw);
	return 0;
}

static int qg_read_ibat(struct pmi632_qg *qg, int *ibat_ua)
{
	u16 raw;
	int ret;

	/*
	 * Hold the burst average register to get a coherent reading,
	 * then release after reading.
	 */
	ret = regmap_update_bits(qg->regmap, qg->base + QG_DATA_CTL2,
				 QG_DATA_CTL2_BURST_AVG_HOLD,
				 QG_DATA_CTL2_BURST_AVG_HOLD);
	if (ret)
		return ret;

	ret = qg_read16(qg, QG_LAST_BURST_AVG_I, &raw);

	/* Always release the hold, even if the read failed */
	regmap_update_bits(qg->regmap, qg->base + QG_DATA_CTL2,
			   QG_DATA_CTL2_BURST_AVG_HOLD, 0);

	if (ret)
		return ret;

	/*
	 * Hardware convention: positive = discharge, negative = charge.
	 * Power supply class convention (ABI): positive = charge, negative = discharge.
	 * Negate to match the ABI.
	 */
	*ibat_ua = -div_s64((s64)qg->current_scale * sign_extend32(raw, 15), 1000);
	return 0;
}

static int qg_read_ocv(struct pmi632_qg *qg, unsigned int reg,
			unsigned int *ocv_uv)
{
	u16 raw;
	int ret;

	ret = qg_read16(qg, reg, &raw);
	if (ret)
		return ret;

	*ocv_uv = (!raw || raw == U16_MAX || raw == QG_FIFO_RESET_VAL) ?
		  0 : QG_V_RAW_TO_UV(raw);
	return 0;
}

static int qg_read_temperature(struct pmi632_qg *qg, int *temp_decidegc)
{
	int ret, val;

	if (!qg->batt_therm)
		return -ENODEV;

	ret = iio_read_channel_processed(qg->batt_therm, &val);
	if (ret < 0)
		return ret;

	/* SPMI ADC5 batt-therm returns millidegrees C; convert to decidegrees */
	*temp_decidegc = val / 100;

	return 0;
}

/**
 * qg_update_ocv() - Read the latest good OCV if available.
 *
 * Checks STATUS2 for the GOOD_OCV bit; if set, reads the S3 good OCV
 * register and updates the cached OCV with a timestamp.
 *
 * Follow stock STATUS2 read handling; do not write status registers.
 */
static int qg_update_ocv(struct pmi632_qg *qg)
{
	unsigned int status2;
	unsigned int ocv_uv;
	int ret;

	ret = regmap_read(qg->regmap, qg->base + QG_STATUS2, &status2);
	if (ret)
		return ret;


	if (!(status2 & QG_STATUS2_GOOD_OCV))
		return 0;

	ret = qg_read_ocv(qg, QG_S3_GOOD_OCV_V, &ocv_uv);
	if (ret)
		return ret;

	qg->ocv_uv = ocv_uv;
	qg->ocv_time = ktime_get_boottime_seconds();

	return 0;
}

/* Resolve status/health from the primary explicitly; the optional secondary
 * provides only external-sense state, never battery status.
 */
static int qg_primary_property(struct power_supply *psy,
		enum power_supply_property prop, union power_supply_propval *val)
{
	struct pmi632_qg *qg = power_supply_get_drvdata(psy);
	struct power_supply *primary;
	int ret;

	primary = power_supply_get_by_reference(dev_fwnode(qg->dev), "qcom,primary-charger");
	if (IS_ERR_OR_NULL(primary))
		return power_supply_get_property_from_supplier(psy, prop, val);
	ret = power_supply_get_property(primary, prop, val);
	power_supply_put(primary);
	return ret;
}

static void qg_restore_soc(struct pmi632_qg *qg);

static int qg_get_capacity(struct pmi632_qg *qg, int *capacity)
{
	int temp_decidegc;
	int ret;
	bool full, can_increase;
	time64_t now;
	union power_supply_propval status;
	union power_supply_propval health;

	qg_restore_soc(qg);
	mutex_lock(&qg->lock);
	if (qg_estimate_valid(qg)) {
		*capacity = DIV_ROUND_CLOSEST(qg->estimate.soc_basis_points, 100);
		mutex_unlock(&qg->lock);
		return 0;
	}
	if (qg->estimate_ms) {
		*capacity = qg->reported_capacity;
		mutex_unlock(&qg->lock);
		return 0;
	}
	mutex_unlock(&qg->lock);

	if (!qg->batt_info)
		return -ENODATA;
	if (qg->soc_restored) {
		*capacity = qg->reported_capacity;
		return 0;
	}

	ret = qg_primary_property(qg->psy,
						   POWER_SUPPLY_PROP_STATUS, &status);
	full = !ret && status.intval == POWER_SUPPLY_STATUS_FULL;
	can_increase = !ret && (full ||
			status.intval == POWER_SUPPLY_STATUS_CHARGING);
	if (full) {
		ret = qg_primary_property(qg->psy,
					POWER_SUPPLY_PROP_HEALTH, &health);
		full = !ret && health.intval == POWER_SUPPLY_HEALTH_GOOD;
	}

	/* Use fresh resting OCV. A loaded-voltage fallback is an estimate,
	 * not the downstream userspace QG coulomb/ESR algorithm.
	 */
	ret = qg_read_temperature(qg, &temp_decidegc);
	if (ret)
		return ret;
	if (!qg->ocv_uv || ktime_get_boottime_seconds() - qg->ocv_time > QG_OCV_STALE_SECS) {
		ret = qg_read_vbat(qg, &qg->vbat_uv);
		if (ret)
			return ret;
		*capacity = power_supply_batinfo_ocv2cap(qg->batt_info,
						 qg->vbat_uv, temp_decidegc / 10);
	} else {
		*capacity = power_supply_batinfo_ocv2cap(qg->batt_info,
						 qg->ocv_uv, temp_decidegc / 10);
	}
	if (*capacity < 0)
		return *capacity;
	*capacity = clamp(*capacity, 0, 100);

	/* Seed from voltage even if the charger already reports termination.
	 * Bound later corrections, including promotion to full, independently
	 * of how often healthd reads this property. This stabilizes an estimate;
	 * it does not replace coulomb counting or capacity learning.
	 */
	now = ktime_get_boottime_seconds();
	mutex_lock(&qg->lock);
	if (!qg->capacity_initialized) {
		qg->reported_capacity = *capacity;
		qg->capacity_update_time = now;
		qg->capacity_initialized = true;
	} else {
		/* Stock only enters hold-full with good health and MSOC >= 99. */
		int target = full && qg->reported_capacity >= 99 ? 100 : *capacity;

		/* Stock does not increase SOC when input is absent/not charging. */
		if (target > qg->reported_capacity && !can_increase)
			target = qg->reported_capacity;

		/* Do not delay reporting a critically low voltage estimate. */
		if (target <= 1) {
			qg->reported_capacity = target;
			qg->capacity_update_time = now;
		} else if (target == qg->reported_capacity) {
			qg->capacity_update_time = now;
		} else if (now - qg->capacity_update_time >= QG_CAPACITY_STEP_SECS) {
			qg->reported_capacity += target > qg->reported_capacity ? 1 : -1;
			qg->capacity_update_time = now;
		}
	}
	*capacity = qg->reported_capacity;
	mutex_unlock(&qg->lock);

	return 0;
}

/* --- IRQ handlers --- */

/* Stock qpnp-qg.c:qg_process_fifo / qg-util.c sample timing. Read the
 * completed, IRQ-latched FIFO without MASTER_HOLD (which clears/restarts
 * measurement). Reject an entire batch on errors; never integrate property
 * reads or stretch a captured sample over a missing interval.
 */
static int qg_account_fifo(struct pmi632_qg *qg)
{
	unsigned int ctl, interval;
	u16 v, i;
	u64 now = ktime_to_ms(ktime_get_boottime()), dt, total;
	s64 delta = 0;
	u64 vsum = 0;
	int ret, n, j;

	mutex_lock(&qg->lock);
	if (!qg->sampling_ready) {
		ret = -EAGAIN;
		goto out;
	}
	ret = regmap_read(qg->regmap, qg->base + QG_S2_NORMAL_MEAS_CTL2, &ctl);
	if (ret)
		goto gap;
	ret = regmap_read(qg->regmap, qg->base + QG_S2_NORMAL_MEAS_CTL3, &interval);
	if (ret)
		goto gap;
	n = ((ctl & GENMASK(5, 3)) >> 3) + 1;
	dt = (1U << ((ctl & GENMASK(2, 0)) + 1)) * interval * 10ULL;
	total = dt * n;
	if (!dt || total > 3600000) {
		ret = -ERANGE;
		goto gap;
	}
	/* A repeated notification cannot account the same completed batch. */
	if (qg->last_fifo_ms && now - qg->last_fifo_ms < total / 2) {
		ret = -EAGAIN;
		goto out;
	}
	for (j = 0; j < n; j++) {
		ret = qg_read16(qg, QG_V_FIFO0 + j * 2, &v);
		if (ret)
			goto gap;
		ret = qg_read16(qg, QG_I_FIFO0 + j * 2, &i);
		if (ret)
			goto gap;
		if (!v || v == U16_MAX || v == QG_FIFO_RESET_VAL ||
		    i == QG_FIFO_RESET_VAL) {
			ret = -ENODATA;
			goto gap;
		}
		vsum += QG_V_RAW_TO_UV(v);
		delta += -div_s64((s64)qg->current_scale *
				 sign_extend32(i, 15), 1000) * (s64)dt;
	}
	if (!qg->last_fifo_ms ||
	    abs((s64)(now - qg->last_fifo_ms) - (s64)total) > total / 10)
		qg->gaps++;
	qg->charge_nc += delta;
	qg->sample_ms += total;
	qg->vbat_uv = div_u64(vsum, n);
	qg->fifo_valid = true;
	qg->sequence++;
	qg->last_fifo_ms = now;
	ret = 0;
	goto out;
gap:
	qg->gaps++;
	qg->fifo_valid = false;
	qg->sequence++;
	qg->last_fifo_ms = now;
out:
	mutex_unlock(&qg->lock);
	return ret;
}

static irqreturn_t qg_fifo_done_irq(int irq, void *data)
{
	struct pmi632_qg *qg = data;

	qg_account_fifo(qg);
	power_supply_changed(qg->psy);

	return IRQ_HANDLED;
}

static irqreturn_t qg_good_ocv_irq(int irq, void *data)
{
	struct pmi632_qg *qg = data;

	mutex_lock(&qg->lock);
	qg_update_ocv(qg);
	mutex_unlock(&qg->lock);

	power_supply_changed(qg->psy);

	return IRQ_HANDLED;
}

static irqreturn_t qg_batt_missing_irq(int irq, void *data)
{
	struct pmi632_qg *qg = data;
	bool present;

	qg_read_battery_present(qg, &present);

	power_supply_changed(qg->psy);

	return IRQ_HANDLED;
}

/* --- Power supply interface --- */

/* Stock qg-reg.h/qpnp-qg.c/fg-alg.c SDAM format. Read-only: do not
 * manufacture learned capacity or increment counters from estimated SOC.
 */
static int qg_read_history_cell(struct nvmem_cell *cell, void *value,
			       size_t expected)
{
	size_t len;
	void *data;
	int ret = 0;

	data = nvmem_cell_read(cell, &len);
	if (IS_ERR(data))
		return PTR_ERR(data);
	if (len != expected)
		ret = -EINVAL;
	else
		memcpy(value, data, len);
	kfree(data);
	return ret;
}

static int qg_get_history(struct pmi632_qg *qg,
			  enum power_supply_property prop, int *value)
{
	__le32 magic;
	__le16 buckets[QG_CYCLE_BUCKETS], capacity;
	int ret, i, sum = 0, learned, design;

	if (!qg->batt_present || !qg->profile_known || !qg->history_magic)
		return -ENODATA;
	ret = qg_read_history_cell(qg->history_magic, &magic, sizeof(magic));
	if (ret)
		return ret;
	if (le32_to_cpu(magic) != QG_SDAM_MAGIC)
		return -ENODATA;

	if (prop == POWER_SUPPLY_PROP_CYCLE_COUNT) {
		ret = qg_read_history_cell(qg->history_cycles, buckets, sizeof(buckets));
		if (ret)
			return ret;
		for (i = 0; i < QG_CYCLE_BUCKETS; i++) {
			if (le16_to_cpu(buckets[i]) == U16_MAX)
				return -ENODATA;
			sum += le16_to_cpu(buckets[i]);
		}
		*value = sum / QG_CYCLE_BUCKETS;
		return 0;
	}

	if (!qg->batt_info)
		return -ENODATA;
	ret = qg_read_history_cell(qg->history_capacity, &capacity, sizeof(capacity));
	if (ret)
		return ret;
	/* Stock stores signed mAh and sanity-checks within 50% of nominal.
	 * Reject invalid history instead of substituting nominal as stock does.
	 */
	learned = (s16)le16_to_cpu(capacity) * 1000;
	design = qg->batt_info->charge_full_design_uah;
	if (learned <= 0 || design <= 0 || learned < design / 2 ||
	    learned > design + design / 2)
		return -ENODATA;
	*value = learned;
	return 0;
}

static int qg_write_soc_cell(struct nvmem_cell *cell, void *value, size_t len)
{
	int ret = nvmem_cell_write(cell, value, len);

	/* Unlike regmap, the NVMEM cell API returns the byte count on success. */
	return ret < 0 ? ret : (ret == len ? 0 : -EIO);
}

static int qg_init_history(struct pmi632_qg *qg)
{
	/* Older DTBs keep battery telemetry; history is optional. */
	if (!device_property_present(qg->dev, "nvmem-cells"))
		return 0;
	qg->history_magic = devm_nvmem_cell_get(qg->dev, "history-magic");
	if (IS_ERR(qg->history_magic))
		return PTR_ERR(qg->history_magic);
	qg->history_cycles = devm_nvmem_cell_get(qg->dev, "cycle-count-buckets");
	if (IS_ERR(qg->history_cycles))
		return PTR_ERR(qg->history_cycles);
	qg->history_capacity = devm_nvmem_cell_get(qg->dev, "learned-capacity");
	if (IS_ERR(qg->history_capacity))
		return PTR_ERR(qg->history_capacity);
	if (device_property_match_string(qg->dev, "nvmem-cell-names", "soc-state") >= 0) {
		qg->soc_valid = devm_nvmem_cell_get(qg->dev, "soc-valid");
		if (IS_ERR(qg->soc_valid))
			return PTR_ERR(qg->soc_valid);
		qg->soc_state = devm_nvmem_cell_get(qg->dev, "soc-state");
		if (IS_ERR(qg->soc_state))
			return PTR_ERR(qg->soc_state);
	}
	return 0;
}


/* Adapted from LineageOS sm6125 qpnp-qg/qg-sdam: use PMIC elapsed time,
 * not Android wall time. The calendar need not have been set by Android.
 * Only shutdown fields 0x46..0x57 are writable here; preserve learned history.
 */
static int qg_rtc_seconds(u32 *seconds)
{
	struct rtc_device *rtc;
	struct rtc_time tm;
	time64_t now;
	int ret;

	rtc = rtc_class_open("rtc0");
	if (!rtc)
		return -ENODEV;
	ret = rtc_read_time(rtc, &tm);
	rtc_class_close(rtc);
	if (ret)
		return ret;
	now = rtc_tm_to_time64(&tm);
	if (now < 0 || now > U32_MAX)
		return -ERANGE;
	*seconds = now;
	return 0;
}

static void qg_restore_soc(struct pmi632_qg *qg)
{
	u8 valid, state[17];
	__le32 magic;
	u32 now, saved_time, ocv;
	int temp, saved_temp, pon_soc, ret;

	if (!qg->soc_state || !qg->batt_info || !qg->profile_known)
		return;
	mutex_lock(&qg->lock);
	if (qg->restore_checked || qg->estimate_ms || !qg->batt_present)
		goto out;
	/* Retry if RTC probes later; never consume a record without its clock. */
	ret = qg_rtc_seconds(&now);
	if (ret)
		goto out;
	qg->restore_checked = true;
	if (qg_read_history_cell(qg->history_magic, &magic, sizeof(magic)) ||
	    le32_to_cpu(magic) != QG_SDAM_MAGIC ||
	    qg_read_history_cell(qg->soc_valid, &valid, sizeof(valid)) || valid != 1 ||
	    qg_read_history_cell(qg->soc_state, state, sizeof(state)) ||
	    qg_read_temperature(qg, &temp))
		goto rejected;
	/* Offsets here are relative to the SOC byte, SDAM 0x47. */
	saved_temp = (s16)get_unaligned_le16(state + 1);
	ocv = get_unaligned_le32(state + 5);
	saved_time = get_unaligned_le32(state + 13);
	pon_soc = power_supply_batinfo_ocv2cap(qg->batt_info, qg->ocv_uv, temp / 10);
	/* More conservative than Laurel's downstream 40-point mismatch bound.
	 * Zero percent is valid; an RTC reset or interrupted write is not.
	 */
	if (state[0] > 100 || !saved_time || now < saved_time || now - saved_time > 2592000 ||
	    abs(temp - saved_temp) > 100 || ocv < 2500000 || ocv > 4500000 ||
	    pon_soc < 0 || abs(pon_soc - state[0]) > 10)
		goto rejected;
	ret = regmap_write(qg->regmap, qg->base + 0xbf,
			   DIV_ROUND_CLOSEST(state[0] * 255, 100));
	if (ret)
		goto rejected;
	qg->reported_capacity = state[0];
	qg->capacity_initialized = qg->soc_restored = true;
	qg->capacity_update_time = ktime_get_boottime_seconds();
	dev_info(qg->dev, "Restored SDAM SOC %u%%, age %u seconds\n",
		 state[0], now - saved_time);
	goto out;
rejected:
	dev_info(qg->dev, "SDAM SOC rejected; using measured PON OCV\n");
out:
	mutex_unlock(&qg->lock);
}

/* Called under the gauge lock. Invalidate first and publish validity last,
 * so a reset during the payload write cannot restore a torn record.
 */
static int qg_save_soc(struct pmi632_qg *qg)
{
	u8 valid = 0, state[17];
	__le32 magic;
	u32 now, voltage;
	int temp, ibat_ua, ret;

	if (!qg->soc_state || !qg->profile_known || !qg->batt_present ||
	    !qg_estimate_valid(qg))
		return -ENODATA;
	ret = qg_rtc_seconds(&now);
	if (ret)
		return ret;
	ret = qg_read_history_cell(qg->history_magic, &magic, sizeof(magic));
	if (ret || le32_to_cpu(magic) != QG_SDAM_MAGIC)
		return ret ?: -ENODATA;
	/* Preserve the resistance field, which this driver does not learn. */
	ret = qg_read_history_cell(qg->soc_state, state, sizeof(state));
	if (ret)
		return ret;
	ret = qg_read_temperature(qg, &temp);
	if (!ret)
		ret = qg_read_vbat(qg, &voltage);
	if (!ret)
		ret = qg_read_ibat(qg, &ibat_ua);
	if (ret)
		return ret;
	state[0] = clamp(DIV_ROUND_CLOSEST(qg->estimate.soc_basis_points, 100), 0, 100);
	put_unaligned_le16((u16)temp, state + 1);
	if (voltage < 2500000 || voltage > 4500000 ||
	    qg->ocv_uv < 2500000 || qg->ocv_uv > 4500000)
		return -ERANGE;
	/* Keep a hardware OCV in the stock OCV field, never loaded VBAT. */
	put_unaligned_le32(qg->ocv_uv, state + 5);
	put_unaligned_le32((u32)-ibat_ua, state + 9); /* downstream current sign */
	put_unaligned_le32(now, state + 13);
	ret = qg_write_soc_cell(qg->soc_valid, &valid, sizeof(valid));
	if (!ret)
		ret = qg_write_soc_cell(qg->soc_state, state, sizeof(state));
	if (!ret) {
		valid = 1;
		ret = qg_write_soc_cell(qg->soc_valid, &valid, sizeof(valid));
	}
	if (!ret)
		qg->last_soc_save_ms = ktime_to_ms(ktime_get_boottime());
	return ret;
}


/* PMI632 has fixed QG=0x4800 and CHGR=0x1000 offsets on the same SID.
 * Only read charger registers here; the SOC service cannot set charging
 * voltage, current, thermal policy or register addresses.
 */
static int qg_snapshot(struct pmi632_qg *qg, struct qcom_qg_snapshot *s)
{
	union power_supply_propval val;
	unsigned int voltage, state, enabled, fv;
	int ret;
	bool present;

	ret = qg_read_battery_present(qg, &present);
	if (ret || !present)
		return ret ?: -ENODEV;
	memset(s, 0, sizeof(*s));
	s->version = QCOM_QG_ABI_VERSION;
	s->accepted_soc_basis_points = -ENODATA;
	s->saved_full_uah = -ENODATA;
	s->saved_cycles = -ENODATA;
	qg_restore_soc(qg);
	ret = qg_read_temperature(qg, &s->temperature_decic);
	if (ret)
		return ret;
	ret = qg_primary_property(qg->psy,
		POWER_SUPPLY_PROP_STATUS, &val);
	if (ret)
		return ret;
	s->status = val.intval;
	ret = qg_primary_property(qg->psy,
		POWER_SUPPLY_PROP_HEALTH, &val);
	if (ret)
		return ret;
	s->health = val.intval;
	ret = qg_primary_property(qg->psy,
		POWER_SUPPLY_PROP_ONLINE, &val);
	if (ret)
		return ret;
	s->input_online = val.intval;
	ret = qg_primary_property(qg->psy,
		POWER_SUPPLY_PROP_CURRENT_MAX, &val);
	if (ret)
		return ret;
	s->input_limit_ua = val.intval;
	qg_get_history(qg, POWER_SUPPLY_PROP_CHARGE_FULL, &s->saved_full_uah);
	qg_get_history(qg, POWER_SUPPLY_PROP_CYCLE_COUNT, &s->saved_cycles);
	if (qg->base != 0x4800)
		return -ENODEV;
	ret = regmap_read(qg->regmap, 0x1006, &state);
	if (ret)
		return ret;
	ret = regmap_read(qg->regmap, 0x1042, &enabled);
	if (ret)
		return ret;
	ret = regmap_read(qg->regmap, 0x1070, &fv);
	if (ret)
		return ret;
	s->charger_state = state & GENMASK(2, 0);
	s->charger_enabled = enabled & BIT(0);
	s->float_uv = 3600000 + fv * 10000;
	mutex_lock(&qg->lock);
	ret = qg_read_vbat(qg, &voltage);
	if (!ret)
		ret = qg_read_ibat(qg, &s->current_ua);
	if (!ret) {
		s->voltage_uv = voltage;
		s->timestamp_ms = ktime_to_ms(ktime_get_boottime());
		s->generation = qg->generation;
		s->sequence = qg->sequence;
		s->charge_nc = qg->charge_nc;
		s->sample_ms = qg->sample_ms;
		s->gaps = qg->gaps;
		s->profile_ohm = qg->profile_ohm;
		if (qg->estimate_ms)
			s->accepted_soc_basis_points = qg->estimate.soc_basis_points;
		else if (qg->soc_restored)
			s->accepted_soc_basis_points = qg->reported_capacity * 100;
		if (qg->soc_restored)
			s->flags |= QCOM_QG_SOC_RESTORED;
		if (qg->full_held)
			s->flags |= QCOM_QG_FULL_HELD;
		s->design_uah = qg->batt_info->charge_full_design_uah;
		if (qg->batt_present)
			s->flags |= QCOM_QG_PRESENT;
		if (qg->fifo_valid)
			s->flags |= QCOM_QG_FIFO_VALID;
		if (qg->ocv_uv && ktime_get_boottime_seconds() - qg->ocv_time <= QG_OCV_STALE_SECS) {
			s->flags |= QCOM_QG_OCV_VALID;
			s->ocv_uv = qg->ocv_uv;
		}
		if (qg_estimate_valid(qg))
			s->flags |= QCOM_QG_ESTIMATE_VALID;
	}
	mutex_unlock(&qg->lock);
	return ret;
}

static int qg_misc_open(struct inode *inode, struct file *file)
{
	struct miscdevice *misc = file->private_data;
	struct pmi632_qg *qg = container_of(misc, struct pmi632_qg, misc);

	if ((file->f_mode & FMODE_WRITE) && atomic_cmpxchg(&qg->writer, 0, 1))
		return -EBUSY;
	file->private_data = qg;
	return nonseekable_open(inode, file);
}

static int qg_misc_release(struct inode *inode, struct file *file)
{
	struct pmi632_qg *qg = file->private_data;

	if (file->f_mode & FMODE_WRITE)
		atomic_set(&qg->writer, 0);
	return 0;
}

static long qg_misc_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct pmi632_qg *qg = file->private_data;
	struct qcom_qg_snapshot snapshot;
	struct qcom_qg_estimate estimate;
	u64 now;
	int ret, i, saved_cycles;
	bool save_soc;

	if (cmd == QCOM_QG_GET_SNAPSHOT) {
		ret = qg_snapshot(qg, &snapshot);
		if (ret)
			return ret;
		return copy_to_user((void __user *)arg, &snapshot, sizeof(snapshot)) ? -EFAULT : 0;
	}
	if (cmd != QCOM_QG_SUBMIT_ESTIMATE)
		return -ENOTTY;
	if (!(file->f_mode & FMODE_WRITE))
		return -EPERM;
	if (copy_from_user(&estimate, (void __user *)arg, sizeof(estimate)))
		return -EFAULT;
	if (estimate.version != QCOM_QG_ABI_VERSION ||
	    estimate.flags & ~(QCOM_QG_FULL_QUALIFIED | QCOM_QG_LEARNED_VALID) ||
	    estimate.soc_basis_points < 0 || estimate.soc_basis_points > 10000 ||
	    estimate.cycles < 0 || estimate.full_uah < qg->batt_info->charge_full_design_uah / 2 ||
	    estimate.full_uah > qg->batt_info->charge_full_design_uah * 3 / 2)
		return -EINVAL;
	for (i = 0; i < ARRAY_SIZE(estimate.reserved); i++)
		if (estimate.reserved[i])
			return -EINVAL;
	if (estimate.flags & QCOM_QG_LEARNED_VALID) {
		if (!qg->profile_known)
			return -ENODATA;
		if (!qg_get_history(qg, POWER_SUPPLY_PROP_CYCLE_COUNT, &saved_cycles) &&
		    estimate.cycles < saved_cycles)
			return -EINVAL;
	}
	if (estimate.flags & QCOM_QG_FULL_QUALIFIED) {
		ret = qg_snapshot(qg, &snapshot);
		if (ret)
			return ret;
		if (estimate.soc_basis_points != 10000 ||
		    (snapshot.charger_state != 5 &&
		     !((snapshot.flags & QCOM_QG_FULL_HELD) &&
		       (snapshot.charger_state == 3 || snapshot.charger_state == 4) &&
		       snapshot.status == POWER_SUPPLY_STATUS_CHARGING)) ||
		    !snapshot.input_online || !snapshot.charger_enabled ||
		    snapshot.health != POWER_SUPPLY_HEALTH_GOOD || snapshot.float_uv < 4390000 ||
		    snapshot.voltage_uv < snapshot.float_uv - 100000 || abs(snapshot.current_ua) > 330000 ||
		    snapshot.temperature_decic < 100 || snapshot.temperature_decic > 440)
			return -EINVAL;
	}
	mutex_lock(&qg->lock);
	now = ktime_to_ms(ktime_get_boottime());
	if (!qg->batt_present || estimate.generation != qg->generation ||
	    estimate.sequence != qg->sequence) {
		ret = -ESTALE;
		goto unlock;
	}
	if (qg->estimate_ms && estimate.cycles < qg->estimate.cycles) {
		ret = -EINVAL;
		goto unlock;
	}
	/* Cap normal UI movement at one percentage point per 20 seconds.
	 * Heartbeats renew freshness without changing this independent clock.
	 */
	if (qg->estimate_ms && abs(estimate.soc_basis_points - qg->estimate.soc_basis_points) >
	    ((now / 1000 - qg->capacity_update_time) / QG_CAPACITY_STEP_SECS) * 100) {
		ret = -ERANGE;
		goto unlock;
	}
	ret = regmap_write(qg->regmap, qg->base + 0xbf,
		DIV_ROUND_CLOSEST(estimate.soc_basis_points * 255, 10000));
	if (ret)
		goto unlock;
	if (!qg->estimate_ms || estimate.soc_basis_points != qg->estimate.soc_basis_points)
		qg->capacity_update_time = now / 1000;
	save_soc = qg->full_held != !!(estimate.flags & QCOM_QG_FULL_QUALIFIED) ||
		   qg->reported_capacity != DIV_ROUND_CLOSEST(estimate.soc_basis_points, 100);
	qg->estimate = estimate;
	qg->estimate_ms = now;
	qg->full_held = !!(estimate.flags & QCOM_QG_FULL_QUALIFIED);
	qg->reported_capacity = DIV_ROUND_CLOSEST(estimate.soc_basis_points, 100);
	qg->capacity_initialized = true;
	if (save_soc || !qg->last_soc_save_ms || now - qg->last_soc_save_ms >= 60000) {
		int save_ret = qg_save_soc(qg);

		if (save_ret)
			dev_warn_ratelimited(qg->dev, "Cannot save SDAM SOC: %d\n", save_ret);
	}
	ret = 0;
unlock:
	mutex_unlock(&qg->lock);
	if (!ret)
		power_supply_changed(qg->psy);
	return ret;
}

static const struct file_operations qg_misc_fops = {
	.owner = THIS_MODULE,
	.open = qg_misc_open,
	.release = qg_misc_release,
	.unlocked_ioctl = qg_misc_ioctl,
	.compat_ioctl = compat_ptr_ioctl,
	/* A NULL llseek plus nonseekable_open rejects seeks with -ESPIPE. */
};

static void qg_misc_deregister(void *data)
{
	misc_deregister(data);
}

static int qg_get_property(struct power_supply *psy,
			   enum power_supply_property psp,
			   union power_supply_propval *val)
{
	struct pmi632_qg *qg = power_supply_get_drvdata(psy);
	struct qcom_qg_snapshot snapshot;
	int ret, temp;

	switch (psp) {
	case POWER_SUPPLY_PROP_CALIBRATE:
		mutex_lock(&qg->lock);
		/* A live SOC producer, not an assertion of laboratory accuracy. */
		val->intval = qg_estimate_valid(qg) && qg->profile_known &&
			(qg->fifo_valid || (qg->ocv_uv &&
			ktime_get_boottime_seconds() - qg->ocv_time <= QG_OCV_STALE_SECS));
		mutex_unlock(&qg->lock);
		return 0;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		mutex_lock(&qg->lock);
		/* Android expects remaining charge, not the signed boot delta.
		 * The measurement ioctl retains the raw nC counter for integration.
		 */
		ret = qg_estimate_valid(qg) ? 0 : -ENODATA;
		val->intval = ret ? 0 : div_s64((s64)qg->estimate.full_uah *
				qg->estimate.soc_basis_points, 10000);
		mutex_unlock(&qg->lock);
		return ret;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		mutex_lock(&qg->lock);
		if (qg->estimate_ms && (qg->estimate.flags & QCOM_QG_LEARNED_VALID)) {
			val->intval = psp == POWER_SUPPLY_PROP_CHARGE_FULL ?
				qg->estimate.full_uah : qg->estimate.cycles;
			mutex_unlock(&qg->lock);
			return 0;
		}
		mutex_unlock(&qg->lock);
		return qg_get_history(qg, psp, &val->intval);
	case POWER_SUPPLY_PROP_PRESENT:
		ret = qg_read_battery_present(qg, &qg->batt_present);
		val->intval = qg->batt_present;
		return ret;

	case POWER_SUPPLY_PROP_STATUS:
		if (!qg->batt_present) {
			val->intval = POWER_SUPPLY_STATUS_UNKNOWN;
			return 0;
		}
		/* Physical pauses/faults come from the primary and are never masked.
		 * Laurel downstream keeps a completed-cycle presentation through
		 * short TERM/TAPER transitions; qualify it with measured health.
		 */
		ret = qg_primary_property(psy, psp, val);
		if (ret || (val->intval != POWER_SUPPLY_STATUS_FULL &&
			    val->intval != POWER_SUPPLY_STATUS_CHARGING))
			return ret;
		ret = qg_snapshot(qg, &snapshot);
		if (ret)
			return ret;
		mutex_lock(&qg->lock);
		if (snapshot.input_online && snapshot.charger_enabled &&
		    snapshot.health == POWER_SUPPLY_HEALTH_GOOD &&
		    snapshot.temperature_decic >= 100 && snapshot.temperature_decic <= 440 &&
		    snapshot.float_uv >= 4390000 &&
		    snapshot.voltage_uv >= snapshot.float_uv - 100000 &&
		    abs(snapshot.current_ua) <= 330000) {
			if (qg_estimate_valid(qg) && qg->full_held &&
			    qg->estimate.soc_basis_points == 10000)
				val->intval = POWER_SUPPLY_STATUS_FULL;
			else if (val->intval == POWER_SUPPLY_STATUS_FULL)
				/* Downstream Laurel's pending charge-done presentation.
				 * This is cycle status, not a positive-current assertion.
				 */
				val->intval = snapshot.accepted_soc_basis_points >= 9900 ?
					POWER_SUPPLY_STATUS_CHARGING : POWER_SUPPLY_STATUS_NOT_CHARGING;
		} else if (val->intval == POWER_SUPPLY_STATUS_FULL) {
			val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
		}
		mutex_unlock(&qg->lock);
		return 0;

	case POWER_SUPPLY_PROP_HEALTH:
		if (!qg->batt_present) {
			val->intval = POWER_SUPPLY_HEALTH_UNKNOWN;
			return 0;
		}
		ret = qg_read_temperature(qg, &temp);
		if (ret)
			return ret;
		ret = qg_read_vbat(qg, &qg->vbat_uv);
		if (ret)
			return ret;
		if (temp <= 0)
			val->intval = POWER_SUPPLY_HEALTH_COLD;
		else if (temp >= 580)
			val->intval = POWER_SUPPLY_HEALTH_OVERHEAT;
		else if (qg->batt_info && qg->vbat_uv >
			 qg->batt_info->voltage_max_design_uv + 50000)
			val->intval = POWER_SUPPLY_HEALTH_OVERVOLTAGE;
		else
			val->intval = POWER_SUPPLY_HEALTH_GOOD;

		return 0;

	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		if (!qg->batt_present)
			return -ENODATA;
		/* Refresh from last ADC register */
		ret = qg_read_vbat(qg, &qg->vbat_uv);
		if (ret)
			return ret;
		val->intval = qg->vbat_uv;
		return 0;

	case POWER_SUPPLY_PROP_CURRENT_NOW:
		if (!qg->batt_present)
			return -ENODATA;
		mutex_lock(&qg->lock);
		ret = qg_read_ibat(qg, &qg->ibat_ua);
		mutex_unlock(&qg->lock);
		if (ret)
			return ret;
		val->intval = qg->ibat_ua;
		return 0;

	case POWER_SUPPLY_PROP_VOLTAGE_OCV:
		if (!qg->batt_present)
			return -ENODATA;
		if (ktime_get_boottime_seconds() - qg->ocv_time > QG_OCV_STALE_SECS)
			return -ENODATA;
		val->intval = qg->ocv_uv;
		return 0;

	case POWER_SUPPLY_PROP_CAPACITY_LEVEL:
		ret = qg_get_capacity(qg, &temp);
		if (ret)
			return ret;
		mutex_lock(&qg->lock);
		if (temp == 100 && qg_estimate_valid(qg) &&
		    (qg->estimate.flags & QCOM_QG_FULL_QUALIFIED))
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_FULL;
		else if (temp <= 5)
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_CRITICAL;
		else if (temp <= 20)
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_LOW;
		else if (temp >= 80)
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_HIGH;
		else
			val->intval = POWER_SUPPLY_CAPACITY_LEVEL_NORMAL;
		mutex_unlock(&qg->lock);
		return 0;
	case POWER_SUPPLY_PROP_CAPACITY:
		if (!qg->batt_present)
			return -ENODATA;
		ret = qg_get_capacity(qg, &val->intval);
		return ret;

	case POWER_SUPPLY_PROP_TEMP:
		if (!qg->batt_present)
			return -ENODATA;
		ret = qg_read_temperature(qg, &val->intval);
		return ret;

	default:
		return -EINVAL;
	}
}

static enum power_supply_property qg_properties[] = {
	POWER_SUPPLY_PROP_CALIBRATE,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_OCV,
	POWER_SUPPLY_PROP_CAPACITY_LEVEL,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_TEMP,
};

static int qg_capture_partial(struct pmi632_qg *qg, int parallel);

static void qg_external_power_changed(struct power_supply *psy)
{
	struct pmi632_qg *qg = power_supply_get_drvdata(psy);
	struct power_supply *parallel;
	union power_supply_propval online;
	int ret;

	parallel = power_supply_get_by_reference(dev_fwnode(qg->dev), "qcom,parallel-charger");
	if (!IS_ERR_OR_NULL(parallel)) {
		ret = power_supply_get_property(parallel, POWER_SUPPLY_PROP_ONLINE, &online);
		if (!ret && !!online.intval != READ_ONCE(qg->parallel_sense)) {
			ret = qg_capture_partial(qg, !!online.intval);
			if (ret)
				dev_warn_ratelimited(qg->dev, "Parallel sense update failed: %d\n", ret);
		}
		power_supply_put(parallel);
	}
	power_supply_changed(psy);
}

static const struct power_supply_desc qg_psy_desc = {
	.name = "qg-battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = qg_properties,
	.num_properties = ARRAY_SIZE(qg_properties),
	.get_property = qg_get_property,
	.external_power_changed = qg_external_power_changed,
};

/* Use supplier OCV data without changing power_supply core's owned data.
 * Battery limits/design capacity are common to all stock Laurel packs.
 */
static int qg_select_ocv_profile(struct pmi632_qg *qg)
{
	struct fwnode_handle *profiles, *profile;
	struct power_supply_battery_info *info;
	struct power_supply_battery_ocv_table *table;
	struct iio_channel *id_chan;
	u32 temps[POWER_SUPPLY_OCV_TEMP_MAX], nominal, *pairs;
	u64 resistance;
	int uv, ret, ntemp, count, i, j;
	char name[32];

	profiles = device_get_named_child_node(qg->dev, "battery-profiles");
	if (!profiles)
		return 0;
	id_chan = devm_iio_channel_get(qg->dev, "batt-id");
	if (IS_ERR(id_chan)) {
		ret = PTR_ERR(id_chan);
		goto put_profiles;
	}
	ret = iio_read_channel_processed(id_chan, &uv);
	if (ret < 0)
		goto put_profiles;
	/* 1.875 V ratiometric reference, physical 100 kohm pull-up. */
	if (uv <= 0 || uv >= 1874000) {
		ret = 0;
		goto put_profiles;
	}
	resistance = div_u64(100000ULL * uv, 1875000 - uv);
	fwnode_for_each_child_node(profiles, profile) {
		ret = fwnode_property_read_u32(profile, "reg", &nominal);
		if (ret || resistance < nominal * 8ULL / 10 ||
		    resistance > nominal * 12ULL / 10)
			continue;
		info = devm_kmemdup(qg->dev, qg->batt_info, sizeof(*info), GFP_KERNEL);
		if (!info) {
			ret = -ENOMEM;
			goto put_profile;
		}
		ntemp = fwnode_property_count_u32(profile, "ocv-capacity-celsius");
		if (ntemp <= 0 || ntemp > ARRAY_SIZE(temps)) {
			ret = -EINVAL;
			goto put_profile;
		}
		ret = fwnode_property_read_u32_array(profile, "ocv-capacity-celsius",
						   temps, ntemp);
		if (ret)
			goto put_profile;
		for (i = 0; i < ntemp; i++) {
			snprintf(name, sizeof(name), "ocv-capacity-table-%d", i);
			count = fwnode_property_count_u32(profile, name);
			if (count < 4 || count % 2) {
				ret = -EINVAL;
				goto put_profile;
			}
			pairs = devm_kcalloc(qg->dev, count, sizeof(*pairs), GFP_KERNEL);
			table = devm_kcalloc(qg->dev, count / 2, sizeof(*table), GFP_KERNEL);
			if (!pairs || !table) {
				ret = -ENOMEM;
				goto put_profile;
			}
			ret = fwnode_property_read_u32_array(profile, name, pairs, count);
			if (ret)
				goto put_profile;
			for (j = 0; j < count / 2; j++) {
				table[j].ocv = pairs[j * 2];
				table[j].capacity = pairs[j * 2 + 1];
			}
			info->ocv_temp[i] = (s32)temps[i];
			info->ocv_table[i] = table;
			info->ocv_table_size[i] = count / 2;
		}
		for (; i < ARRAY_SIZE(info->ocv_temp); i++) {
			info->ocv_table[i] = NULL;
			info->ocv_table_size[i] = 0;
		}
		qg->batt_info = info;
		qg->profile_known = true;
		qg->profile_ohm = nominal;
		dev_info(qg->dev, "Battery ID %llu ohm: using %u ohm stock OCV profile\n",
			 resistance, nominal);
		ret = 0;
		goto put_profile;
	}
	dev_info(qg->dev, "Battery ID %llu ohm: using stock fallback OCV profile\n",
		 resistance);
	ret = 0;
	goto put_profiles;
put_profile:
	fwnode_handle_put(profile);
put_profiles:
	fwnode_handle_put(profiles);
	return ret;
}

static int qg_request_irq(struct platform_device *pdev, const char *name,
			  irq_handler_t handler, struct pmi632_qg *qg)
{
	int irq = platform_get_irq_byname(pdev, name);

	if (irq < 0)
		return irq;
	return devm_request_threaded_irq(&pdev->dev, irq, NULL, handler,
					 IRQF_ONESHOT, name, qg);
}

/* --- Probe and driver registration --- */

static int pmi632_qg_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct pmi632_qg *qg;
	struct power_supply_config psy_cfg = {};
	unsigned int perph_type, subtype;
	int ret;
	u32 fifo_length = 4, acc_length = 128, interval_ms = 100;
	unsigned int ctl, interval;
	int release_ret;

	qg = devm_kzalloc(dev, sizeof(*qg), GFP_KERNEL);
	if (!qg)
		return -ENOMEM;

	qg->dev = dev;
	mutex_init(&qg->lock);
	atomic_set(&qg->writer, 0);
	qg->generation = get_random_u64();

	qg->regmap = dev_get_regmap(dev->parent, NULL);
	if (!qg->regmap)
		return dev_err_probe(dev, -ENODEV,
				     "Failed to get parent regmap\n");

	ret = device_property_read_u32(dev, "reg", &qg->base);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to read reg property\n");

	/* Verify peripheral type */
	ret = regmap_read(qg->regmap, qg->base + QG_PERPH_TYPE, &perph_type);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to read peripheral type\n");

	if (perph_type != QG_PERPH_TYPE_VALUE)
		return dev_err_probe(dev, -ENODEV,
				     "Unexpected peripheral type: 0x%02x\n",
				     perph_type);

	ret = regmap_read(qg->regmap, qg->base + QG_PERPH_SUBTYPE, &subtype);
	if (ret)
		return ret;
	if (subtype == QG_SUBTYPE_5A)
		qg->current_scale = 152588;
	else if (subtype == QG_SUBTYPE_10A)
		qg->current_scale = 305176;
	else
		return dev_err_probe(dev, -ENODEV, "Unknown QG subtype %u\n", subtype);

	/* Read initial battery presence */
	ret = qg_read_battery_present(qg, &qg->batt_present);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to read battery presence\n");

	/* Read PON (power-on) OCV as initial estimate */
	ret = qg_read_ocv(qg, QG_S7_PON_OCV_V, &qg->ocv_uv);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to read PON OCV\n");

	qg->ocv_time = ktime_get_boottime_seconds();

	/* Also read initial voltage */
	ret = qg_read_vbat(qg, &qg->vbat_uv);
	if (ret)
		return dev_err_probe(dev, ret,
				     "Failed to read battery voltage\n");

	qg->batt_therm = devm_iio_channel_get(dev, "batt-therm");
	if (IS_ERR(qg->batt_therm))
		return dev_err_probe(dev, PTR_ERR(qg->batt_therm),
				     "Missing battery thermistor\n");

	/* Register power supply */
	ret = qg_init_history(qg);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get battery history cells\n");

	psy_cfg.drv_data = qg;
	psy_cfg.fwnode = dev_fwnode(dev);

	qg->psy = devm_power_supply_register(dev, &qg_psy_desc, &psy_cfg);
	if (IS_ERR(qg->psy))
		return dev_err_probe(dev, PTR_ERR(qg->psy),
				     "Failed to register power supply\n");

	/* Power supply core owns and releases the parsed battery data. */
	qg->batt_info = qg->psy->battery_info;
	if (!qg->batt_info)
		return dev_err_probe(dev, -EINVAL, "Missing battery data\n");

	ret = qg_select_ocv_profile(qg);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to select battery OCV profile\n");


	ret = qg_request_irq(pdev, "fifo-done", qg_fifo_done_irq, qg);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request FIFO IRQ\n");
	ret = qg_request_irq(pdev, "good-ocv", qg_good_ocv_irq, qg);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request OCV IRQ\n");
	ret = qg_request_irq(pdev, "batt-missing", qg_batt_missing_irq, qg);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request battery IRQ\n");
	ret = qg_request_irq(pdev, "vbat-low", qg_batt_missing_irq, qg);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request low-voltage IRQ\n");
	ret = qg_request_irq(pdev, "vbat-empty", qg_batt_missing_irq, qg);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request empty-voltage IRQ\n");

	/* Stock Laurel programs these at each boot; bootloader values are not
	 * a reliable sampling policy (build #29 inherited 307.2 s batches).
	 */
	device_property_read_u32(dev, "qcom,s2-fifo-length", &fifo_length);
	device_property_read_u32(dev, "qcom,s2-acc-length", &acc_length);
	device_property_read_u32(dev, "qcom,s2-acc-interval-ms", &interval_ms);
	if (!fifo_length || fifo_length > QG_MAX_FIFO_LENGTH ||
	    acc_length < 2 || acc_length > 256 || !is_power_of_2(acc_length) ||
	    interval_ms < 10 || interval_ms > 2550 || interval_ms % 10)
		return dev_err_probe(dev, -EINVAL, "Invalid S2 sampling parameters\n");
	mutex_lock(&qg->lock);
	ret = qg_master_hold(qg, true);
	if (!ret) {
		ret = regmap_update_bits(qg->regmap, qg->base + QG_S2_NORMAL_MEAS_CTL2,
			QG_S2_FIFO_LENGTH_MASK | QG_S2_NUM_ACCUM_MASK,
			((fifo_length - 1) << QG_S2_FIFO_LENGTH_SHIFT) | (ilog2(acc_length) - 1));
		if (!ret)
			ret = regmap_update_bits(qg->regmap, qg->base + 0x43, BIT(7), 0);
		if (!ret)
			ret = regmap_write(qg->regmap, qg->base + QG_S2_NORMAL_MEAS_CTL3,
					   interval_ms / 10);
		if (!ret)
			ret = regmap_read(qg->regmap, qg->base + QG_S2_NORMAL_MEAS_CTL2, &ctl);
		if (!ret)
			ret = regmap_read(qg->regmap, qg->base + QG_S2_NORMAL_MEAS_CTL3, &interval);
		if (!ret && (((ctl & QG_S2_FIFO_LENGTH_MASK) >> 3) + 1 != fifo_length ||
		    (1U << ((ctl & QG_S2_NUM_ACCUM_MASK) + 1)) != acc_length ||
		    interval * 10 != interval_ms))
			ret = -EIO;
		/* Always release hold, preserving the original configuration error. */
		release_ret = qg_master_hold(qg, false);
		if (!ret)
			ret = release_ret;
	}
	if (!ret) {
		qg->sampling_ready = true;
		qg->last_fifo_ms = ktime_to_ms(ktime_get_boottime());
	}
	mutex_unlock(&qg->lock);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to initialize QG sampling\n");
	dev_info(dev, "S2 FIFO %u x %u x %u ms = %u ms\n",
		 fifo_length, acc_length, interval_ms, fifo_length * acc_length * interval_ms);

	qg->misc.minor = MISC_DYNAMIC_MINOR;
	qg->misc.name = "qcom-qg";
	qg->misc.mode = 0600;
	qg->misc.fops = &qg_misc_fops;
	qg->misc.parent = dev;
	ret = misc_register(&qg->misc);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register QG measurement device\n");
	ret = devm_add_action_or_reset(dev, qg_misc_deregister, &qg->misc);
	if (ret)
		return ret;
	platform_set_drvdata(pdev, qg);

	dev_info(dev, "PMI632 QG: batt=%s OCV=%u uV VBAT=%u uV\n",
		 qg->batt_present ? "present" : "absent",
		 qg->ocv_uv, qg->vbat_uv);

	return 0;
}

/* Stock qg_process_rt_fifo/qg_process_accumulator: freeze the FSM before
 * consuming the partial FIFO, then restart it so an IRQ cannot account the
 * same samples again. Caller never substitutes elapsed time for ADC count.
 */
static int qg_capture_partial(struct pmi632_qg *qg, int parallel)
{
	unsigned int status, ctl, interval, count, acc_v;
	u8 acc[3];
	u16 v, i;
	u64 duration = 0, dt, now;
	s64 delta = 0, sum;
	int ret, release_ret, n, j;

	mutex_lock(&qg->lock);
	if (!qg->sampling_ready) {
		mutex_unlock(&qg->lock);
		return 0;
	}
	ret = qg_master_hold(qg, true);
	if (ret)
		goto out;
	ret = regmap_read(qg->regmap, qg->base + QG_STATUS3, &status);
	if (ret)
		goto release;
	n = status & QG_STATUS3_FIFO_RT_COUNT;
	if (n > QG_MAX_FIFO_LENGTH) {
		ret = -EIO;
		goto release;
	}
	ret = regmap_read(qg->regmap, qg->base + QG_S2_NORMAL_MEAS_CTL2, &ctl);
	if (ret)
		goto release;
	ret = regmap_read(qg->regmap, qg->base + QG_S2_NORMAL_MEAS_CTL3, &interval);
	if (ret)
		goto release;
	if (!interval) {
		ret = -EINVAL;
		goto release;
	}
	dt = (1U << ((ctl & QG_S2_NUM_ACCUM_MASK) + 1)) * interval * 10ULL;
	for (j = 0; j < n; j++) {
		ret = qg_read16(qg, QG_V_FIFO0 + j * 2, &v);
		if (ret)
			goto release;
		ret = qg_read16(qg, QG_I_FIFO0 + j * 2, &i);
		if (ret)
			goto release;
		if (!v || v == U16_MAX || v == QG_FIFO_RESET_VAL || i == QG_FIFO_RESET_VAL) {
			ret = -ENODATA;
			goto release;
		}
		delta -= div_s64((s64)qg->current_scale * sign_extend32(i, 15), 1000) * (s64)dt;
		duration += dt;
	}
	ret = regmap_read(qg->regmap, qg->base + QG_ACCUM_COUNT, &count);
	if (ret)
		goto release;
	/* Stock ignores fewer than ten conversions. Account no guessed time. */
	if (count >= 10) {
		ret = regmap_bulk_read(qg->regmap, qg->base + QG_V_ACCUM, acc, sizeof(acc));
		if (ret)
			goto release;
		acc_v = (acc[0] | acc[1] << 8 | acc[2] << 16) / count;
		if (!acc_v || acc_v >= U16_MAX || acc_v == QG_FIFO_RESET_VAL) {
			ret = -ENODATA;
			goto release;
		}
		ret = regmap_bulk_read(qg->regmap, qg->base + QG_I_ACCUM, acc, sizeof(acc));
		if (ret)
			goto release;
		sum = sign_extend64(acc[0] | acc[1] << 8 | acc[2] << 16, 23);
		delta -= div_s64(sum * qg->current_scale, 1000) * interval * 10;
		duration += count * interval * 10ULL;
	} else if (count) {
		qg->gaps++;
	}
	if (duration) {
		qg->charge_nc += delta;
		qg->sample_ms += duration;
		qg->fifo_valid = true;
		qg->sequence++;
	}
release:
	if (!ret && parallel >= 0) {
		ret = regmap_update_bits(qg->regmap, qg->base + 0x43, BIT(7), parallel ? BIT(7) : 0);
		if (!ret)
			qg->parallel_sense = parallel;
		/* Discard no unmeasured time into the coulomb model. The switch
		 * follows the supplier notification, not an exact ADC boundary.
		 */
		qg->gaps++;
	}
	release_ret = qg_master_hold(qg, false);
	if (!ret)
		ret = release_ret;
	if (ret)
		qg->gaps++;
	now = ktime_to_ms(ktime_get_boottime());
	qg->last_fifo_ms = now;
out:
	mutex_unlock(&qg->lock);
	if (!ret)
		power_supply_changed(qg->psy);
	return ret;
}

static int pmi632_qg_suspend(struct device *dev)
{
	struct pmi632_qg *qg = dev_get_drvdata(dev);

	int ret = qg_capture_partial(qg, -1);

	if (ret)
		dev_warn(qg->dev, "Partial QG capture failed: %d\n", ret);
	/* A telemetry error must not prevent system suspend. */
	return 0;
}

static int pmi632_qg_resume(struct device *dev)
{
	struct pmi632_qg *qg = dev_get_drvdata(dev);

	/*
	 * After resume, the hardware may have measured a good OCV during
	 * the sleep state. Check for it and update our cached value.
	 * Also refresh the battery voltage.
	 */
	mutex_lock(&qg->lock);
	qg_update_ocv(qg);
	mutex_unlock(&qg->lock);

	qg_read_vbat(qg, &qg->vbat_uv);

	dev_dbg(qg->dev, "Resumed, OCV=%u uV VBAT=%u uV\n",
		qg->ocv_uv, qg->vbat_uv);

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(pmi632_qg_pm_ops,
				pmi632_qg_suspend, pmi632_qg_resume);

static const struct of_device_id pmi632_qg_of_match[] = {
	{ .compatible = "qcom,pmi632-qg" },
	{}
};
MODULE_DEVICE_TABLE(of, pmi632_qg_of_match);

static void pmi632_qg_shutdown(struct platform_device *pdev)
{
	struct pmi632_qg *qg = platform_get_drvdata(pdev);
	int ret;

	mutex_lock(&qg->lock);
	ret = qg_save_soc(qg);
	mutex_unlock(&qg->lock);
	if (ret && ret != -ENODATA)
		dev_warn(qg->dev, "Shutdown SOC save failed: %d\n", ret);
}

static struct platform_driver pmi632_qg_driver = {
	.driver = {
		.name = "pmi632-qg",
		.of_match_table = pmi632_qg_of_match,
		.suppress_bind_attrs = true,
		.pm = pm_sleep_ptr(&pmi632_qg_pm_ops),
	},
	.probe = pmi632_qg_probe,
	.shutdown = pmi632_qg_shutdown,
};
module_platform_driver(pmi632_qg_driver);

MODULE_DESCRIPTION("Qualcomm PMI632 QGauge fuel gauge driver");
MODULE_AUTHOR("Marc Lainez <marc.lainez@gmail.com>");
MODULE_LICENSE("GPL");
