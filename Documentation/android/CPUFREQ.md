<!-- SPDX-License-Identifier: GPL-2.0-only -->
# SM6125 CPU frequency bringup

Status: maintainer-built kernel validated in normal Android on 2026-10-05.
Both qcom-cpufreq-hw policies use schedutil and record frequency transitions.

## Reason for the change

Recovery exposed no cpufreq policies despite ARM_QCOM_CPUFREQ_HW=y. The SM6125
DT did not describe the OSM controller or CPU frequency domains. During one
sideload interval the updater accumulated 75.54 seconds of CPU time over
83.44 seconds elapsed. Whole-sda I/O busy time increased by 1.612 seconds;
68 flushes accumulated 49 milliseconds. This supports CPU-bound payload work,
but does not establish the actual bootloader CPU clock or guarantee a speedup.

## Integration and hardware provenance

The separate devicetree repository owns the controller and CPU links in
qcom/sm6125.dtsi. CPUs 0-3 use domain 0; CPUs 4-7 use domain 1. The controller
uses 0x0f521000 and 0x0f523000 with 0x1400-byte regions. Its clock inputs are
RPM XO and GCC GPLL0_OUT_EARLY, following the v1 mainline CPUFREQ HW binding.
SM6125 calls the undivided GPLL0 output OUT_EARLY; OUT_MAIN is already divided
by two. The cpufreq driver performs its own divide-by-two for the alternate
CPU frequency, so the divided OUT_MAIN clock must not be used here.

The ACK driver gains an SM6125 match with twelve LUT entries. Other existing
v1/EPSS matches retain forty entries. Allocation, LUT reads and requested-index
clamping use the selected SoC limit. Frequencies and voltages are read from
firmware LUTs; no hand-written OPP table or forced performance governor is added.
The existing firmware-enable check and per-core DCVS handling are retained.

Hardware facts come from Lineage's downstream Trinket sources:

- https://github.com/LineageOS/android_kernel_xiaomi_sm6125/blob/lineage-23.2/arch/arm64/boot/dts/qcom/trinket.dtsi
- https://github.com/LineageOS/android_kernel_xiaomi_sm6125/blob/lineage-23.2/drivers/clk/qcom/clk-cpu-osm.c

The downstream trinket fixup uses OSM_TABLE_REDUCED_SIZE=12. Its v1 registers
match ACK: enable 0x0, per-core DCVS 0xbc, frequency LUT 0x110, voltage LUT
0x114, performance-state request 0x920 and a 32-byte row stride. This is a
new adaptation of hardware facts; no upstream or donor commit is represented
as a cherry-pick. Existing source authorship and copyright are retained.

## Maintainer validation after rebuilding

Build a matching kernel, DTB and module set through the existing split-source
Lineage integration. The bringup DTS includes the SoC tree, so no optional
display, GPU or other hardware is enabled by this change.

Before attempting sideload, capture:

```sh
adb shell 'uname -a; ls /sys/devices/system/cpu/cpufreq'
adb shell 'for p in /sys/devices/system/cpu/cpufreq/policy*; do echo "$p"; cat "$p/related_cpus" "$p/scaling_driver" "$p/scaling_governor" "$p/scaling_available_frequencies" "$p/scaling_cur_freq"; done'
adb shell 'dmesg | grep -iE "cpufreq|frequency|opp|domain.*enabled"'
```

Expected evidence: policies for CPUs 0-3 and 4-7, qcom-cpufreq-hw, valid
nonzero frequency lists and frequency changes during payload work. If the
driver reports a disabled hardware domain or an invalid/empty firmware LUT,
capture that failure; do not bypass the enable check or invent frequencies.

Repeat the same ROM ZIP transfer and compare elapsed verification/application
time, updater CPU ticks, UFS busy/flush time and temperature with the earlier
capture. Keep diagnostic fsync local to log files; never call global sync in
the sampling loop. Complete sideload and CPU thermal/throttling behavior are
still acceptance requirements; source integration alone does not validate them.

## Live validation, 2026-10-05

ADB confirmed policy0 covers CPUs 0-3 at 300 MHz to 1.8048 GHz and policy4
covers CPUs 4-7 at 300 MHz to 2.016 GHz. Both report qcom-cpufreq-hw and
schedutil. Time-in-state includes every available frequency, including the
maximum. The first capture recorded 592 and 1143 transitions; a later capture
recorded 20895 and 29697 on the running device.

The maintainer reports that recovery sideload completed, although still slow.
Recovery-side frequency counters and installation timing were unavailable
while ADB was in sideload mode, and recovery logs did not survive in the usual
locations. Completion is confirmed by the maintainer; the size of any speedup
and sustained thermal/throttling behavior remain unverified.
