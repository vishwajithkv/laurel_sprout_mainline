<!-- SPDX-License-Identifier: GPL-2.0-only -->
# First 6.18 boot: maintainer validation

Target: match the 6.15 Android boot milestone before enabling other hardware.
No 6.18 build or boot result is established yet. Builds and installation are
performed by the maintainer; collect the results before claiming success.

1. Build a complete 6.18 artifact set with the documented LLVM toolchain.
   Run verify-artifacts.py against the ROM outputs. Stop on missing built-ins,
   wrong boot layout, mismatched DTB/modules or partition-size failures.
2. Preserve a known-working recovery, the working slot and partition backups.
   Confirm the test slot and its DTBO state. The existing mainline setup used
   erased DTBO; do not substitute the downstream overlay or the earlier custom
   no-op image. There is no automatic flash, erase or data-format command here.
3. Install the matching ROM package using the established device procedure.
   First enter recovery. Record `uname -a`, `/proc/cmdline`, storage discovery,
   filesystem/metadata access and USB ADB operation. Recovery must identify 6.18.32.
4. Boot normal Android. The bootloader's exact `skip_initramfs` flag must select
   first-stage mounting through the stored system/core patch; recovery selection
   must still work when that flag is absent. Keep `avb=vbmeta` in the fstab.
5. Capture current and previous-boot logs through the existing metadata logger,
   logcat and pstore when available. Check system/vendor/metadata/data mounts,
   cgroup setup, BPF loading, service startup and filesystem security labels.
   A recovery boot by itself does not validate normal Android first-stage mounts.
6. Confirm `adb shell getprop sys.boot_completed` reports `1`, boot remains
   stable and `adb shell am start -a android.settings.SETTINGS` opens Settings
   through `scrcpy --no-audio`. Check for recurring fatal services or a reboot.

Retain the current unencrypted userdata profile, permissive bringup policy,
software graphics and disabled optional hardware. Do not format data to hide
a regression. A usable physical display, touch, OTG host operation, native GPU,
encryption, networking and other HALs are subsequent milestones.

On failure, keep the logs and identify the first new fatal error relative to
6.15. Fix that specific regression and rebuild a coherent kernel/DTB/module
set. Restore the known-working images if recovery or normal-boot diagnosis is
lost; do not exhaust A/B retries with repeated blind boots.
