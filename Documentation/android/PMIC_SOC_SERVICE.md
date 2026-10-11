<!-- SPDX-License-Identifier: GPL-2.0-only -->
# PMI632 open SOC integration: source candidate, 2026-10-09

Latest SOC/completion candidate (2026-10-11): [PMIC_SOC_CONTINUITY.md](PMIC_SOC_CONTINUITY.md). It supersedes the historical filesystem persistence and read-only shutdown-state descriptions below. Source-only; maintainer build and validation required.

Latest correction: [PMIC_CHARGING_FIX.md](PMIC_CHARGING_FIX.md). The earlier
QC/parallel, sample timing and charge-counter limitations below describe the
previous candidate; the newer source remains unbuilt and unvalidated.

This is an unbuilt implementation candidate, not measured charging parity.
The kernel and complete ROM must be rebuilt together by the maintainer.
Kernel/ROM AGENTS.md explicitly prohibits agent builds, compiles, tests and
flashing. No live register writes or forced battery percentage were used.

## Failure evidence

Installed build #27 still reports battery 95%, Full, 4.335344 V, 0 uA;
the physical charger is online at 5.082352 V, with a 500000 uA input ceiling
and 222048 uA input current. The Type-C source advertises 3000000 uA; this
is not the actual charger limit. These readings demonstrate a stopped battery
charge, not just a stale Android battery broadcast. They do not distinguish
charge inhibit from termination without reading the raw charger state.
The previous voltage-only gauge also cannot measure fractional charge/SOC.

## Implemented source changes

- IRQ-latched QG V/I FIFO batches are converted with the stock PMI632 5 A
  scale, current sign and S2 sample timing. Positive nC means charge entering
  the battery; cumulative nC is converted to standard signed relative uAh
  for CHARGE_COUNTER. Property reads never integrate instantaneous current.
  Invalid batches and missed time windows increment a gap counter instead
  of inventing charge. The PMI632 has no PM6150 clock-adjust workaround.
- `/dev/qcom-qg` exports pointer-free version 1 snapshots and accepts SOC
  estimates from one privileged writer. Generation changes invalidate state
  after battery removal. Sequence checking rejects stale submissions. The
  kernel bounds estimates/FCC/cycles, rate-limits SOC movement, qualifies
  full-state inputs and expires the SOC producer after 30 seconds. It retains
  the last accepted SOC on service loss rather than reseeding from loaded
  voltage. CALIBRATE means a live stock-profile SOC producer; it does **not**
  assert laboratory calibration or five-point accuracy.
- `laurel-batteryd` runs without framework or userdata dependencies in Android
  and recovery. It integrates the hardware counter using fractional SOC and
  stock FCC temperature scaling, interpolates supplier OCV tables between
  temperatures, and uses only rested hardware OCV for slow drift correction.
  UI movement is at most one percentage point per 20 seconds. The service
  cannot set input current, float voltage, thermal limits or register addresses.
- Stock full hold requires normal temperature, good health, actual termination,
  normal 4.4 V float, >=99% modeled SOC, a preceding high-voltage taper and a
  minute of qualified termination. The taper-voltage observation is latched
  because VBAT relaxes after charging stops. Charge inhibit maps to
  NotCharging, and an unqualified terminated battery is not advertised Full.
- Stock inhibit-disable and FLOAT-as-SDP / 300 ms DCD policy are retained.
  VBAT recharge is explicitly set to active JEITA float minus 100 mV, using
  the stock big-endian ADC conversion and three samples. This **fallback
  threshold is local policy**, not Laurel's stock SOC threshold. With a live
  producer, the charger uses stock 99% SOC recharge and one sample; QG BF
  receives the scaled 0..255 SOC. The PMI632 recharge workaround restarts a
  terminated charger once per charging episode when SOC is <=99%, only if
  thermal supervision left charging enabled. Producer loss restores VBAT mode.
- Stock SDAM remains read-only. Valid learned FCC/cycles seed the service.
  New equivalent discharge cycles count measured gap-free charge throughput;
  this differs from stock's eight SOC bucket counter algorithm. New FCC
  learning requires an uninterrupted low-OCV-to-qualified-full session, good
  temperature, and plausible capacity. Adaptation is capped at 10% per session.
  `/metadata/laurel-battery/state` stores fingerprinted/versioned/checksummed
  state using fsync plus atomic rename; no `/data` dependency. Persistence
  failure keeps measurement running in memory. On replacement with the same
  supplier ID, an explicit state reset is required: there is no unique pack ID.
