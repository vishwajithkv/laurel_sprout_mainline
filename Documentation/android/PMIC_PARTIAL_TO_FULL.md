# Mi A3 PMIC: original partial support, additions and remaining gaps

Latest source validation is recorded in [PMIC_BUILD_33.md](PMIC_BUILD_33.md).
The entries below retain earlier investigations; their unbuilt status refers
to the source at the time of each entry.

New source candidate, 2026-10-11:
[SDAM/RTC continuity and completion changes](PMIC_SOC_CONTINUITY.md).
The rejected filesystem SOC persistence is now removed from the source.
Kernel SDAM/RTC restore and charge-completion changes are implemented but
unbuilt and unvalidated. Historical descriptions below record previous stages.

Latest live 4.14 comparison (2026-10-11):
[stock PMIC findings](PMIC_4_14_LIVE.md), with captured logs
and live DT. Stock also fails SMB1355 probe. The other LLM guide's proposed
shutdown SDAM offsets and GPIO6 IRQ correction must not be applied as written.
Confirmed missing mainline work includes SDAM boot-state restore, enabled RTC
driver and reconciliation of live stock capacity versus historical SDAM FCC.
The comparison also now maps the three reported symptoms to downstream
full-charge hold, SOC smoothing, recharge coordination and input-capability
reporting. Mainline's separate completion latches and Full-to-NotCharging
conversion are a concrete reporting gap; physical restart oscillation still
requires synchronized mainline measurements. See the symptom table in the
linked report. Changing design capacity alone will not fix these symptoms.

Updated 2026-10-10. This is a handoff for reviewers and other agents working on
Xiaomi Mi A3 (`laurel_sprout`, SM6125/Trinket) with the custom ACK 6.18 kernel.
**PMIC charging parity is incomplete.** Real battery measurements and primary
USB charging work; the rapid-charge capability bug is corrected in build #32,
secondary charger probe fails, and near-full restart policy still needs
validation. Android/recovery SOC differs. The maintainer has rejected
`/metadata` SOC persistence as the solution; it is documented below as an
attempted implementation, not the accepted final architecture.

## How much PMIC support existed before this work?

There is no defensible numerical percentage: PMIC support includes separate
transport, regulator, GPIO, thermal, USB, charger and gauge functions. Existing
device boot support was substantial, but real Android battery/charging support
was absent at the initial audit. These must not be counted as the same thing.

The pre-PMIC baseline already included PM6125/PMI632 descriptions and the
infrastructure used to boot and power the board. This work did **not** create
the whole PMIC implementation from nothing.

| Area | Before the battery/charging completion work | Added during this work / current result |
| --- | --- | --- |
| PMIC transport and register access | Existing Qualcomm SPMI/MFD infrastructure | Reused for new power drivers; not replaced |
| Board power rails | Existing RPM PM6125 regulator descriptions and peripheral supply connections | Reused; this was not a complete regulator rewrite |
| PMIC GPIO and power-key/reset descriptions | Already present in baseline DT | Reused; SMB_EN/external-sense configuration added for charging |
| USB Type-C / VBUS | Existing PMI632 Type-C and VBUS nodes, with TCPM source power_supply observed | Kept existing ownership; charger/Health integration distinguishes source capability from actual charging |
| ADC / thermal infrastructure | ADC5, ADC_TM and PMIC thermal support present; independent VBAT/ID ADC readings possible | Added missing battery thermistor channel, physical battery-temperature wiring and charger measurement consumers |
| Primary SMB5 charging driver | No active physical charger power_supply in initial live audit | Added/adapted built-in PMI632 charger driver and stock-derived supervision |
| PMI632 QG battery driver | No active Battery power_supply in initial live audit | Added/adapted built-in gauge, supplier profiles, history decoding and real measurements |
| Android battery reporting | Cuttlefish bring-up Health returned fixed synthetic values | Existing standard AIDL Health now reads physical sysfs supplies |
| Usable SOC algorithm | No real Android gauge SOC integration | First OCV-only estimate, then FIFO charge integration/open estimator; accuracy and recovery parity still incomplete |
| QC2/QC3 / secondary charger | No validated mainline charging integration | Source implementation added; SMB1355 fails probe and adapter operation is unverified |
| RTC / reliable time | Not established as working; no RTC interface in observed boots | Still missing; recovery starts in 1970 |

