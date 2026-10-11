<!-- SPDX-License-Identifier: GPL-2.0-only -->
# PMI632 charging correction — 2026-10-09

Latest SOC/completion candidate (2026-10-11): [PMIC_SOC_CONTINUITY.md](PMIC_SOC_CONTINUITY.md). It supersedes the historical filesystem persistence and read-only shutdown-state descriptions below. Source-only; maintainer build and validation required.

Source candidate, unbuilt and not validated on hardware. Rebuild the kernel,
DTB and complete ROM together. No live register writes, forced percentages,
compiles, tests or flashing were performed by the agent. Repository AGENTS.md
reserves those operations for the maintainer.

## Installed-build evidence and cause versus fix

Build #29 (6.18.32-gf3f89dff1844-dirty) had a working QG FIFO, but inherited
307.2-second batches. SOC moved from 96.41 to 96.68%, then Android eventually
reached 98%. This is slow measured progress, not proof of a dead charger.
Earlier readings: VBUS 5.043 V, input limit 500 mA, input current 323 mA,
battery 4.416 V / +138 mA, temperature 32 C. Input power was approximately
1.63 W and net battery power 0.61 W. Near-full taper contributes to that gap.

The last ADB read showed level 98, 4.415 V, 32.1 C, Charging, physical limit
500 mA, while the duplicate TCPM source advertised 3 A at 5 V. BatteryService
selected 3 A; its charge counter was 82148 uAh (relative charge since boot).
These readings describe the installed build, not the new source below.

| Observed or source problem | Implemented correction |
| --- | --- |
| Gauge retains bootloader sample timing | Program FIFO=4, accumulation=128, interval=100 ms under master hold; verify readback; nominal batch 51.2 s |
| Completed FIFO only; transitions lose partial samples | Capture real-time FIFO/24-bit accumulator at suspend and parallel-sense changes, restart FSM, count gaps rather than invent time |
| SOC seed remains below 99% despite qualified physical termination | Existing batteryd may anchor to full after observed high-voltage taper, valid FIFO, normal temperature, real termination for 60 s and a plausible >=90% model; retain 1%/20 s UI rate limit and publish Full only at displayed 100% |
| Repeated restart of every terminated battery below 99% | Require an earlier qualified Full latch before the once-per-episode recharge workaround |
| Android chooses TCPM 3 A rather than physical 500 mA | Reuse existing AIDL Health service and existing ignorePowerSupplyNames config; board property excludes exactly the duplicate TCPM source |
| Relative boot counter exposed as remaining charge | Standard CHARGE_COUNTER returns SOC × learned FCC; raw signed nC remains in the existing QG measurement interface |
| QC/DPDM and SMB1355 absent | Add bounded authenticated DCP QC2/QC3, legacy QUSB2 DPDM ownership, stock-wired I2C secondary charger, primary-controlled aggregate budgets and QG external-sense transition |

## Charging policy and source comparison

The phone supports 18 W QC3; its included adapter is 10 W. Xiaomi specifies:
https://www.mi.com/uk/mi-a3/specs/ . GSMArena's review was also requested:
https://www.gsmarena.com/xiaomi_mi_a3-review-1964p3.php (fetch returned 403).
18 W is an upper bound, not expected battery power at 96–100% SOC.
Android's rapid/normal label uses advertised input capability; it is not a
wattmeter and can still say rapid while a genuine QC source is tapering.

Stock LineageOS sm6125 revision 77d2912bc00eda19182a95a24bbe23543c792814
and magicxavi branch 16 revision a9b94eac2508f1639ac6697f3ffba490a688327e
were compared. The inherited Marc Lainez driver credits remain intact.
New policy/glue is a local adaptation, not an upstream or donor cherry-pick.
SMB1355 register initialization and Qualcomm/Linux Foundation copyright are
retained from stock. PHY ownership follows stock phy-msm-qusb.c, adapted to
generic PHY/regulator lifetimes; regulator and data-PHY users are serialized.
Do not conflate the new DPDM ownership regulator with its analog power rail.

QC is requested only for classified DCP sources with successful temperature
readings in the normal band. SDP stays at 500 mA, CDP at <=1.5 A. Unknown and
float sources remain conservative. QC2 requests 9 V, never 12 V. Manual QC3
starts from a measured 5 V baseline, uses at most twenty 200 mV pulses, and
stops at the measured high-voltage target. Switcher frequency is changed
before upward voltage requests. Negotiation has bounded retries; failures
request 5 V and keep conservative current until ADC confirms the voltage.
Independent ADC checks reject VBUS outside 4.4–9.5 V and enforce <=18 W input.
Actual attainable power is further bounded by register quantization and
thermal/current limits. No USB PD policy is added.