- Built-in power drivers remain in ACK; exported QG UAPI is a kernel-owned
  Soong header library. The open service, profile data and narrow SELinux
  rules live in the ROM tree. Existing bindings/DTS remain in the device-tree
  repository. Modules repository does not need a battery module.

## Source provenance

LineageOS android_kernel_xiaomi_sm6125, lineage-23.2:
`77d2912bc00eda19182a95a24bbe23543c792814`.
Magicxavi kernel_xiaomi_laurel_sprout, branch 16:
`a9b94eac2508f1639ac6697f3ffba490a688327e`.
The QG FIFO timing/sign, recharge conversions/workaround and full/direction
rules were compared against qpnp-qg.c, qg-util.c, qg-soc.c, smb5-lib.c and
qpnp-smb5.c. Qualcomm/Linux Foundation source provenance is retained in
pmic-import-history.json. Marc Lainez's donor driver headers are retained;
the open SOC algorithm here is a new adaptation, not his minimal OCV algorithm
or a recovered Qualcomm proprietary userspace algorithm.

`power/batteryd/import_profiles.py` regenerates exact stock discharge/charge
OCV and FCC tables from the pinned Lineage source. IDs are Sunwoda 68 kohm,
Feimaotui 330 kohm, plus stock fallback. Each source file has a SHA-256-derived
fingerprint. Charge curves are carried for model work; the current service
uses discharge/rest OCV plus coulomb integration, not an ESR model derived
from the charging curve. Ginkgo's voltage/BF fallback is not an SOC parity model.

Linux power-supply semantics/units:
https://www.kernel.org/doc/html/latest/power/power_supply_class.html
Original Qualcomm QG design (userspace data exchange):
https://android.googlesource.com/kernel/msm/+/e6b2f4a17ed594192172d63c215b388b1c3cc9c9

## Remaining work; do not call the entire charging plan complete

- QC2/QC3 negotiation, QUSB2 DP/DM ownership and SMB1355 parallel charging are
  **not implemented/enabled**. They need coordinated PHY/charger support,
  stock SMB1355 I2C/IRQ/enable/sense wiring and aggregate-current supervision.
  Merely turning on HVDCP bits or powering vdda-phy-dpdm is not equivalent.
- Partial FIFO/accumulator handling at suspend/state transitions, hardware ESR
  pulse measurement and the complete stock compensation model are not ported.
  Gaps invalidate learning. Suspend power support remains disabled in this ROM;
  full FIFO accounting does not establish complete suspend coulomb coverage.
- Accurate FCC/cycles and <=5 percentage-point SOC error need independent
  reference measurements. A preserved 3172 mAh / 971-cycle SDAM value is
  historical data, not proof of present usable capacity or completed learning.
- Source integration/SELinux policy and recovery coexistence require the
  maintainer build and device results. Static diff review is not compilation.

## Maintainer acceptance sequence

Build a complete kernel/DTB/modules/ROM/recovery set, then install with the
established device procedure. No format is required by this change. Confirm
`/dev/qcom-qg`, one running laurel-batteryd process, no ioctl/AVC errors and
periodic QG logs in Android and recovery. Capture gauge/charger uevents,
FIFO/OCV interrupt counts and service logs alongside USB-meter measurements.

1. Confirm input absence reports Discharging, and reconnect distinguishes
   charging, pause, inhibit and qualified full. A 3 A TCPM advertisement is
   not evidence of 3 A battery current. On SDP, retain the 500 mA limit.
2. Starting below full, verify positive battery current and measured counter
   progression, stable percentage increase, successful termination, and
   recharge below 99% with normal temperature. Record raw charger states.
3. Unplug/reboot/service restart/recovery must preserve continuity; service
   loss must stop SOC-controlled recharge and select the explicit VBAT fallback.
4. Calibrated discharge reference must show <=5 percentage-point SOC error.
   Verify gap-free samples before assessing cycle/FCC learning. Don't force
   percentages or write stock SDAM to make the UI look correct.
5. Compare normal/cool/warm/hot behavior to the pinned stock policy before
   relaxing the existing conservative current/temperature derating. QC and
   parallel-charge acceptance remain pending implementation.
