<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Laurel PMI632 battery and charging integration

Latest SOC/completion candidate (2026-10-11): [PMIC_SOC_CONTINUITY.md](PMIC_SOC_CONTINUITY.md). It supersedes the historical filesystem persistence and read-only shutdown-state descriptions below. Source-only; maintainer build and validation required.

Latest correction: [PMIC_CHARGING_FIX.md](PMIC_CHARGING_FIX.md). The earlier
QC/parallel, sample timing and charge-counter limitations below describe the
previous candidate; the newer source remains unbuilt and unvalidated.

Latest unbuilt SOC/recharge candidate: [PMIC_SOC_SERVICE.md](PMIC_SOC_SERVICE.md).
That document supersedes the older voltage-only/recharge descriptions below;
QC/parallel charging and measured SOC parity remain incomplete.

See [PMIC_4_14_COMPARISON.md](PMIC_4_14_COMPARISON.md) for the pinned Lineage
and magicxavi branch 16 comparison, omissions and requirements for SOC parity.

Status: basic telemetry observed on maintainer build #27. Later source changes,
including the charge-inhibit correction below, are not compiled or installed.
Live-device readings do not validate those later changes.
Agents do not build, compile, flash or run tests under this tree's AGENTS.md.

## Why Android always reported charging

The ROM's TARGET_INITIAL_BRINGUP selected the Cuttlefish health HAL. Its
health-aidl.cpp hardcodes AC and USB online, CHARGING, GOOD, 85 percent,
3.6 V, 25 C, 400 mA, 32 cycles and 4 Ah. These were fake values, not PMIC
readings. See device/google/cuttlefish/guest/hals/health/health-aidl.cpp.

Live ADB on 2026-10-09 found only the TCPM source power_supply, with no battery
or charger power_supply. ADC5 independently measured VBAT at about 4.34 V.
The TCPM supply advertised 5 V / 3 A; advertised source current is not a
measurement of battery charging current. Its name was
`tcpm-source-psy-1c40000.spmi:pmic@2:typec@1500`.
A fresh dumpsys battery capture also reported AC and USB powered, 85 percent,
3.6 V, 25 C and 1900000 uAh charge counter, matching the fake HAL.
The battery-ID ADC returned 758239 uV. With the stock 1.875 V reference and
100 kohm pull-up, that is approximately 67.9 kohm, consistent with Sunwoda.
Later build #27 measurements are recorded below.

The ROM now explicitly sets TARGET_HAS_BATTERY=true and
TARGET_HEALTH_HAL=default-aidl before optional/options.mk. This selects the
standard sysfs health service and its recovery variant. Other initial-bringup
choices stay as they were. A full matching ROM build is needed, because
replacing only boot.img leaves the fake vendor health HAL installed.

## Exact sources and authorship

Hardware baseline examined:
https://github.com/LineageOS/android_kernel_xiaomi_sm6125/tree/77d2912bc00eda19182a95a24bbe23543c792814

In that revision, Laurel's battery DTS enables PMI632 SMB5 and QG, not FG3/FG4:
`arch/arm64/boot/dts/xiaomi/laurel_sprout/laurel_sprout-trinket-battery.dtsi`.
Relevant charger/gauge sources are qpnp-smb5.c, smb5-lib.c, smb5-reg.h,
qpnp-qg.c, qg-util.c, qg-reg.h and fg-alg.c under drivers/power/supply/qcom/.

Mainline driver source donor: Marc Lainez's PMI632 port, PR 242:
https://github.com/msm8953-mainline/linux/pull/242
https://github.com/mlainez/linux-msm8953/tree/59ae383591619d8fb55176cbc89a84de1cdcfb8b

Copyright/license headers are retained. pmic-import-history.json records
selected upstream commit IDs, full authors, author dates and commit messages.
It is an import inventory, not a claim that the working-tree edits have already
been committed with those authors. When publishing, separate original imports
from local adaptations and preserve the imported author/date/trailers.
No downstream binary HAL or firmware is added.

## Source ownership

- ACK core: qcom_pmi632_charger.c, qcom_pmi632_qg.c, Kconfig/Makefile,
  ADC5 battery-therm channel and built-in board configuration.
- Devicetrees repository: PMI632 charger/gauge nodes, battery thermistor node,
  Laurel limits and supplier OCV tables, and both power/supply schemas.
  Compatibility schema paths in ACK are symlinks to this repository.
- ROM device tree: real Android and recovery health service selection and
  narrow sysfs_batteryinfo labels for physical PMI632 supply paths.
- Modules repository: no changes; these power drivers are built in so recovery
  and Android do not depend on loading vendor modules for battery supervision.

## Charging behavior implemented

