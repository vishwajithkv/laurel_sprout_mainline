# Live 4.14 PMIC comparison, 2026-10-11

Read-only inspection of device 5781707fda83 running
`4.14.357-openela-perf-g1ac5e8c71b79 #1`, built 2026-10-10 17:40:56 UTC.
ADB was restarted as root to read protected sysfs and kernel logs. No PMIC
register writes, flash, filesystem mount, service-policy change or forced SOC
were performed. Captures are sequential; do not treat current/voltage readings
from different moments as a simultaneous power measurement.

Artifacts: `power-and-probe.txt` contains battery supplies, probe logs and RTC;
`stock.dtb` was pulled from `/sys/firmware/fdt`; `stock.dts` is its decoded
live device tree. These device artifacts include identifying board properties.

## Confirmed missing mainline functionality

### The three reported symptoms: source comparison

This comparison is against the downstream 4.14 LineageOS reference, not
Xiaomi factory firmware. The installed revision and available source revision
are different, as recorded below. These are source-supported mechanisms;
they do not prove every live status transition is a physical charging restart.

| Symptom | Downstream 4.14 mechanism | Current 6.18 gap / supported correction |
| --- | --- | --- |
| Laptop USB incorrectly says rapidly charging | USB input capability is separate from battery FCC and Type-C source capability. The captured USB supply limits current to 500 mA. | Health must select the actual input supply. The source-only TCPM supply exclusion already carried in build #32 addresses the observed 3 A capability contamination; verify BatteryService receives 500 mA and 5 V. Do not substitute the battery current or the advertised 18 W adapter maximum. |
| Charging alternates with connected, not charging near full | `smblib_get_prop_batt_status()` applies enable-bit, suspend and termination-workaround checks. Its Laurel-specific branch reports hardware Full as Charging until `chg_done` or capacity 100. `qg_charge_full_update()` coordinates completion and the recharge workaround. | The mainline QG status getter converts hardware Full to NotCharging until `QCOM_QG_FULL_QUALIFIED`. The estimator and charger have separate completion latches. Reconcile completion, hold and recharge as one state machine; preserve genuine thermal/input pauses. Copying the downstream UI masking alone would hide a possible physical fault. |
| 100% drops to 95%, or recovery disagrees with Android | Validated SDAM/RTC restoration preserves shutdown SOC. Full-charge hold reports 100, then `maint_soc` linearizes the return to underlying SOC. | Mainline restarts the estimator/full qualification and can seed from OCV again; writing monotonic SOC alone is not a boot restoration path. Implement validated kernel-side SDAM restoration and full-state continuity, with the same gauge semantics in Android and recovery. `/metadata` persistence is rejected. |

Source details:

- `qpnp-qg.c:qg_charge_full_update()` accepts charge completion at SOC >=99
  with appropriate health, holds full, and on Laurel writes 100 to the
  monotonic register and SDAM SOC. Its forced-recharge workaround requires a
  previously established full state, input present, SOC at/below the recharge
  threshold and no charging already in progress. It waits while charge_done
  remains set rather than immediately discarding full state.
- `qg_get_battery_capacity()` returns held-full SOC first, then maintained SOC,
  then underlying msoc. `qg-soc.c:update_msoc()` applies current-direction
  guards specific to Laurel. Maintained SOC decrements in configured steps;
  the default maintenance interval at/above recharge SOC is 120 seconds.
  Below that threshold it can catch up faster. This is distinct from the
  current estimator's general 20-second display-step cadence.
- `smb5-lib.c:smblib_eval_chg_termination()` sets Laurel `chg_done` on hardware
  termination with real capacity 100. It also schedules the downstream
  termination workaround. A complete port must account for that policy,
  rather than treating every raw TERM/TAPER transition as a new session.
- Mainline `qcom_pmi632_qg.c` explicitly demotes Full to NotCharging without
  a valid qualified estimate. `batteryd.cpp` resets its full state on a new
  generation and unplug and only publishes qualification at displayed 100.
  The charger's separate `full_latched` controls its recharge policy. These
  separate owners explain a reporting inconsistency; proving physical
  oscillation additionally requires synchronized raw charger state, current,
  voltage, input-limit and thermal-policy observations on 6.18.