The baseline DT was checked against committed HEAD of the split devicetrees
repository (`cca763670a8c2fa68865994119a18fa682d40778`), while the power
drivers/charging integration are dirty additions relative to kernel HEAD
`f3f89dff184418f4396aad561a4aca71304509d3`. Presence of a DT node or config
option does not independently validate each peripheral on hardware.

### Intermediate partial implementation before the completion attempts

After importing the minimal PMI632 drivers, but before the later SOC/QC/
parallel work, build #27 established physical battery presence, voltage,
signed current, temperature, primary charger status and saved stock FCC/cycles.
It still used voltage/OCV-based percentage and plateaued near 95%, while
hardware termination independently produced Full/Charged. Thus two baselines
matter: originally no real battery interface, then partial working telemetry
with an incomplete SOC/charging model. The later work extended that partial
implementation; it did not turn it into verified complete PMIC support.

## Partial-to-full work ledger

| Step | What was done | Outcome / unresolved issue |
| --- | --- | --- |
| Physical battery interface | Import/adapt Marc Lainez PMI632 SMB5 and QG support to ACK 6.18; built-in config and board wiring | Measurements observed; original donor credits preserved |
| Temperature/profile support | Add missing ADC5 thermistor channel, stock battery ID/profile/limits and OCV curves | 68-kohm profile and physical temperature observed |
| Existing Android Health | Replace fake bring-up HAL selection with default AIDL Health | Android uses physical supplies; no new AIDL service |
| Primary charge control | Stock-derived ICL/FCC/FV, BC1.2 limits, enable ordering, termination, safety timer, JEITA/thermal/watchdog supervision | Primary laptop charging observed; complete stock thermal/suspend parity unverified |
| Gauge history | Decode valid stock SDAM FCC and eight cycle buckets | 3172 mAh / 971 historical cycles observed; not proof of current accuracy |
| Gauge measurement model | FIFO current/time integration, 51.2 s programmed cadence, partial capture/gap accounting, validated raw UAPI | Cadence and fractional SOC observed; full compensation/discharge validation missing |
| Open SOC policy | batteryd integration, direction/UI guards, full endpoint qualification, bounded learning and cycle-accounting logic | Android reaches Full; recovery differs; learning accuracy unverified |
| Persistence attempt | Save open-estimator state to `/metadata`, then add recovery mount/clock handling | **Rejected by maintainer as unreliable; not accepted as final solution. Source attempt remains identifiable for review/replacement.** |
| Charge counter semantics | Expose remaining charge rather than signed charge-since-boot through standard CHARGE_COUNTER | Remaining counter observed; raw delta remains in UAPI |
| USB capability selection | Board exclusion property + existing Health entry-point parser + BatteryMonitor rescan fix | #32 reports physical 500 mA instead of TCPM 3 A |
| Recharge policy | Explicit VBAT/SOC threshold/sample programming, qualified-full/manual-restart gates; latest hardware SOC gate waits for full | Source corrected; prolonged near-full behavior still needs validation |
| QC/DPDM | Bounded DCP authentication/QC2/QC3 requests, measured-voltage/power bounds, QUSB2 DPDM ownership | Source added; wall-adapter/QC performance unverified |
| Parallel charging | Stock-wired SMB1355 driver, aggregate budget control, SMB_EN and QG external sense coordination | Probe fails -ENXIO; parallel path is not working |
| Regressions corrected | Normalize positive-success IIO returns; explicitly package Health property; honor rescan exclusions | #31 restored current; #32 corrected reported input capability |

The detailed sections below preserve implementation history and live evidence.
They describe progress **toward** full support, not a completed partial-to-full
conversion. Reliable SOC state ownership across Android/recovery, secondary
transport, QC validation and stock-equivalent compensation remain open work.