Stock-derived ceilings are 4.4 V battery, 3 A battery current and 2 A USB input.
They are independent ceilings, not guaranteed simultaneous measured current.
The charger hardware and AICL can reduce delivered current below those limits.
The stock signed -200 mA ADC termination upper threshold is converted using
PMI632's 5 A full scale and written big-endian. Its unspecified lower threshold
is preserved. The stock 768-minute fast-charge safety timer is configured.
VBAT-based recharge currently inherits its threshold from previous PMIC setup;
that threshold is not explicitly programmed or validated here. Stock's QG
SOC-based recharge at 99 percent is not equivalent and is not implemented here.
Stock disables charge inhibit when no inhibit threshold is specified; Laurel
does not specify one. The mainline initialization now clears that bit rather
than enabling inhibit near float voltage. This source correction requires a
maintainer rebuild and validation; it does not establish recharge parity.

BC1.2 source handling limits SDP/unclassified/float sources to 500 mA and CDP
to 1.5 A. DCP may use the board's 2 A ceiling. APSD retry is bounded. The input
limit uses stock SW_OVERRIDE_HC_MODE register ordering, not merely a write to
USBIN_CURRENT_LIMIT_CFG. A userspace limit may lower the ceiling; zero suspends
input and a subsequent positive limit resumes it.

The two stock hard JEITA comparator codes are programmed before charge enable:
cold 0 C = 0x6894, hot 60 C = 0x15aa, hot first and cold second, big-endian.
Software compensation keeps 4.4 V through 44.0 C, lowers to 4.1 V from 44.1 C,
and stops at 58 C. Additional conservative policy reduces FCC to 1 A at/below
10 C, at/above 44.1 C, or when the charger temperature reaches 70 C. Charging
stops at/below 0 C or at/above 80 C charger temperature. This extra derating
is not a claim of full stock thermal-policy parity.

Battery and charger temperature reads are mandatory. Read/programming failures
stop charging. Hardware hard JEITA and thermal protection remain active;
JEITA_EN_CFG controls only the soft compensation replaced by the software
worker. High-current limits are set only after battery/profile checks.
Watchdog bite is configured to disable charging. Only the supervised worker
pets it; the bark IRQ schedules supervision and does not pet unconditionally.

A charging wake source is retained while input is online, so five-second thermal
polling cannot silently stop in suspend. This avoids depending on the currently
disabled PMIC RTC. It costs plugged-in power and needs replacement with a fully
validated autonomous thermal policy for production. It is released on unplug
and cleanup. Shutdown stops polling and disables charge; a later charger-mode
boot can probe and resume. Off-state charging parity remains unvalidated.

USB current uses PMI632's buck sense conversion: ADC microvolts * 5 / 2 gives
microamps (0.4 V/A). USB voltage is available whenever the input is online,
including at full charge. Type-C role/VBUS regulation remain owned by their
existing drivers; the charger does not rewrite Type-C registers.

## Battery information

| Field | Source and limitations |
| --- | --- |
| Present | QG battery-present status, refreshed when queried and on missing IRQ |
| Charging/discharging/full/not charging | Actual SMB5 status, including software disable; USB online alone does not imply charging |
| Voltage | QG last ADC measurement, microvolts |
| Current | QG burst average, peripheral subtype-specific 5 A/10 A scaling; Linux positive = charging |
| Temperature | ADC5 physical battery thermistor, reported in tenths of a Celsius degree |
| Fault health | Battery temperature and overvoltage; GOOD does not mean a new battery or 100 percent remaining lifetime |
| Design capacity/chemistry | Standard simple-battery metadata: nominal 4040 mAh, lithium-ion |
| Percentage | Stock supplier discharge OCV tables, with temperature in Celsius; loaded-voltage fallback remains an estimate |
| Cycle count | Read-only stock SDAM bucket sum / 8, when history is valid; ongoing counting is not implemented |
| Learned full capacity | Read-only stock SDAM signed mAh converted to uAh, when valid; new capacity learning is not implemented |
| Wear/health percentage | No dedicated property; valid saved full capacity can be compared with design capacity, with the history limitations below |
| Charge counter/time-to-full | Not implemented |

2026-10-09 maintainer build #27 live readback confirms a present 68-kohm
Sunwoda-profile battery, approximately 33 C thermistor temperature, real
charge/discharge current, and stock history `charge_full=3172000` uAh and
`cycle_count=971`. Reading b100 bytes 0x58..0x69 gives bucket values
320,1006,1342,1495,1349,1097,776,388 (sum/8 = 971) and signed capacity
0x0c64 = 3172 mAh. That validates decoding, not current battery wear or a
new learning session; the stored capacity/design ratio is about 78.5 percent.
This boot's healthd logs remain at 95 percent through charge termination and
unplugging. A separate full status produces the lockscreen "Charged" label
even though the OCV estimate is 95. Early current reversals and voltage
changes must not be interpreted as sudden percentage changes.

