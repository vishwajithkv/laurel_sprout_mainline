<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Mi A3 display startup investigation, 2026-10-07

Live Android build: 6.18.32-g23215b7795ae-dirty #18, boot_completed=1.
Driver binding is early, despite the reported black interval:

- SimpleDRM registers at 0.369 s; MSM DRM at 1.254 s.
- SMMU faults at 1.251 s use SID 0x400 and addresses in the bootloader
  framebuffer range starting at 0x5c000000.
- Initial brightness fails at 1.722 s after video-done/DMA timeouts.
- bpfloader blocks init for 13.245 s, approximately 7.366 to 20.611 s.
  Its logs report successful loading; entropy initialized at kernel time zero.
- Composer starts at 20.908 s; brightness/display-off transfers time out
  around 22 s. BootAnimation reports a start time of 23802 ms.

No current log identifies the exact instant physical output starts. BPF
loading accounts for a measured userspace delay, but does not explain the
early native-link transfer failures.

## Comparison with the exact Mi A3 4.14 source

Reference: LineageOS/android_kernel_xiaomi_sm6125 commit
77d2912bc00eda19182a95a24bbe23543c792814. Inspected the Laurel Samsung
S6E8FCO panel DTS, drivers/gpu/drm/msm/dsi-staging/dsi_panel.c and
dsi_display.c, plus drivers/gpu/drm/msm/sde/sde_connector.c.

The panel requests delay_until_first_frame. The connector waits for
MSM_ENC_TX_COMPLETE before allowing a backlight update. Our 20 ms worker
does not provide that hardware completion guarantee. Downstream also has
continuous-splash handling that skips panel preparation/controller reset
while retaining the active bootloader display. Mainline reinitializes the
link and does not reproduce that handover.

Brightness byte order already matches: downstream swaps bl_inverted_dbv
before its low-byte-first helper; mainline uses brightness_large, which
sends high-byte-first. Do not change byte order or brightness transfer mode
merely to hide the timeout.

The next native correction needs orderly bootloader DMA/IOMMU handover and
first-frame synchronization for brightness. DPU wait_flush waits for commit
completion after frame kickoff; panel enable runs before kickoff. Waiting
inside panel enable would delay the kickoff itself. An asynchronous design
must also serialize correctly with disable/removal. The precise native
failure mechanism remains unproven; do not restore the MDSS reset consumer
without addressing the previously isolated boot failure.

## Recovery drawing lock correction

Recovery uses SimpleDRM and does not bind the native panel. Earlier saved
SimpleDRM recovery logs show missing-battery retries from 2.933 to 7.784 s.
ScreenRecoveryUI::BattMonitorThreadLoop holds updateMutex through the
five-second retry, including sleeps. UI drawing uses that same mutex.

The local bootable/recovery edit moves battery sampling/retries outside the
drawing lock, retaining locked UI state updates and the existing fallback.
It does not remove battery discovery or claim that a battery driver works.
The companion recovery-battery-ui-lock.patch reproduces the change on
bootable/recovery commit acd99f7c48e0229eaedd40aa3e2adcbabe2dc283. Apply it
from that repository when recreating this workspace.

This fixes a demonstrated blocking code path; the connected device was in
Android, so latest recovery timing is not yet captured. The maintainer must
rebuild matching ROM/boot outputs and verify menu timing, input and sideload
progress with new recovery logs. No build, test, flash or reboot was performed.

Evidence: out/recovery-sideload-debug/display-startup-20261007/
android-kernel.log and android-startup-logcat.txt; earlier SimpleDRM recovery
logs remain under native-probe-isolation-20261006/.

## Researched source candidate

The upstream S6E8FC0-M1906F9 series by Kamil Golda and Yedaya Katsman is
specifically for Mi A3. Our timing/reset and prepare_prev_first already match:
https://lore.gitlab.freedesktop.org/drm-ai-reviews/20260320-panel-patches-v7-1-3eaefc4b3878@gmail.com/T/

The new DPU teardown adapts Bjorn Andersson's bootloader data-path proposal,
retaining attribution rather than presenting it as a merged fix:
https://lists.openwall.net/linux-kernel/2021/05/12/3502
Only Mi A3 changes initialization order. With clocks/hardware objects ready,
active interfaces stop and drain for 35 ms at 60 Hz. CTL blend stages are
cleared/flushed before replacing the inherited IOMMU mappings. The unsafe
MDSS reset consumer remains omitted.

The panel worker now uses a local exported MSM DSI helper to wait for a
VIDEO_DONE interrupt instead of treating a timer as frame completion. The
host serializes it with commands and arms completion before the interrupt.
Brightness waits for the event, with retries bounded to one second per
enable. Disable/removal stop retries and cancel work before host teardown.
The new drm/msm_dsi_frame.h API requires matching rebuilt kernel and modules.

