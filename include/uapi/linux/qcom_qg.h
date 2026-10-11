/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_LINUX_QCOM_QG_H
#define _UAPI_LINUX_QCOM_QG_H
#include <linux/types.h>
#include <linux/ioctl.h>

#define QCOM_QG_ABI_VERSION 1
#define QCOM_QG_PRESENT 1U
#define QCOM_QG_OCV_VALID 2U
#define QCOM_QG_FIFO_VALID 4U
#define QCOM_QG_ESTIMATE_VALID 8U
#define QCOM_QG_SOC_RESTORED 16U
#define QCOM_QG_FULL_HELD 32U
#define QCOM_QG_FULL_QUALIFIED 1U
#define QCOM_QG_LEARNED_VALID 2U

/* Fixed-width, pointer-free layout shared by Android and recovery. Charge
 * is signed nC (uA * ms); positive means charge entering the battery.
 */
struct qcom_qg_snapshot {
	__u32 version;
	__u32 flags;
	__aligned_u64 generation;
	__aligned_u64 sequence;
	__aligned_u64 timestamp_ms;
	__aligned_s64 charge_nc;
	__aligned_u64 sample_ms;
	__u32 gaps;
	__u32 profile_ohm;
	__s32 voltage_uv;
	__s32 current_ua;
	__s32 temperature_decic;
	__s32 ocv_uv;
	__s32 status;
	__s32 health;
	__u32 charger_state;
	__u32 charger_enabled;
	__s32 float_uv;
	__s32 input_limit_ua;
	__s32 input_online;
	__s32 design_uah;
	__s32 saved_full_uah;
	__s32 saved_cycles;
	__s32 accepted_soc_basis_points; /* -ENODATA before first estimate */
	__u32 reserved[3];
};

struct qcom_qg_estimate {
	__u32 version;
	__u32 flags;
	__aligned_u64 generation;
	__aligned_u64 sequence;
	__s32 soc_basis_points;
	__s32 full_uah;
	__s32 cycles;
	__u32 reserved[5];
};

#define QCOM_QG_GET_SNAPSHOT _IOR('Q', 0x70, struct qcom_qg_snapshot)
#define QCOM_QG_SUBMIT_ESTIMATE _IOW('Q', 0x71, struct qcom_qg_estimate)
#endif