The secondary uses stock I2C address 0x0c, TLMM130 active-low IRQ, PMI632
GPIO2 SMB_EN and GPIO7/8 external sense. GPIO1 CTM is disabled as on Laurel;
SMB1355 hardware die-temperature mitigation and watchdog remain enabled.
Primary current is reduced before the secondary receives its portion; total
FCC <=3 A and FV <=4.4 V. The primary independently drives SMB_EN low before
reconfiguring/disabling the secondary, including I2C-error paths. Secondary
thermal or bus faults stop its path. An unchanged healthy split is retained
across watchdog polls; taper or reduced thermal budgets remove the split.
The secondary is UNKNOWN supply type so Health cannot count it as another USB
source. Missing secondary hardware does not defer the battery gauge forever.

## Repository ownership and reproducibility

- ACK kernel: built-in PMI632 charger/QG, built-in SMB1355, shared PHY changes,
  QG UAPI and config. No battery module is added to the modules repository.
- Devicetrees: board I2C/IRQ/sense/DPDM wiring, sample configuration and power /
  extended PHY bindings. ACK binding paths are compatibility symlinks.
- ROM: existing batteryd qualification correction and property/SELinux config.
- hardware/interfaces: small change to the existing AIDL Health entry point;
  preserve rom-patches/health/0001-health-select-physical-charger-supplies.patch.
  Recovery prop.default already concatenates vendor build.prop, carrying the
  same selection. No AIDL interface, HAL instance, service or binary is added.

Battery estimation still uses the existing open batteryd: the stock QG SOC
algorithm was proprietary userspace too. Charging limits, QC, thermal policy,
parallel budgeting and measurement remain in the kernel. This does not claim
full ESR/impedance compensation parity, laboratory-calibrated capacity, or an
accurate new cycle count. The saved 3172 mAh / 971 cycles are historical SDAM.

## Maintainer acceptance after the complete build

Confirm the resolved config selects all three built-in power drivers and
that kernel, DTB, Health binary and ROM property match this source candidate.
Check probe logs for S2 sampling parameters, SMB1355 ID/initialization, absence
of transport/probe errors, and the exact TCPM exclusion. Verify battery reads
in Android and recovery, cable unplug detection and USB data/sideload.

With a PC/SDP source, BatteryService should select the physical <=500 mA
limit, not 3 A. At moderate SOC with a known QC adapter/cable, record VBUS,
input current, primary/secondary current limits, temperature, battery current,
FIFO cadence and fractional SOC. Confirm voltage rises only after QC auth;
aggregate input power/FCC must respect the bounds above. Then observe taper,
qualified full and unplug/recharge without repeated enable toggles. Test
warm/cold, lost ADC/I2C, failed QC authentication and suspend/resume paths
before claiming stock charging parity. Preserve logs rather than adjusting
percentage to make the UI appear correct.

## Build #30 follow-up: charging disabled at 99%

Live ADB confirmed NotCharging, primary state 7 (disabled), battery current
0 uA, 4.367 V, 32.8 C, Good health and repeated `Charging update failed: 1`.
QG sample time progressed in 51.2-second batches; gauge configuration worked.
The newly added VBUS/QC checks incorrectly assumed that successful processed
IIO reads return zero. ADC5 returns IIO_VAL_INT (1). The source now normalizes
all charger processed-IIO reads to zero on success, preserving negative errors.
This is an introduced regression, not evidence of normal end-of-charge taper.

The Health exclusion property was also missing on build #30: common already
sets TARGET_VENDOR_PROP, which suppresses the default legacy vendor.prop file.
The board now explicitly appends power/health.prop to TARGET_VENDOR_PROP.
No unused legacy camera/fingerprint properties are newly included.
Both corrections remain unbuilt and require a matching full-ROM rebuild.

## Build #31: laptop USB label and termination cycling

Live inspection confirmed that the IIO correction restored charging and the
Health exclusion property is present. The laptop source is SDP, limited to
500 mA: measured input was 204693 uA at 5.091 V (about 1.04 W), with battery
current 94451 uA at 4.415 V. BatteryService nevertheless reported TCPM's
3 A / 5 V capability. BatteryMonitor::updateValues() clears and rescans its
charger list without honoring ignorePowerSupplyNames, unlike init(). The
system/core correction is carried as rom-patches/health/0002 and requires a
complete ROM rebuild; the running build still has the faulty rescan.

Logs show repeated state 4 (taper) / state 5 (termination) transitions with
USB online, while modeled SOC slowly advanced from 98.24 to 98.60. The
charger selected stock's 99% hardware SOC recharge comparator as soon as
the gauge was calibrated, even before qualified full. This could restart
charging immediately after termination while SOC remained below threshold.
The candidate now keeps three-sample VBAT recharge until qualified full;
the existing manual recharge workaround also requires qualified full. This
is a source correction, not yet proof that all near-full cycling is resolved.
Reference: stock qpnp-smb5.c recharge configuration and qpnp-qg.c's
charge_full-gated QG_RECHARGE_SOC_WA in /tmp/laurel-stock-power.

SMB1355 still reports -ENXIO at ID probe on this build. Secondary charging
and wall-adapter QC/18 W operation remain unvalidated. Do not infer adapter
performance from the laptop connection or the UI label. No new AIDL service
was added; no build, flash, or live register writes were performed here.