## Read this before changing anything

- Distinguish installed build evidence from working-tree source changes.
  Build #31 does not contain the latest BatteryMonitor rescan correction or
  the latest qualified-full gate for hardware SOC recharge.
- Read each repository's `AGENTS.md`. Builds, compiles, tests, flashing,
  formatting and partition operations are reserved for the maintainer.
  This investigation used read-only ADB, source inspection and static edits.
- Do not force percentages, falsify TCPM capability, disable thermal limits,
  or assume a kernel-only rebuild updates the vendor Health service.
- Keep charging control in the kernel. Use the existing standard AIDL Health
  service; no additional AIDL service was introduced. The existing open
  `laurel-batteryd` estimates SOC from the kernel's raw gauge measurements.
- Do not extend the `/metadata` persistence approach: the maintainer rejected
  it. The existing source and the recovery-mount/clock attempt are historical
  review inputs; this documentation update does not endorse or redesign them.
- Preserve donor authorship separately from local adaptations. Most PMIC
  changes here are still uncommitted working-tree files. A provenance inventory
  is not equivalent to authored commits already being published.

## Hardware, references and repository ownership

The hardware path is PMI632 SMB5 primary charger + PMI632 QG fuel gauge + ADC5
battery thermistor, with an optional SMB1355 parallel charger on I2C. Type-C
TCPM exposes a separate source capability supply. That supply is not a battery
gauge and its advertised current is not measured charging current.

Primary references:

| Reference | Revision / purpose |
| --- | --- |
| [LineageOS sm6125 4.14](https://github.com/LineageOS/android_kernel_xiaomi_sm6125/tree/77d2912bc00eda19182a95a24bbe23543c792814) | Laurel wiring, SMB5/QG policy, register definitions, SMB1355 initialization |
| [magicxavi Laurel branch 16](https://github.com/magicxavi/kernel_xiaomi_laurel_sprout/tree/16) | Compared revision `a9b94eac2508f1639ac6697f3ffba490a688327e` |
| [Marc Lainez PMI632 port](https://github.com/msm8953-mainline/linux/pull/242) | Mainline driver donor; inspected head `59ae383591619d8fb55176cbc89a84de1cdcfb8b` |
| Google ACK | `android17-6.18-2026-09_r5`, Linux 6.18.32; custom board integration, not a certified GKI binary |
| Ginkgo mainline | Secondary comparison only; inspected gauge fallback was not complete charging/SOC parity |

Stock checkouts available during this investigation: `/tmp/laurel-stock-power`
and `/tmp/laurel-magic-power`. These temporary directories may disappear.
Stock's key files are `qpnp-smb5.c`, `smb5-lib.c`, `smb5-reg.h`, `qpnp-qg.c`,
`qg-util.c`, `qg-reg.h`, `qg-soc.c`, `fg-alg.c`, `smb1355-charger.c`, and Laurel
`laurel_sprout-trinket-battery.dtsi` / `laurel_sprout-trinket.dtsi`. QG is the
correct gauge block; do not substitute FG3/FG4 behavior without checking it.

| Repository / path | Changes and responsibility |
| --- | --- |
| `kernel/mainline/sm6125-mainline-6.18` | Built-in power drivers, ADC5 thermistor support, QUSB2 DPDM ownership, QG UAPI, Kconfig and board configuration |
| `drivers/power/supply/qcom_pmi632_charger.c` | Source classification, current/voltage limits, thermal supervision, watchdog, termination/recharge, QC and parallel budgets |
| `drivers/power/supply/qcom_pmi632_qg.c` | Gauge measurements/FIFO, raw charge integration, history, validated estimate submission, sysfs battery interface |
| `drivers/power/supply/qcom_smb1355.c` | Stock-derived secondary initialization adapted to regmap/I2C and standard power_supply |
| `drivers/phy/qualcomm/phy-qcom-qusb2.c` | Optional legacy DPDM regulator ownership serialized with USB data PHY lifecycle |
| `include/uapi/linux/qcom_qg.h`, kernel `Android.bp`, `android/qg-uapi` | Existing batteryd's versioned raw-measurement ABI and header exposure |
| `kernel/mainline/sm6125-mainline-6.18-devicetrees` | PMI632 nodes, supplier battery profiles, SMB1355 I2C/IRQ/sense wiring, DPDM and bindings; ACK binding paths are compatibility symlinks |
| `device/xiaomi/laurel_sprout` | Standard Health selection, `power/batteryd`, profiles, init configuration, Health property and narrow SELinux labels |
| `hardware/interfaces/health/aidl/default/main.cpp` | Existing Health service reads the board's supply-exclusion property |
| `system/core/healthd/BatteryMonitor.cpp` | Latest fix: honor exclusions during periodic supply rescans as well as initialization |
| Kernel modules repository | No PMIC module added; charging dependencies stay built in for recovery and Android |

Authored donor imports are inventoried in
`pmic-import-history.json`.
Local changes must not be attributed to Marc or Qualcomm merely because they
adapt those sources. Copyright/license headers remain intact.

## What was implemented, and why

### 1. Replace fake Android battery reporting

Initial bring-up selected Cuttlefish's Health HAL, which hardcodes charging,
85%, 3.6 V and 25 C. Only TCPM's source supply existed; Android was not reading
a physical battery. The device configuration now selects
`TARGET_HAS_BATTERY=true` and `TARGET_HEALTH_HAL=default-aidl` before optional
configuration. A full ROM rebuild is required to replace the installed HAL.

### 2. Add physical charger, gauge and temperature support

Marc Lainez's minimal PMI632 mainline drivers were adapted to ACK 6.18 and
Laurel's stock wiring. ADC5's missing battery-thermistor channel was added.
Battery ID around 67.6–67.9 kohm selects the stock 68-kohm Sunwoda profile.
Stock supplier OCV curves, design capacity 4040 mAh and thermistor configuration
are carried in the device-tree/profile integration.

Real voltage, signed battery current, temperature, presence and primary charger
status now reach sysfs and Android. Positive Linux battery current means charge
entering the battery. USB online alone never proves battery current is positive.

### 3. Replace voltage-only percentage with measured charge integration

The early OCV-only implementation stayed near 95% and could say Full/Charged at
95%. Loaded voltage and old power-on OCV were not a reliable coulomb counter.
QG FIFO samples now use the stock PMI632 current scale/sign and real sample
durations. Programmed S2 timing is four FIFO entries × 128 accumulations ×
100 ms = 51.2 seconds, with readback. Earlier inherited timing was 307.2 s.
Partial FIFO/accumulator capture handles suspend and external-sense transitions;
missing intervals are counted as gaps rather than invented charge.

The existing open batteryd handles SOC integration, profile/temperature
correction, persistence, bounded UI movement (1% per 20 s), qualified full and
bounded learning. Kernel estimate submission checks ABI, generation, sequence,
freshness and bounds. Standard `CHARGE_COUNTER` now means estimated remaining
charge (`SOC × learned FCC`); raw relative nano-coulombs remain in the ABI.
Stock QG also depended on userspace estimation; complete proprietary ESR and
impedance compensation has not been reproduced.

Saved stock SDAM decoded to 3172 mAh learned capacity and 971 cycles. These are
historical values, not a new laboratory measurement of battery wear. The open
estimator includes bounded learning/cycle accounting, but those require valid
sessions and are not demonstrated by these nearly-full USB observations.
`health=Good` means no reported charging fault; it is not lifetime health.

### 4. Adapt charging protection and source limits

Stock-derived ceilings are 4.4 V battery float, 3 A aggregate battery current
and 2 A board input current, subject to source/AICL/thermal limits. SDP laptop
USB stays at 500 mA, CDP at <=1.5 A, and classified DCP may use the board ceiling.
Unknown/float sources remain conservative. Primary input register ordering,
termination threshold and the 768-minute safety timer follow stock references.

Hard JEITA comparators remain enabled. Software reduces float to 4.1 V above
44 C, derates current in cool/warm or high die-temperature conditions and stops
outside safe bounds. Failed required ADC/thermal reads disable charging.
The supervised worker pets the watchdog, and a plugged-in wake source preserves
thermal polling. This has a power cost and is not final suspend/off-state parity.

QC2/QC3 requests are restricted to authenticated DCP and safe temperatures.
QC2 requests 9 V, never 12 V. QC3 uses bounded pulses from a measured 5 V baseline.
ADC checks constrain VBUS to 4.4–9.5 V and input power to <=18 W; failed
negotiation requests 5 V and retains conservative limits until measured safe.
No USB PD charging policy was added. Xiaomi's published 18 W QC3 capability
does not imply 18 W draw from a laptop or near-full battery.

SMB1355 integration uses stock address 0x0c, TLMM130 interrupt, PMI632 GPIO2
SMB_EN and GPIO7/8 external sense. Primary current is reduced before assigning
secondary current; aggregate limits, error shutdown and QG sense transitions
are coordinated. Missing secondary hardware does not prevent QG/primary probe.
**Implemented source is not evidence that this secondary path works.**

## Bug history and cause versus correction

| Symptom | Cause / evidence | Correction and current confidence |
| --- | --- | --- |
| Always charging with implausible fixed values | Cuttlefish fake Health HAL | Standard existing AIDL Health; real readings verified |
| Question mark / missing battery fields | No physical battery power_supply before drivers/integration | PMI632 QG + primary charger + ADC5; real fields now present |
| Boot/unplug percentage jumps, stuck 95%, Charged at 95% | OCV-only estimate and independent hardware termination status | FIFO integration and qualified full; partial success, not full estimator parity |
| Very slow SOC updates | Inherited 307.2 s FIFO batches | Explicit 51.2 s configuration observed in #31 probe logs |
| Build failures in charger/gauge port | Kernel API adaptation, including removed `no_llseek` | Port adapted; maintainer builds supplied, no agent build claim |
| #30 NotCharging / state 7 / `Charging update failed: 1` | Successful processed IIO reads returned `IIO_VAL_INT` (1), treated as error | Normalize nonnegative IIO success; #31 positive current verifies recovery |
| Exclusion property absent in #30 | Common configuration already set TARGET_VENDOR_PROP, legacy vendor.prop was not included | Explicit `power/health.prop` append; property present in #31 |
| Rapid label on 500 mA laptop USB | TCPM advertises 3 A; BatteryMonitor rescan ignored configured exclusions | Latest `system/core` rescan patch; not yet installed/validated |
| Repeated taper/termination at 98–99% | Logs confirm cycling; early selection of 99% hardware SOC recharge is a source-level likely cause | Latest source keeps VBAT recharge until qualified full; not yet validated |
| Secondary unavailable | `qcom-smb1355 0-000c: error -ENXIO: SMB1355 ID unavailable` | Unresolved transport/probe problem; exact wiring/enable/address cause not established |

## Latest connected-device check: 2026-10-10

Device `5781707fda83`, kernel
`6.18.32-gf3f89dff1844-dirty #31`, laptop USB, not a wall QC adapter.
Device wall-clock log timestamps are inconsistent around boot; use uptime and
sample counters for ordering, not the displayed calendar time alone.

First capture:

- Android: USB powered, level 99, status 4 (NotCharging), maximum input
  3,000,000 uA at 5,000,000 uV; temperature 33.2 C.
- Primary: Full/termination, online=1, SDP, physical limit 500,000 uA,
  measured input 196,420 uA at 5,087,536 uV: approximately **1.00 W**.
- QG: initially capacity 100, NotCharging, zero battery current, 4.373493 V,
  Good health, learned FCC 3,172,000 uAh, design 4,040,000 uAh, 971 cycles.
- TCPM: online=1, advertised 3 A at 5 V. Board exclusion property exists.
- SMB1355: same -ENXIO ID probe failure. No new IIO `Charging update failed: 1`
  error appeared in the filtered capture.

The samples are sequential, not atomic. The initial Android 99 / QG 100 readings
must not be interpreted as simultaneous exact fractional SOC. A later snapshot
showed Android 100 / NotCharging with counter 3,171,048 uAh (consistent with an
accepted estimate just below 100), before the qualified-full log arrived.

At uptime around 180–200 seconds, logs advanced to:

```text
QG soc=100.00 full=3172000 cycles=971 I=0 V=4373493 state=5 ... flags=3
```

Final read confirmed Android **100 / Full** (status 5, capacity level Full),
counter 3,172,000 uAh, QG Full, primary Full, USB online=1, battery current=0.
This is expected termination once qualified, not proof of a cable disconnect.
The latest short observation did not reproduce sustained cycling, although an
earlier #31 boot repeatedly alternated state 4/5 at modeled 98.24–98.60%.
The false 3 A capability remained throughout the check.

The currently checked-out batteryd requires prior high-voltage taper for full
qualification. This fresh boot's collected logs lack that taper yet eventually
show qualification. Do not assume the installed dirty #31 userspace exactly
matches current source. Preserve this source/artifact mismatch as an unresolved
verification point; early 100/NotCharging alone does not prove a rounding bug
or permanently failed qualification. No new estimator change was made in this
documentation/check pass.

## Latest source-only fixes to preserve

1. `hardware/interfaces` patch:
   `Documentation/android/rom-patches/health/0001-health-select-physical-charger-supplies.patch`
   in the kernel repo. Reads comma-separated exact supply names from
   `ro.vendor.health.ignore_supply_names` into the existing Health config.
2. `system/core` patch:
   `Documentation/android/rom-patches/health/0002-health-honor-supply-exclusions-on-rescan.patch`.
   `BatteryMonitor::updateValues()` previously cleared and repopulated charger
   names without the exclusions respected by `init()`. The periodic scan now
   uses the same exclusion list. Unconfigured devices retain their behavior.
3. Board property excludes exactly
   `tcpm-source-psy-1c40000.spmi:pmic@2:typec@1500` through `power/health.prop`,
   explicitly appended in BoardConfig, with narrow property SELinux access.
4. Primary recharge policy now calls `smb5_set_recharge()` with
   `soc_ready && full_latched`, rather than `soc_ready` alone. Calibration means
   a live producer, not proven full. Before qualified full, keep three-sample
   VBAT recharge; manual SOC restart also requires prior qualified full.

The rescan and recharge corrections require matching full ROM/kernel rebuilds
by the maintainer. No claim that either has fixed the running device yet.

## Remaining work and evidence needed

1. Validate artifact parity: kernel, DTB, Health binary, batteryd and board
   properties must come from the same candidate. Check actual vendor binary
   identity before explaining runtime behavior using newer dirty source.
2. Validate the Health correction on laptop SDP: BatteryService must report the
   primary <=500 mA input limit, not TCPM's 3 A. Preserve TCPM's honest capability.
3. Observe qualified full and recharge over time. Keep primary online, raw state,
   QG fractional SOC/full flag/current/voltage, float voltage and temperatures
   together. Distinguish hardware termination, thermal pause, physical detach,
   estimate expiry and forced/hardware restarts; do not hide them with UI status.
4. Resolve SMB1355 -ENXIO against both stock kernels: verify SE1 I2C controller,
   pinctrl, address, 16-bit regmap byte ordering, power/clock/enable sequence and
   probe timing. -ENXIO identifies transport failure, not its exact root cause.
   Do not blindly alter registers or claim a precompiled `.so` issue.
5. At moderate SOC, test a known QC adapter/cable and independently measured
   input power. Capture authenticated negotiation, actual VBUS/input current,
   primary/secondary limits, battery current and temperatures. Laptop USB cannot
   validate QC, parallel operation or stock 18 W capability.
6. Validate discharge, persistence, cycle/FCC learning, temperature boundaries,
   ADC/I2C failure handling, unplug/replug, suspend/resume and recovery/off-state
   charging before declaring complete PMIC parity.

A 4.14 ROM comparison can provide useful hardware baseline measurements,
especially SMB1355 enumeration and near-full recharge policy, but was not
flashed for this check. The user offered it; no flash was requested or performed.

Further details and earlier-stage limitations are in the kernel's
`Documentation/android/PMIC_BATTERY.md`, `PMIC_4_14_COMPARISON.md`,
`PMIC_SOC_SERVICE.md` and `PMIC_CHARGING_FIX.md`. Some earlier sections describe
superseded candidates; use dated live evidence and current source to resolve
contradictions rather than treating all historical limitations as current.

## Build #32 follow-up: Android 100%, recovery reportedly 97%

The maintainer reports a 97% recovery reading versus 100% in Android on the
latest build. Direct ADB currently reaches normal Android, identified by
`skip_initramfs`, the running vendor batteryd and Android Health service.
Kernel is #32, built 2026-10-10 00:18:53 IST. QG reports 100 / Full and the
estimator logs accepted 100.00 with flags=3. BatteryService now reports
500000 uA maximum input, confirming the Health rescan correction works in
Android. SMB1355 still fails ID probe with -ENXIO.

Recovery's 97% has not yet been captured directly. Current source packages
`laurel-batteryd_recovery` at `/system/bin/laurel-batteryd` and starts it from
`init.recovery.laurel_sprout.rc` on boot. Both variants use
`/metadata/laurel-battery/state`, but recovery does not explicitly ensure that
metadata is mounted or create the state directory before starting the service.
Record restore also rejects future timestamps and records over 30 days old;
the PMIC RTC is unavailable in the observed Android boot. Thus missing/late
metadata, a missing recovery estimator, or differing recovery wall time are
plausible explanations for falling back to a voltage/profile seed. None is a
confirmed root cause until recovery mounts, services, logs and clock are read.

Do not force recovery to display 100 or assume its 97% is the accurate value.
Capture recovery build identity, QG capacity/calibrate/current/OCV, estimator
service state, metadata mount/state availability and estimator restore errors.
The existing shared estimator should provide consistency once its startup and
state availability are validated; no second algorithm or AIDL service is needed.

### Recovery directly inspected, same build #32 — persistence attempt rejected

At uptime 37 s and again at 101 s, ADB identified recovery. The recovery
estimator was running, but `/proc/mounts` contained no metadata mount and
`/metadata/laurel-battery` was inaccessible. Recovery wall time was
1970-01-01, with no initialized RTC. QG reported 99 / NotCharging,
CALIBRATE=1, zero battery current, 4.372325 V, OCV 4.371741 V and
CHARGE_COUNTER=3130446 uAh with FCC=3172000 uAh. This is approximately
98.69% estimated charge, rather than Android's persisted qualified 100%.
The primary reported Full and USB online, SDP limit 500 mA.

Confirmed startup gaps: recovery cannot load the shared record without
metadata, and its unset wall clock rejects Android's future-dated record.
Latest source fixes explicitly mount only metadata in recovery before boot
services, and prepare the state directory only when metadata is mounted
(device identity differs from ramdisk root). A narrow named SELinux directory
transition supports first recovery use. Failed mounts do not create a state
record on ramdisk; no userdata mount or format was introduced.

The estimator checks record age only when both timestamps are meaningful
(>=2020), retains profile/checksum/value/OCV plausibility validation, and
preserves the last meaningful timestamp when saving from an unset-clock boot.
Record age cannot be independently established without a working clock;
this is an explicit limitation, not permission to ignore battery/profile
validation. Restoring SOC does not force the qualified-full flag: actual
full-status qualification still runs in each boot. A restore log was added.
These changes were unbuilt when proposed. The maintainer subsequently rejected
using `/metadata` for SOC persistence, so the mount/clock changes must not be
presented as the approved fix or evidence of full PMIC support. They remain in
the working-tree source for review; this turn changes documentation only.
No live mount, forced percentage, service restart or flash was performed.