### 2026-10-09 charging and reference audit

Build #27 at the latest readback reports 95 percent, 4.336 V, 33.3 C,
USB online, battery current 0 uA, and SMB5 Full. USB input measures about
5.112 V / 111 mA with a 500 mA limit. USB input current is not battery
current: the running system consumes power even when battery charging stops.
Earlier in this same boot healthd records positive battery currents of about
123--210 mA and battery voltage reaching 4.415 V, followed by Full and near-zero
battery current. This supports an earlier net charging interval and a stopped
charging state now; it does not prove accurate SOC or complete charging parity.

The charger maps both TERMINATE and INHIBIT to Full, matching stock's status
mapping. Android uses that status for its "Charged" label independently of
the level. These captures do not distinguish the two raw charger states.
The erroneous inhibit enable identified above is a possible contributor to
near-full restart behavior, not a proven explanation for every Full report.

The gauge's 95 percent is an OCV/voltage estimate, not a hardware coulomb
counter. Removing the old unconditional Full-to-100 override prevents the
100-to-95 jump but cannot replace stock QG SOC tracking. It can remain at
95 after genuine termination. Forcing another charge cycle or forcing the
reported percentage to 100 is not a substitute for a calibrated gauge.

A subsequent readback at uptime 1543 seconds still reports 95 percent,
4.336901 V, 32.6 C and zero battery current, with fresh healthd messages
every five seconds. VOLTAGE_OCV is also available at 4.336901 V, so the
reporting path is active, rather than Android retaining a disconnected gauge's
last value. The reported percentage has not increased during this boot.
The capacity path retains fresh OCV for up to 180 seconds and never integrates
the positive charging-current samples into SOC. The early charging interval
therefore need not raise its estimate; after charging stops the settled voltage
still maps to the mid-90s. Stock's 99-percent hold-full condition cannot make
this estimator converge from 95 to 100. Both calibrated SOC tracking and the
charger/recharge corrections require further work; merely waiting plugged in
does not establish a fix.

The ginkgo reference was inspected at
https://github.com/Huabin1010/ginkgo-mainline-linux . Its mainline overlay
`overlays/linux/drivers/power/supply/qcom-pmi632-qg.c` explicitly implements
a minimal gauge without the complete CAF SOC algorithm. It reads the monotonic
SOC register (0xbf) when available, otherwise linearly maps 3.4--4.35 V to
percentage. Stock Laurel `qg-soc.c` writes that register from software MSOC;
it is not an autonomous replacement for the missing SOC algorithm. Ginkgo's
overlay therefore does not provide a complete charger/recharge/learning port
to copy. Its different battery profiles must not replace Laurel's profiles.

The remaining gaps are persisted/calibrated SOC, FIFO/coulomb integration,
ESR/load compensation, new capacity learning and cycle updates, explicitly
configured recharge, Quick Charge/DPDM and the secondary charger. TCPM's
advertised 3 A also causes Android's maximum-current display to exceed the
actual 500 mA limit in this USB session. "Complete PMIC" is not the current
implementation status.

Battery-usage Settings crashed because its saved app-usage timestamp was
01:57:35 while the current phone clock was 01:52:24. The source guard in
`packages/apps/Settings` skips an inverted usage-query interval and preserves
the database. It is retained for fresh sync at
`rom-patches/settings/0001-battery-usage-skip-invalid-clock-period.patch`
relative to the kernel repo; apply with `git -C packages/apps/Settings apply`
using the patch's absolute path, once. This patch requires a ROM rebuild;
it does not repair RTC/timekeeping or validate a Settings runtime fix.

The previously imported gauge treated any supply as charging, assumed the 5 A
ADC range, and passed tenths of degrees into a Celsius lookup. Those errors
are corrected. It now uses the core-owned battery metadata instead of allocating
an unreleased duplicate. Supplier tables are separate driver-owned copies.

All five temperature columns of each stock pc-temp-v1-lut are converted exactly:
voltage units of 100 uV become uV, SOC hundredths of percent become integer
percent. QG uses the physical battery-ID ADC to select 68 kohm Sunwoda or
330 kohm Feimaotui within 20 percent. Other IDs use the stock fallback table.
These three data sets are from the same exact stock revision. This is not the
complete downstream QG hysteresis, coulomb integration, ESR compensation,
capacity learning, or aging model; percentage can vary under load/charging.
Do not claim stock fuel-gauge accuracy from this integration alone.