Android's local `frameworks/base/packages/SettingsLib/.../BatteryStatus.java`
calculates charging speed from `EXTRA_MAX_CHARGING_CURRENT` multiplied by
`EXTRA_MAX_CHARGING_VOLTAGE`, compared with resource thresholds. SystemUI uses
that classification for the lock-screen text. It does not measure net battery
watts. Input capability may remain high while battery current tapers normally
near full, but a 500 mA / 5 V laptop input must not be represented as 3 A.

Supported fix order: keep the correct input-provider selection; replace the
rejected filesystem restore with validated SDAM/RTC continuity; coordinate
gauge completion, full hold, smoothing and recharge; then validate physical
charging and UI status together. Neither a design-capacity adjustment nor
forcing Charging whenever USB is online solves these three problems.

### Hardware-backed SOC restore and RTC

Stock boot logs explicitly say:

```text
QG-K: qg_determine_pon_soc: using SHUTDOWN_SOC @ PON ocv_uv=4287100uV soc=95
```

Stock has `/sys/class/rtc/rtc0` / `/proc/driver/rtc`, backed by PM6125.
Its calendar reads 1977 while Android wall time is 2026. A correct calendar
is not required for stock's elapsed-time validation: it compares timestamps
from the same hardware RTC. Mainline observations had no RTC; its board
fragment explicitly disables CONFIG_RTC_DRV_PM8XXX even though the DT RTC
node is enabled. This is a definite configuration gap, distinct from SDAM.

The inspected local 4.14 reference at `/home/vishwajithkv/Android/kernel`
is revision 488774b31ce97bb8e762e5ddd2a7b5e63358a63e, branch floppy-unity.
It is not the exact running revision 1ac5e8c71b79, which was unavailable in
that checkout. Live DT/logs take precedence for the installed kernel; source
comparisons below describe the available downstream reference.

`qg_determine_pon_soc()` reads hardware OCV, temperature, RTC and SDAM state;
validates record validity, elapsed time, temperature and optional SOC mismatch;
then chooses shutdown SOC or OCV fallback. It writes accepted state back to
SDAM and QG monotonic register. It does not blindly trust register 0x48bf.
Live DT sets ignore-shutdown time to 2592000 seconds, temperature difference
100 and shutdown SOC threshold 40. Those are reference policy values, not
automatically appropriate bounds for a new estimator.

Mainline currently lacks this boot/shutdown state path and relies on the
rejected filesystem persistence attempt. Restore/write ownership, validity,
pack changes and failed/interrupted updates need a proper kernel-side design.
SOC continuity alone will not reproduce fractional SOC, FCC or ESR algorithms.

### Correct SDAM layout versus the other LLM guide

The available stock `qg-reg.h` specifies:

| Offset relative to QG SDAM base b100 | Field / size |
| --- | --- |
| 0x46 | Valid / 1 byte |
| 0x47 | SOC / 1 byte |
| 0x48 | Temperature / 2 bytes |
| 0x4a | Battery resistance / 2 bytes |
| 0x4c | OCV / 4 bytes |
| 0x50 | Battery current / 4 bytes |
| 0x54 | Hardware RTC seconds / 4 bytes |
| 0x58 | Eight cycle buckets / 16 bytes |
| 0x68 | Learned capacity / 2 bytes |
| 0x80 | Magic / 4 bytes |

`pmic_fix_guide.md` incorrectly places Valid at 0x45, SOC at 0x46, OCV at
0x4b and time at 0x53. Do not apply its proposed cells/writes. Verify byte
ordering and exact compatible stock format before enabling writes; preserve
cycle/FCC history and PBS-owned locations. PMIC SDAM is hardware-retained
state, not proof of retention after battery removal or a PMIC reset.

### Capacity model and other omitted measurements

Live stock at around 91–93% reports:

