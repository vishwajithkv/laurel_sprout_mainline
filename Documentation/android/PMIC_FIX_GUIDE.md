# Xiaomi Mi A3 (laurel_sprout) PMIC & Charging Fix Guide (ACK 6.18)

> **Target Platform:** Xiaomi Mi A3 (`laurel_sprout`), Qualcomm Snapdragon 665 (`SM6125` / Trinket)
> **Target Kernel:** Android Common Kernel (ACK) 6.18 (`6.18.32`, custom mainline port)
> **Reference 4.14 Downstream Kernels:**
> - [LineageOS/android_kernel_xiaomi_sm6125](https://github.com/LineageOS/android_kernel_xiaomi_sm6125) (branch `lineage-23.2`, commit `77d2912bc00eda19182a95a24bbe23543c792814`)
> - [magicxavi/kernel_xiaomi_laurel_sprout](https://github.com/magicxavi/kernel_xiaomi_laurel_sprout/commits/16/) (branch `16`, commit `a9b94eac2508f1639ac6697f3ffba490a688327e`)
> **Primary Input Handoff:** `pmic_partial_to_full.md`

---

## 1. Executive Summary & Problem Breakdown

PMIC support on mainline 6.18 began with a partial port from postmarketOS (pmos) and Marc Lainez's PMI632 driver. Initial bring-up suffered from the device reporting **"always charging"** (due to Cuttlefish mock Health HAL + TCPM advertising 3 A). While physical measurements (voltage, current, temperature, battery ID) and primary charging have been brought up, several major issues remain unresolved:

1. **SOC Mismatch between Android (100%) and Recovery (97%–99%)**:
   - The attempted workaround of saving state to `/metadata/laurel-battery/state` was **explicitly rejected by the maintainer**.
   - Recovery starts with wall clock `1970-01-01` (no RTC) and does not mount `/metadata`, causing recovery to fall back to raw voltage OCV while Android reports latched 100%.
2. **Secondary / Parallel Charger (SMB1355) Probe Failure (`-ENXIO`)**:
   - `qcom-smb1355 0-000c: error -ENXIO: SMB1355 ID unavailable`. The I2C address byte is NACKed at early boot.
   - Discrepancies exist in `SMB_EN` GPIO control, interrupt routing, regmap retries, and hardware presence.
3. **Near-Full Cycling at 98%–99%**:
   - In build #31, the charger repeatedly cycled between taper (state 4) and termination (state 5) because the 99% hardware SOC recharge comparator was activated prematurely before qualified full was latched.
4. **False 3 A / "Charging Rapidly" Display on 500 mA SDP**:
   - Corrected in build #32 via Health exclusion property (`ro.vendor.health.ignore_supply_names`) and `system/core` `BatteryMonitor.cpp` rescan patch, but needs permanent integration safeguards.
5. **Quick Charge (QC2/QC3) and 18 W Wall Charging Parity**:
   - DPDM PHY ownership and authenticated DCP voltage stepping need verification against hardware wall adapters.

---

## 2. Deep Dive & Root Causes

### 2.1 The SOC Parity Failure & The Rejected `/metadata` Persistence

#### What went wrong:
The previous implementation attempted to make `laurel-batteryd` persist estimated battery state into `/metadata/laurel-battery/state`.
- **Maintainer rejection:** The maintainer rejected `/metadata` persistence because battery state is hardware/PMIC state. Tying battery estimation to an Android filesystem creates bootloader/recovery coupling, breaks statelessness during recovery wiping, and fails when partitions are unmounted.
- **Clock desynchronization:** Mi A3 recovery has no synchronized RTC at boot (clock defaults to `1970-01-01`). The persistence logic checked record age (`timestamp <= 30 days`), rejecting the Android record as "from the future" or failing open because `/metadata` was unmounted.

#### Downstream 4.14 Reference Architecture:
In Qualcomm's 4.14 kernel (`drivers/power/supply/qcom/qpnp-qg.c`, `qg-soc.c`, `qg-sdam.c`):
1. **PMIC Monotonic Register (`QG_SOC_MONOTONIC_REG` = 0xBF at QG base 0x4800):**
   Whenever software updates monotonic SOC (`msoc`), it writes:
   ```c
   reg = (msoc * 255) / 100;
   qg_write(chip, chip->qg_base + QG_SOC_MONOTONIC_REG, &reg, 1);
   ```
   Because PMI632 is powered continuously by the battery (VBAT), register `0xBF` **retains its value across SoC warm resets, fastboot, and reboots into recovery**!
2. **PMIC SDAM (Shared Direct Access Memory at SPMI `0xB100`):**
   Qualcomm saves shutdown state into SPMI SDAM non-volatile cells:
   - Offset `0x45`: `SDAM_VALID` (1 byte)
   - Offset `0x46`: `SDAM_SOC` (1 byte, stored as `msoc`)
   - Offset `0x47`: `SDAM_TEMP` (2 bytes)
   - Offset `0x4B`: `SDAM_OCV_UV` (4 bytes)
   - Offset `0x53`: `SDAM_TIME_SEC` (4 bytes)
   - Offset `0x58`: `SDAM_CYCLE_COUNT` (16 bytes, 8 buckets)
   - Offset `0x68`: `SDAM_LEARNED_CAPACITY` (2 bytes)
   - Offset `0x80`: `SDAM_MAGIC` (`0x12345678`)

   On boot (`qpnp-qg.c:qg_determine_pon_soc`):
   ```c
   /* If SDAM_VALID is 1 and S7_PON_SOC is within threshold of SDAM_SOC: */
   soc = shutdown[SDAM_SOC];
   /* Kernel seeds directly from SDAM_SOC without touching filesystems */
   ```

#### Why 6.18 broke:
In `kernel/mainline/sm6125-mainline-6.18/drivers/power/supply/qcom_pmi632_qg.c`:
- Register `0xBF` is written at line 817 when an estimate is submitted, but **never read during probe or boot**!
- In `pmi632.dtsi`, `pmi632_qg_sdam` (`0xb100`) was marked `read-only;` and only exposed cells for `cycle-count@58`, `learned-capacity@68`, and `history-magic@80`. The shutdown SOC cells (`0x45`, `0x46`, `0x4B`) were omitted!
- Without reading register `0xBF` or `SDAM_SOC`, the 6.18 kernel probed with zero knowledge of prior state and re-evaluated loaded terminal voltage (`power_supply_batinfo_ocv2cap`), producing ~97%–98% under loaded USB.

---

### 2.2 SMB1355 Secondary Charger Probe Failure (`-ENXIO`)

#### Symptoms:
```text
qcom-smb1355 0-000c: error -ENXIO: SMB1355 ID unavailable
```

#### Root Causes:
1. **I2C Bus & Address:**
   - In 4.14, SMB1355 is on `qupv3_se1_i2c` (`i2c@4a84000`, `i2c1` in mainline), slave address `0x0c`.
   - Error `-ENXIO` in Linux I2C means the master received a **NACK on the device address byte**.
2. **Missing Power / Hardware Enable (`SMB_EN`):**
   - In 4.14 downstream, SMB1355 requires `SMB_EN` to be enabled or under hardware control from the primary PMI632 charger.
   - Primary PMI632 controls `SMB_EN` via register `MISC_SMB_EN_CMD_REG` (`MISC_BASE + 0x48`):
     ```c
     #define MISC_SMB_EN_CMD_REG (MISC_BASE + 0x48)
     #define SMB_EN_OVERRIDE_BIT BIT(3)
     #define SMB_EN_OVERRIDE_VALUE_BIT BIT(4)
     ```
   - In 6.18 DTS, someone configured PMI632 GPIO2 as `function = "func1"; output-enable;` but never configured its state or linked it to the charger.
   - If `SMB_EN` is held low or floating, the SMB1355 remains in hard shutdown and does not respond on the I2C bus.
3. **Interrupt Pin Mapping Discrepancy:**
   - In 6.18 DTS:
     ```dts
     interrupt-parent = <&tlmm>;
     interrupts = <130 IRQ_TYPE_LEVEL_LOW>;
     ```
   - On Laurel (`laurel_sprout-trinket-pinctrl.dtsi`), TLMM GPIO 130 is actually `spkr_1_sd_n` (Speaker Shutdown)!
   - In 4.14 base DTS (`smb1355.dtsi`), the hardware IRQ is:
     ```dts
     interrupt-parent = <&spmi_bus>;
     interrupts = <0x2 0xC5 0x0 IRQ_TYPE_LEVEL_LOW>;
     ```
     Peripheral `0xC5` on PMI632 (`SID 2`) is **PMI632 GPIO 6**, not SoC TLMM 130!
4. **Early Boot Probe Timing & Retries:**
   - In 4.14, `i2c_pmic_read` implemented a retry loop on `-ENOTCONN` / bus busy.
   - Mainline `qcom_smb1355.c` attempts a single `regmap_read(chip->regmap, REVID_MFG_ID_SPARE_REG, &id)` at probe time. If the QUP core is still initializing or VBUS is not yet asserted, probe fails immediately.
5. **Hardware Variant Population:**
   - If the specific board does not populate the parallel charging IC, probe failure must be handled gracefully without triggering error loops or blocking primary charging.

---

### 2.3 Near-Full Cycling at 98%–99% (Taper vs. Termination)

#### Symptoms:
Device repeatedly cycles between State 4 (taper charging) and State 5 (termination), jumping back and forth every few seconds.

#### Root Cause:
In 6.18 candidate `qcom_pmi632_charger.c`:
- Stock Laurel uses `auto-recharge-soc = <99>`.
- The 6.18 driver enabled the 99% SOC hardware comparator as soon as the gauge was calibrated (`soc_ready == true`).
- When the battery was at 98.4%, charging reached float voltage and tapered down to the termination current threshold (-200 mA). Hardware terminated charging (State 5).
- Immediately upon termination, the hardware saw that `SOC (98.4%) < 99%` and triggered an immediate recharge!
- Charging resumed, hit termination current again, and cycled indefinitely.

#### 4.14 Downstream Logic:
In stock `qpnp-smb5.c:smb5_recharge_update()` and `smb5-lib.c`:
- **Before qualified Full is latched:** Recharge uses **3-sample VBAT recharge** (triggers only if battery voltage drops to `float_voltage - 100mV` = ~4.30V).
- **After qualified Full is latched:** The charger enables **SOC recharge at 99%** with 1-sample filter.
- SOC recharge must NEVER be enabled while the battery is still in its initial charge cycle below 99%.

---

## 3. Detailed Fix Specifications

### Fix 1: Hardware-Backed SOC Persistence (Eliminating `/metadata`)

#### 1. Device Tree Updates (`qcom/pmi632.dtsi`)
Enable read/write on `pmi632_qg_sdam` and define shutdown SOC cells:
```dts
pmi632_qg_sdam: nvram@b100 {
	compatible = "qcom,spmi-sdam";
	reg = <0xb100>;
	#address-cells = <1>;
	#size-cells = <1>;
	ranges = <0 0xb100 0x100>;
	status = "okay";

	/* Shutdown / Boot SOC state */
	pmi632_sdam_valid: sdam-valid@45 {
		reg = <0x45 0x1>;
	};
	pmi632_sdam_soc: sdam-soc@46 {
		reg = <0x46 0x1>;
	};
	pmi632_sdam_ocv: sdam-ocv@4b {
		reg = <0x4b 0x4>;
	};

	/* History cells */
	pmi632_qg_cycles: cycle-count@58 {
		reg = <0x58 0x10>;
	};
	pmi632_qg_learned_capacity: learned-capacity@68 {
		reg = <0x68 0x2>;
	};
	pmi632_qg_magic: history-magic@80 {
		reg = <0x80 0x4>;
	};
};
```

#### 2. Kernel QG Driver Updates (`qcom_pmi632_qg.c`)
1. **On Probe:**
   Read hardware monotonic register `0xBF` from PMI632:
   ```c
   unsigned int msoc_raw;
   ret = regmap_read(qg->regmap, qg->base + 0xBF, &msoc_raw);
   if (!ret && msoc_raw > 0 && msoc_raw <= 255) {
       int boot_soc_bp = (msoc_raw * 10000) / 255;
       qg->reported_capacity = DIV_ROUND_CLOSEST(boot_soc_bp, 100);
       qg->capacity_initialized = true;
       qg->boot_soc_basis_points = boot_soc_bp;
   }
   ```
2. **In `qg_snapshot()`:**
   Pass `boot_soc_basis_points` to userspace in `qcom_qg_snapshot.accepted_soc_basis_points` if no live estimate has been submitted yet.
3. **When an estimate is submitted:**
   Update both register `0xBF` and SDAM cell `sdam-soc@46` (with `sdam-valid@45 = 1`).

#### 3. Userspace `batteryd` Updates (`device/xiaomi/laurel_sprout/power/batteryd/batteryd.cpp`)
- **Remove all references to `/metadata/laurel-battery/state`**.
- In `Gauge::update()`:
  When `!initialised_`, check if `s.accepted_soc_basis_points >= 0`. If so:
  ```cpp
  soc_ = s.accepted_soc_basis_points;
  displayed_ = soc_;
  ```
  This immediately seeds the gauge from hardware register `0xBF` on both Android and Recovery boots.
- No filesystem mounts, timestamp comparisons, or SELinux policies on `/metadata` are needed.

---

### Fix 2: SMB1355 Secondary Charger Resolution

#### 1. Device Tree Corrections (`sm6125-xiaomi-laurel-sprout-battery.dtsi`)
Fix the interrupt parent and pin configuration:
```dts
&i2c1 {
	status = "okay";
	clock-frequency = <400000>;

	laurel_smb1355: charger@c {
		compatible = "qcom,smb1355";
		reg = <0xc>;
		/* PMI632 GPIO 6 (SPMI peripheral 0xC5) is the hardware SMB1355 IRQ */
		interrupt-parent = <&pmi632_gpios>;
		interrupts = <6 IRQ_TYPE_LEVEL_LOW>;
		pinctrl-names = "default";
		pinctrl-0 = <&laurel_smb_sense &laurel_smb_ctm>;
	};
};
```

#### 2. Driver `qcom_smb1355.c` Probe Retries & Bus Verification
Modify `smb1355_probe()` in `drivers/power/supply/qcom_smb1355.c`:
```c
/* Add retry loop for early-boot bus stabilization */
int retries = 5;
do {
    ret = regmap_read(chip->regmap, REVID_MFG_ID_SPARE_REG, &id);
    if (!ret && id == 0xFF)
        break;
    msleep(20);
} while (--retries > 0);

if (ret || id != 0xFF) {
    dev_info(dev, "SMB1355 secondary charger not detected (id=0x%x, ret=%d); parallel charging disabled\n", id, ret);
    return -ENODEV; /* Soft fallback, do not spam or halt */
}
```

#### 3. Primary Charger Coordination (`qcom_pmi632_charger.c`)
Ensure PMI632 hardware control of `SMB_EN` is configured during initialization:
```c
/* Allow PMI632 hardware state machine to control SMB_EN */
regmap_update_bits(chip->regmap, chip->base + 0x1648, /* MISC_SMB_EN_CMD_REG */
                   BIT(3) | BIT(4), 0);
```

---

### Fix 3: Recharge Policy Hysteresis (Solving 98%–99% Cycling)

In `drivers/power/supply/qcom_pmi632_charger.c`:

```c
static void smb5_update_recharge_policy(struct smb5_chip *chip, bool qualified_full)
{
	if (!qualified_full) {
		/* Before qualified full: Use 3-sample VBAT recharge (float - 100 mV) */
		regmap_update_bits(chip->regmap, chip->base + CHGR_CFG2_REG,
				   RECHARGE_THRESHOLD_MASK, RECHARGE_100MV);
		regmap_update_bits(chip->regmap, chip->base + CHGR_ENG_CHARGING_CFG_REG,
				   SOC_BASED_RECHARGE_BIT, 0);
	} else {
		/* After qualified full: Switch to stock 99% SOC recharge */
		regmap_update_bits(chip->regmap, chip->base + CHGR_ENG_CHARGING_CFG_REG,
				   SOC_BASED_RECHARGE_BIT, SOC_BASED_RECHARGE_BIT);
	}
}
```

- When the charger enters state 5 (termination), do not switch to SOC recharge until the gauge has held `full == true` for at least 60 seconds.
- If battery voltage drops by more than 100 mV or SOC drops below 98%, clear the qualified full latch and restart charging.

---

### Fix 4: Quick Charge (QC2 / QC3) Validation

In `drivers/power/supply/qcom_pmi632_charger.c` and devicetree:
1. Verify that `dpdm-supply = <&qusb_phy0>;` is properly bound in DTS.
2. In `smb5_handle_usb_plugin()`:
   - Check APSD result register: `POWER_SUPPLY_TYPE_USB_DCP`.
   - Acquire DPDM regulator only for authenticated DCP.
   - For QC2: Request 9 V (never 12 V on SM6125).
   - For QC3: Measure baseline 5 V, increment with 200 mV pulses up to target voltage (max 9 V / 18 W).
   - If APSD is SDP (`POWER_SUPPLY_TYPE_USB`), leave DPDM untouched and enforce 500 mA input ceiling.

---

## 4. Verification & Diagnostic Commands

When validating on target hardware via ADB:

### 1. Check Hardware Monotonic Register 0xBF & SDAM
```bash
# Read Monotonic SOC register (0x48BF)
adb shell "su -c 'od -tx1 /sys/kernel/debug/regmap/spmi0-02/registers -j 0x48bf -N 1'"

# Check QG sysfs values
adb shell "cat /sys/class/power_supply/bms/capacity"
adb shell "cat /sys/class/power_supply/bms/calibrate"
adb shell "cat /sys/class/power_supply/bms/charge_counter"
```

### 2. Verify Android vs. Recovery SOC Parity
1. Charge phone to 100% in Android (`dumpsys battery` reports `level: 100`, `status: 5`).
2. Run `adb reboot recovery`.
3. In recovery ADB:
   ```bash
   adb shell "cat /sys/class/power_supply/bms/capacity"
   ```
4. Verify recovery reads **100%** (matching register 0xBF), not 97% or 98%.

### 3. Verify Health Supply Exclusion (Laptop SDP 500 mA)
```bash
adb shell dumpsys battery
# Verify:
#   Max charging current: 500000 uA (NOT 3000000 uA)
#   Max charging voltage: 5000000 uV
```

### 4. Monitor Near-Full Recharge Behavior
```bash
# Watch charger state around 99% - 100%
adb shell "while true; do cat /sys/class/power_supply/battery/status; cat /sys/class/power_supply/battery/capacity; cat /sys/class/power_supply/battery/current_now; sleep 2; done"
```
Ensure no alternating between Charging and Full every 5 seconds.
