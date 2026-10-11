<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Laurel 4.14 to 6.18 power audit

Latest correction: [PMIC_CHARGING_FIX.md](PMIC_CHARGING_FIX.md). The earlier
QC/parallel, sample timing and charge-counter limitations below describe the
previous candidate; the newer source remains unbuilt and unvalidated.

## Exact references

- LineageOS `android_kernel_xiaomi_sm6125`, `lineage-23.2`,
  `77d2912bc00eda19182a95a24bbe23543c792814`.
- magicxavi `kernel_xiaomi_laurel_sprout`, branch `16`,
  `a9b94eac2508f1639ac6697f3ffba490a688327e`.

The second tree places the board configuration in
`arch/arm64/boot/dts/qcom/laurel_sprout-qrd.dtsi`; Lineage places it in
`arch/arm64/boot/dts/xiaomi/laurel_sprout/laurel_sprout-trinket-battery.dtsi`.
The Sunwoda, Feimaotui and default Wingtech battery profiles have identical
data after ignoring whitespace/comments. Different paths do not mean different
pack calibration. No source from the second tree was copied wholesale.

## Implementation comparison

| Function | Lineage 4.14 | magicxavi branch 16 | Present 6.18 implementation |
| --- | --- | --- | --- |
| Voltage/current conversion | QG last ADC and burst current, subtype scaling | Same qg-util.c | Implemented with Linux positive charging sign |
| SOC source | process_udata_work accepts QG_CC_SOC, QG_BATT_SOC, QG_SYS_SOC/QG_SOC from userspace | Same algorithm interface | Missing; OCV lookup only |
| FIFO measurement | Voltage/current sample arrays and accumulation timing feed algorithm | Same architecture | Reads FIFO voltage but does not integrate current/time into SOC |
| SOC direction | General logic plus F9S-specific measured-current guards | F9S guards made unconditional: no upward step during net discharge; no downward step while charging | Supplier-status guards only; does not reproduce full stock direction policy |
| Hold-full | Requires charge_done, MSOC >=99 and good health; F9S code writes 100 | Writes 100/MSOC/SDAM on qualified full | Conditional reporting step only; no calibrated MSOC/full-state persistence |
| Monotonic register | Software writes percent *255/100 to QG 0xbf | Same qg-util.c | No MSOC producer; reading this register cannot supply the missing algorithm |
| Restart after termination | SOC-based recharge at 99, threshold and sample count programmed | Same board value and programming | VBAT mode; threshold inherited and unvalidated |
| Charge inhibit | Cleared when board supplies no inhibit threshold | Same | Previously enabled incorrectly; source now clears it |
| Floating USB source | float-option=1, FLOAT_SDP; DCD timeout bit cleared for 300 ms | Same board value and programming | Previously omitted; source now programs USBIN_OPTIONS_2_CFG |
| Charger limits | 4.4 V, 3 A FCC, 2 A USB ICL, -200 mA ADC upper termination threshold | Same | Implemented ceilings; conservative unknown/SDP limit 500 mA |
| Gauge termination parameter | QG iterm 330 mA, separate from SMB5 termination | Same | Gauge does not implement the stock model that consumes this parameter |
| Capacity learning | fg-alg.c callbacks depend on valid battery/coulomb SOC | Board explicitly sets qcom,cl-disable | Saved capacity read only; no new learning |
| Cycle count | Eight buckets updated from real tracked SOC and charge-session events | Same algorithm; removes another device's override | Saved buckets read only; no updates |
| Profiles | OCV, FCC/temperature, charge/discharge and impedance/model tables | Same calibration data | Discharge OCV and limits only; not the complete model |
| Charging/thermal coordination | IRQs, votables, SW JEITA, thermal mitigation, parallel SMB1355 | Same architecture plus downstream customizations | Basic polled policy; lacks full voting/parallel integration |
| Quick Charge | PHY DPDM ownership, HVDCP/QC2/QC3, parallel path | Same architecture | Disabled pending proper PHY/charger integration |

Branch 16 also adds CHARGE_NOW_RAW as capacity times current SOC. That is a
derived quantity, not an independent hardware coulomb counter; copying it
would not fix the missing SOC estimator. Its optional CONFIG_DISABLE_TEMP_PROTECT
paths are not imported. Neither reference is a drop-in 6.18 driver.

## Why the current boot stays at 95

At uptime 4086 seconds, the connected maintainer build #27 still reports
95 percent, 4.337096 V, 32.2 C, battery current 0 uA and charger Full.
USB input is approximately 5.113 V / 98 mA with a 500 mA limit. Earlier logs
show a short positive net charging interval ending near 4.415 V; afterward
the current settles around zero. The input now powers the running system.

The voltage-to-SOC table maps the settled voltage to about 95 percent.
The early power-on OCV is retained for up to 180 seconds; neither that path
nor the later voltage fallback accumulates the charge that entered the pack.
The copied >=99 hold-full prerequisite cannot make an OCV estimate starting
at 95 converge to 100. This is a missing SOC model, not merely a delayed UI.
The captures do not identify raw INHIBIT versus TERMINATE: both map to Full.
The inhibit correction is supported by source comparison, but is not proof
that inhibit caused the recorded termination or the entire plateau.

## Required implementation to close the gaps

1. Implement a 6.18 SOC producer using coherent QG FIFO current/voltage,
   accumulator counts and sample intervals. Consume each interval once;
   do not integrate repeated property reads of the same burst sample.
2. Add temperature/pack-aware state initialization, valid resting OCV
   correction, load/ESR compensation, and qualified full/empty endpoints.
   Retain fractional charge so positive charge below one percent is not lost.
   Suspend, reboot and battery removal must not silently duplicate or lose
   samples or reuse another pack's learned state.
3. Apply stock-style current-aware monotonic reporting, full-state hysteresis
   and persisted state only on top of that SOC producer. Stable reporting is
   not a substitute for the producer. Do not force 100 solely on USB online
   or an undifferentiated Full status.
4. Couple validated MSOC to stock recharge threshold/sample programming, or
   deliberately specify and validate a voltage-based alternative. Do not
   enable SOC recharge while QG 0xbf contains stale bootloader/stock state.
5. Implement cycle updates and capacity-learning prerequisites from real
   charge-session data, then add QC/DPDM/parallel and full thermal policy.
   Keep saved stock SDAM records read only until state ownership is defined.

The references expose the transport and kernel-side adjustments, but their
userspace-fed SOC implementation is not included in these driver files.
The current integration cannot be called complete PMIC support or stock
battery accuracy. A register-only charger correction cannot finish this port.

## Changes and validation in this audit

The charge-inhibit correction is retained. USBIN_OPTIONS_2_CFG now matches both
stock boards' FLOAT_SDP and DCD timeout settings. Existing unknown-source
current limits, voltage limits, hard thermal protection and watchdog remain.
No force-charge, battery percentage override, SDAM write or live register write
was performed. These source changes are not installed on the connected phone.
Builds and installation belong to the maintainer under AGENTS.md.

Acceptance requires matched kernel/DTB/ROM, actual current flowing into a
partially discharged pack, SOC progression during charging, correct taper and
termination, restart, unplug/reboot continuity, and sensible temperature/load
behavior. A displayed 100 or a readable cycle count alone is insufficient.