This candidate is uncompiled/unvalidated. It targets handover/first-frame
failures, not BPF load time or seamless continuous splash. Maintainer checks:

- Log: Mi A3 bootloader display paths cleared before IOMMU handover.
- Bootloader-framebuffer SMMU faults disappear during DRM initialization.
- Log: Brightness enabled after DSI video frame.
- Initial brightness VIDEO_DONE/DMA timeouts disappear; compare physical
  output timing and reboot stability against build #18.
- Recovery keeps SimpleDRM and renders promptly with the battery drawing-lock
  correction. Check input and sideload progress.

Only source/diff review was performed under the repository instructions.

## BPF startup candidate and build #19 evidence

The connected maintainer build #19 (6.18.32-g7f5e1bde1341-dirty) still waits
13.806 s for bpfloader, from 7.487 to 21.282 s. Its detailed loader logs place
3.050 s between processing offload.o and the first map creation, and 6.745 s
between processing netd.o and the first map creation. Another approximately
1.3 s for offload.o and 1.1 s for netd.o precede their first program-load
results. Most subsequent logged program loads take milliseconds. These
intervals include userspace ELF/BTF preparation and do not isolate the BTF
syscall itself. The supported-kernel warning does not cause a 20 s failure
sleep: loading succeeds. Both CPU policies use schedutil and reach their
stock maximum frequencies in the current read-only snapshot.

NetBpfLoad's ElfObject uses repeated seek/read calls: each BTF variable scans
symbols, and each symbol-name lookup rereads the ELF header, section headers
and strings. The source candidate in packages/modules/Connectivity reads
each immutable APEX ELF once into an in-memory stream. Existing parsing,
kernel verification, map/program selection and pinning remain intact. New
INFO timings distinguish BTF fixups, kernel BTF loading and total ELF loading.
This targets a concrete redundant-I/O path; its contribution to the measured
delay and the resulting improvement require the next maintainer build.

Reproducible patch, against Connectivity c9f3e7795256bd4a3f99d02e604e24fd1552c2b3:
rom-patches/connectivity/0001-netbpfload-cache-elf-startup.patch
The edit is already applied locally. For a fresh checkout, apply that patch
once from packages/modules/Connectivity. A full ROM rebuild must include the
Tethering APEX; rebuilding only boot/kernel will not install this correction.
Compare the new BTF/ELF timings and init's bpfloader wait across boots, confirm
successful loading and network-map availability, then compare composer start.
No build, test, flash or BPF loader rerun was performed by the agent.

Build #19 evidence is saved in
out/recovery-sideload-debug/display-startup-20261007/android-build19-kernel.log.
The native brightness worker exhausts its initial retries at 2.501 s but
reports a successful frame-synchronized brightness update at 23.199 s after
userspace composition starts. This does not establish seamless splash or
eliminate the native first-frame delay before composer startup.

## Maintainer result: BPF cache and late splash takeover candidate

The subsequent connected Android boot still identifies the same #19 kernel
but contains the rebuilt Tethering APEX. BPF loading succeeds; init's wait is
now 1.369 s, compared with 13.806 s in the previous captured boot. offload.o
loads in 103 ms and netd.o in 106 ms; their BTF fixups take 18/32 ms, and
kernel BTF load rounds to 0 ms. Composer starts at 11.003 s and brightness
succeeds after a DSI frame at 12.688 s. The new log contains no bootloader
framebuffer context faults in the focused scan. Initial brightness retries
still exhaust at 2.556 s before native userspace scanout is running.
Evidence: android-bpf-cache-kernel.log alongside the earlier captures.
Recovery display/timing improvement is the maintainer's report; this
connected boot is Android and does not independently validate recovery.

The experimental late splash takeover has been reverted at the maintainer's
request after improper display behavior. The opt-in parameter, fbdev-client
suppression, splash identity mapping/IOVA reservation and retained DPU power
are removed. Native startup again stops inherited scanout before attaching
its IOMMU mappings. The earlier DSI frame-synchronized brightness, recovery
drawing-lock and validated BPF cache changes remain. No build or flash was
performed for this revert; matching kernel/module outputs need rebuilding.

## Complete display rollback, 2026-10-08

Following the report of a stuck Lineage boot logo, all remaining uncommitted
display experiments have now been restored to the committed baseline:
DPU teardown/MMU reordering, exported DSI frame wait and panel brightness
retry changes are removed, in addition to splash retention. The panel again
uses the committed deferred 20 ms brightness worker. Earlier historical
candidate descriptions above no longer describe the current source.
Wi-Fi, recovery UI and Connectivity BPF changes remain independent.
Archived display diffs are in out/display-revert-20261008 locally. Matching
kernel and panel modules must be rebuilt together; no runtime fix is claimed
until the maintainer validates.
