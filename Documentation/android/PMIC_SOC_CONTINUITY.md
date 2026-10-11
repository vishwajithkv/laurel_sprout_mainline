<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Laurel SOC continuity and charge completion candidate, 2026-10-11

Source-only; no build, test or flash performed. Build a complete matching ROM,
kernel and DTB, including recovery, before maintainer validation. This candidate
addresses SOC continuity and completion presentation; it is not complete PMIC
parity, a validated 18 W result, or a replacement for capacity/ESR model work.

## Verified reference and provenance

LineageOS android_kernel_xiaomi_sm6125 `lineage-23.2`, revision
`77d2912bc00eda19182a95a24bbe23543c792814`, fetched and inspected directly.
Reference paths: `drivers/power/supply/qcom/{qpnp-qg.c,qg-sdam.c,qg-reg.h,
qg-soc.c,smb5-lib.c}` and Laurel's `laurel_sprout-trinket-battery.dtsi`.
The reference implements SDAM/RTC restore, hold-soc-while-full, linearize-soc,
and SOC 99 recharge. Qualcomm/Linux Foundation and Xiaomi policy attribution
is retained in the driver; Marc Lainez's driver authorship remains intact.
These are new 6.18 adaptations, not unchanged cherry-picks of downstream code.

## Changes

- Enable built-in PM8xxx RTC and the actual bringup DT RTC node. Native and
  recovery profiles inherit that node. No Android wall-time prerequisite.
- Add named NVMEM cells for validity at b100+0x46 and 17-byte shutdown payload
  at +0x47..0x57. Validate magic, record validity, SOC 0..100, hardware RTC age
  <=30 days without clock reversal, temperature difference <=10 C, OCV range,
  known physical supplier profile and PON OCV SOC agreement within 10 points.
  The mismatch bound is intentionally tighter than downstream's 40 points.
  Rejection falls back to OCV and logs why the restore path was rejected.
- Save accepted SOC on integer/full-state changes, every 60 seconds, and in
  the platform shutdown callback while the producer is fresh. Invalidate
  first, write payload, publish Valid last. Preserve resistance, learned FCC,
  cycle buckets, ESR, magic and PBS-owned locations. Store measured hardware
  OCV in the OCV field, not loaded VBAT. Normalize NVMEM's positive successful
  byte count; unlike regmap, success is not zero.
- Snapshot flags advertise restored SOC and held completion without changing
  the ABI layout or ioctl numbers. Seed both the existing estimator and UI
  from accepted/restored SOC. On restart within one boot, use the kernel's
  held flag. Across boots, SOC 100 is restored but completion is requalified
  against hardware and thermal state; the stored percentage alone is not
  proof of current completion.
- Accept a stable normal-temperature termination at modeled SOC >=99 without
  requiring a taper observation from before this boot. A lower SOC still
  needs the existing measured high-voltage taper and FIFO evidence. Keep
  the 60-second qualification interval and kernel measurement checks.
- Hold displayed full while underlying coulomb accounting continues; use
  120-second steps away from the top end, following downstream maintenance
  cadence. Clear completion for unplug, thermal/fault pause, SOC below 99,
  or a new active high-current recharge. Do not repeatedly anchor underlying
  SOC to 100 on every terminated heartbeat.
- Match Laurel's pending charge-done presentation only for healthy,
  normal-voltage termination at accepted SOC >=99: Charging until qualified
  Full. This denotes charge-cycle progress, not instantaneous positive current.
  Disabled/paused/inhibited/fault states are never converted to Charging.
  Qualified full survives short low-current TERM/TAPER transitions. Existing
  primary recharge latch/one-shot guard continues to consume gauge status.
- Remove filesystem record code, its SELinux permissions and the recovery
  metadata mount introduced solely for that record. No new AIDL service.
  Existing standard Health selection still excludes the TCPM source supply;
  laptop USB current capability remains the primary charger's input limit.

## Limits

SDAM records persist integer displayed SOC; fractional integration restarts
from that seed (up to rounding error). A service restart uses the last accepted
SOC, not a separate persisted fractional counter. Newly learned FCC and cycle
estimates are not written to stock history; original history is still read-only
in driver policy. No unique battery serial is available: physical removal
invalidates the record, but replacement outside running-kernel observation
cannot be identified solely by matching supplier ID. RTC reset, large OCV/SOC
mismatch or thermistor failure rejects restore rather than trusting old SOC.
A build with older DTB has no new cells and cannot provide this continuity.

The current selection uses board RTC `rtc0`; validation must confirm it is
PM6125 and retained through reboot. The driver does not initialize stock magic
or manufacture history if it is absent. Recovery and Android share the same
kernel and estimator semantics; this needs live confirmation.

## Maintainer validation

Use the normal complete ROM build/install procedure. No data format is needed
for this change. Confirm `rtc0` exists and PM6125 RTC binds. Record Android's
percentage, then reboot to recovery and back with the same USB connection.
Look for `Restored SDAM SOC ... age ... seconds` in dmesg and
`Kernel SOC seed ... (SDAM)` in the estimator log. Missing RTC or rejected
record must be investigated rather than overriding SOC by hand.

On laptop USB, compare BatteryService max current/voltage with primary charger
current_max/voltage_max; 500 mA at 5 V is 2.5 W input capability, not 18 W.
Capture gauge status/capacity/current/voltage/temp alongside primary status,
input limit and actual input voltage/current around termination. Test unplug,
replug and a wall adapter separately. Expect legitimate taper at high SOC;
verify no repeated force-recharge loop and no immediate 100-to-95 jump.
If labels still toggle, distinguish actual hardware pause/termination from
presentation using those paired observations. Do not force Charging to hide
thermal protection or genuine lack of charge entering the battery.