- Sunwoda profile `S88512_mtp_Sunwoda_4V4_4030mAh`, resistance ID 67568 ohm.
- `bms/charge_full=4048000`, `bms/charge_full_design=4048000` uAh.
- Aggregate battery supply reports 4030000 uAh, showing supply semantics differ.
- Cycle count 971; last bucket now 389 rather than the earlier 388.
- Fractional `capacity_raw`, `cc_soc`, OCV, resistance, current/voltage averages,
  SOC-ready status and time-to-full predictions.

Mainline's 3172000 uAh is a decoded historical SDAM value; it must not be
called the currently accurate maximum capacity. Determine why stock's live
capacity differs: learning, profile fallback and provider conventions are
possible explanations, not proven causes. Mainline's complete ESR/impedance,
capacity-learning, time prediction and cycle-persistence parity is missing.
Several stock ESR/SOH properties return -22; their names alone do not prove
these features are working in this ROM either.

Current signs differ: stock's battery and bms supplies can use opposite signs;
mainline normalizes positive current to charge entering the battery. Compare
each provider's convention, not bare signs between different supplies.

## SMB1355 failure is also present in this stock build

Live stock logs:

```text
i2c_geni 4a84000.i2c: i2c error :-107
I2C PMIC: i2c_pmic_read: i2c_pmic_read failed for 3 retries, rc = -107
I2C PMIC: i2c_pmic_probe: Couldn't determine initial status rc=-107
i2c_pmic: probe of 0-000c failed with error -107
THERM-BALANCE: Parallel PSY entry not found.
```

The 0-000c DT-created device exists, named i2c-pmic, but has no driver binding
and no parallel power_supply. Thus adding probe retries is not demonstrated
to fix this phone: stock already retries and fails. This is not proof that the
chip is absent; hardware variant, enable/power and transport remain open.

Both live stock DT and the Laurel board override in the available source use
TLMM GPIO130, IRQ_TYPE_LEVEL_LOW, with pull-up/input pin configuration. The
generic smb1355.dtsi uses PMI632 GPIO6, but Laurel overrides it. Do not switch
mainline to GPIO6 based on that generic include. An interrupt change cannot
by itself fix the early address-phase read failure.

The guide's primary `chip->base + 0x1648` is also incorrect for our driver:
base is 0x1000 and the existing relative offset is 0x648, yielding absolute
0x1648. Blindly releasing SMB_EN override would bypass the current secondary
budget/fault shutoff policy; investigate the probe dependency without doing so.

## Recharge and charger-input comparison

Stock SDP reports physical input settled at 500 mA / 5 V, despite Type-C
showing a high-current source. One later paired capture measured input
362673 uA at 5040816 uV, about 1.83 W. This is laptop input, not a QC test.
Stock charge_type=Fast is a charger phase classification, not an 18 W reading.
Stock BatteryService fields in the initial capture were old; sysfs readings
must be used alongside broadcast age, not treated as perfectly current UI.

Available stock `qpnp-smb5.c` selects SOC recharge during initialization when
DT specifies it, and Laurel specifies 99%. The guide's claimed stock dynamic
VBAT-before-full/SOC-after-full switch is not established by that source.
Stock QG separately gates the manual recharge workaround on its charge_full
state. Our qualified-full-gated hardware policy is a local adaptation, not
a verbatim stock mechanism. Continued near-full cycling needs measured raw
charger state plus MSOC/recharge policy evidence, not a guaranteed-cause claim.

## Next implementation priorities

1. Restore built-in PM6125 RTC support and validate read-only elapsed time.
   Do not rewrite RTC time to match Android calendar during this investigation.
2. Replace `/metadata` SOC persistence with a validated QG SDAM boot/shutdown
   record path, using correct offsets and stock fallback checks. Preserve
   history cells; do not just trust any nonzero monotonic byte (0% is valid too).
3. Reconcile live FCC/profile/learned history and implement coherent full,
   discharge, cycle and learning state ownership across Android/recovery.
4. Treat SMB1355 as independently unresolved on both tested kernels; validate
   actual hardware presence/power/enable with a working reference before claiming
   its absence in mainline causes all charging limitations.
5. Validate QC/18 W using a known wall adapter at moderate SOC. None of these
   laptop captures proves QC or parallel charging.

No driver changes were made in this comparison pass.