The 2026-10-09 device logs showed an abrupt 95-to-100 percent change when SMB5
terminated charging. The initial mainline code unconditionally returned 100
on Full. Stock `qpnp-qg.c:qg_charge_full_update()` instead requires tracked SOC
of at least 99 and good health; `qg-soc.c` scales ordinary SOC in small steps
with a 20-second minimum and rejects increases without charging input.
The mainline adaptation seeds reported SOC from the voltage estimate, limits
subsequent changes to one percentage point per 20 seconds, blocks increases
when the supplier is not Charging/Full, and only promotes 99 to 100 on Full
with good health. Critical estimates of 0/1 percent bypass the delay.
This is a reporting stabilization, not a port of stock's FIFO-dependent timing,
persisted MSOC, full-state hysteresis, or userspace coulomb/ESR algorithm.
It requires a maintainer rebuild and device validation, including unplugging
after termination and the transition from resting OCV to loaded voltage.

Stock stores eight cycle-count buckets at SDAM base 0xb100 offset 0x58,
learned capacity at 0x68 in mAh, and a validity magic 0x12345678 at 0x80.
The optional read-only SPMI NVMEM provider at b100 now exposes three cells to
QG. They are distinct from LPG's b600 SDAM. QG requires a present battery,
a recognized supplier profile, and the exact stock magic. It rejects erased
cycle buckets and nonpositive learned capacity or values outside stock's
50-to-150-percent nominal sanity range. Invalid/missing data returns ENODATA;
it never fabricates capacity or zero cycles as a fallback. NVMEM provider
deferral is propagated, and older DTBs without cells retain basic telemetry.

These are **saved stock records**, not new mainline measurements. Stock can
initialize learned capacity to nominal; the record has no flag proving a
learning session completed and no unique pack identity. A replacement with
the same supplier ID cannot be detected. Do not treat the resulting ratio as
validated battery wear. Mainline does not clear or write this SDAM, update
cycle buckets, or run stock's userspace-dependent learning algorithm.
Persistent pack-aware counting and new capacity learning remain unfinished.

After rebuilding the matching kernel/DTB, check `qg-battery/charge_full` and
`qg-battery/cycle_count` under `/sys/class/power_supply` and `dumpsys battery`.
Record the values alongside `charge_full_design` (4040000 uAh). Invalid
history should remain unavailable while voltage/temperature/SOC still work.
No build, NVMEM write, or hardware validation was performed for this addition.

## Stock Quick Charge is unfinished

Stock Laurel has HVDCP/Quick Charge and an SMB1355 secondary charger path.
PMI632 does not provide USB Power Delivery; the existing connector has
pd-disable. Stock's enabled authentication/DP-DM flow, USB PHY coordination,
QC2/QC3 voltage control, secondary charger and thermal voting are not supplied
by this driver. HVDCP enable, authentication and autonomous negotiation bits
are explicitly cleared. Charging therefore remains at a 5 V source.

Raising FCC to 3 A or observing TCPM's advertised 3 A does not prove 18 W QC.
The stock dpdm-supply points to the downstream QUSB PHY regulator interface;
the mainline PHY's vdda-phy-dpdm supply is an input rail, not that interface.
Do not connect them by name or force 9/12 V without implementing ownership,
source authentication and fallback. The remaining port must arbitrate DP/DM
with DWC3/USB, restore 5 V on errors/disconnect/thermal limits and validate
with a compatible adapter and independent USB power measurement. No Quick
Charge support or stock charging-speed claim is made for this build.

## Maintainer build and acceptance

Build/install matching kernel, external DTS, vendor and recovery-as-boot using
the normal documented ROM procedure. No erase/format is needed for this change.
Confirm the resolved kernel config contains both PMI632 symbols built in and
QCOM_SPMI_ADC5=y, and vendor contains the example health service rather than
Cuttlefish's service. The existing artifact verifier now requires both drivers.

Read-only collection after installation:

```sh
adb shell uname -a
adb shell 'cat /sys/class/power_supply/qg-battery/uevent'
adb shell 'cat /sys/class/power_supply/pmi632-charger/uevent'
adb shell 'dmesg | grep -iE "pmi632|qg|charger|therm|battery"'
adb shell dumpsys battery
```

If permission is denied, collect kernel logs using the established root/logger
procedure. Do not use dumpsys battery set or health debug overrides to simulate
successful measurements. Physically disconnect USB and inspect the phone's
battery UI; USB ADB disappearing is expected. Reconnect and verify status and
current change, sensible battery/charger temperatures, selected ID profile,
voltage, percentage and full/termination behavior. Repeat in recovery.

A desktop SDP connection must remain within 500 mA. Check CDP and a 5 V DCP
adapter separately with independent measurement; do not infer delivered power
from CURRENT_MAX. Check that stopping/temperature-read failures do not leave
charging enabled. Do not heat/cool the battery outside its operating range to
exercise protection. Preserve logs and the known-working recovery on regression.
