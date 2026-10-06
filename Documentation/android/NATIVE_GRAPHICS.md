<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Mi A3 native graphics integration

Status: the maintainer's first native build boots Android on 2026-10-05,
but physical display scanout fails. Live SurfaceFlinger reports Freedreno
FD610 / GLES 3.2 Mesa 25.3.3, confirming native GLES is selected. This does
not validate Vulkan, performance, physical scanout or suspend/resume.
The previously confirmed physical display transition used SimpleDRM.

## Selected stack

The ROM now defaults to `LAUREL_GRAPHICS_PROFILE ?= native` through
device/xiaomi/laurel_sprout/configs/graphics-profile.mk (using `?=` so a board
override is possible). This selects all layers together:

- ACK MSM DRM, Adreno 610, ARM SMMU, GPU/display clocks and DPU/DSI 14 nm PHY.
- The attributed Samsung S6E8FCO panel from the external modules repository.
- Mesa Freedreno GLES and Turnip Vulkan, using the MSM kernel interface.
- MSM minigbm-upstream allocation and the upstream AIDL DRM composer.

The kernel merges laurel_native_graphics.config after laurel_bringup.config.
The external DTS target sm6125-xiaomi-laurel-sprout-native.dtb re-enables the
existing GPU, SMMU, display and DSI nodes without replacing memory reservations,
panel wiring, timing, power supplies or GPU operating points. The imported
drivers retain their original authorship; no replacement BSP GPU driver is
introduced. SM6125 UBWC parameters and A610 bank-bit handling already exist
in this ACK tree.

The ROM pins inspected Mesa 25.3.3, DRM composer and Mesa host dependencies.
The maintainer's first Mesa build exposed an Android cross-file bug: replacing
every occurrence of `out/` also replaced the suffix in `laurel_sprout/`, corrupting
library paths. The carried patch uses prefix-only Make substitutions for output
and prebuilt paths. It is applied in this workspace. After a fresh Mesa sync,
apply it once from the Android root:

```sh
git -C external/mesa apply ../../kernel/mainline/sm6125-mainline-6.18/Documentation/android/rom-patches/external-mesa/0001-android-preserve-linker-path-components.patch
```

Its device framework matrix accepts the composer's AIDL version 4 while
retaining target FCM 7 for the remaining HALs. Shared allocations start with
`vendor.minigbm.debug=nocompression`; GPU internal tiling is still possible.
Framework rendering initially uses Skia GLES. This is an initial compatibility
choice, not a demonstrated performance optimum.

SimpleDRM stays compiled for early scanout; MSM removes the conflicting
framebuffer when it takes ownership. The native composer requires working MSM
KMS. There is no automatic switch back to the software compositor on failure.
Framebuffer console text remains disabled.

## Firmware and recovery

No firmware bytes are added to these source repositories. The product refers
to the already extracted Mi A3 a610_zap.elf in vendor/xiaomi/laurel_sprout and
the existing upstream a630_sqe.fw in external/linux-firmware-mainline.
Missing files stop product configuration with a clear error.

The inspected signed ZAP is a complete ELF with its loadable segment bytes
inside the file, not the separate MDT header. It is installed under the board
DT's firmware-name, qcom/sm6125/xiaomi/laurel/a610_zap.mbn. Its inspected SHA256
is e1f6e36c5a1cb4686dfacc2bd023f549444b0814032fa6e4cd6ec52821921a16.
The inspected SQE version is 0x207, newer than the kernel's 0x190 condition.
These file inspections do not establish successful SCM authentication on-device.

Both files also go into the recovery ramdisk's vendor/firmware directory.
The vendor SQE destination is owned by the existing Soong module
linux_firmware_qcom-a630; PRODUCT_COPY_FILES copies SQE only into recovery
to avoid duplicate install recipes.
The freshly compiled panel-samsung-s6e8fco.ko and dependency metadata are
included in /lib/modules with an early load list, and also installed in vendor.
This is required because recovery is inside boot and cannot rely on mounting
the Android vendor partition before drawing. Generic GPU/display drivers stay
built in. Do not reuse a panel module from another kernel build.

## Maintainer build

A complete ROM rebuild is required: kernel, DTB, panel module, firmware paths,
allocator, composer and Mesa change together. Agents do not execute builds or
device tests under AGENTS.md. From the existing patched workspace:

```sh
cd /home/vishwajithkv/android/lineage
export OUT_DIR=out-6.18-native
source build/envsetup.sh
breakfast laurel_sprout
m -j8 bacon
```

Use the existing init patch, userdata configuration and installation procedure.
No additional format or partition-layout change is required by this profile.
The ROM manifest pins matching native-profile kernel and DTS commits. A fresh
sync also needs the carried Mesa and DRM composer patches applied once as
documented here, alongside the established system/core init patch. Graphics
dependency pins are in the `zz-` XML to replace roomservice entries after they
have been parsed.

Inspect the resulting artifacts with the profile selected explicitly:

```sh
python3 kernel/mainline/sm6125-mainline-6.18/Documentation/android/verify-artifacts.py \
    --profile native \
    --out out-6.18-native/target/product/laurel_sprout/obj/KERNEL_OBJ \
    --modules-root out-6.18-native/target/product/laurel_sprout/vendor/lib/modules \
    --boot out-6.18-native/target/product/laurel_sprout/boot.img
```

Standalone compilation selects native with MI_A3_GRAPHICS_PROFILE=native;
the standalone helper still defaults to simpledrm. Its output is not a ROM or
boot image. Its native output defaults to out/kernel-laurel-6.18-native,
separate from out/kernel-laurel-6.18 for SimpleDRM. Keep overrides separate too.

## Device acceptance and next corrections

Validate recovery display/touch/ADB first. Then capture normal-boot kernel
and Android logs before assuming a performance improvement. Confirm the
panel module loads, DRM identifies MSM with a connected DSI display, and
there are no firmware, SCM, IOMMU, fence or atomic-commit failures. Check that
SurfaceFlinger reports Mesa/Adreno rather than ANGLE/SwiftShader, boot completes,
and the physical screen updates at its 60 Hz mode.

Check scrolling/frame timing, GPU devfreq transitions, suspend/resume, brightness
and repeated screen-off/on before calling the stack stable. Compare under the
same workload and thermal conditions; no stock-performance claim is established
by enabling these drivers. Fix the earliest actual failure in the logs before
tuning compression, shader compilation or scheduler policy. CPU/UFS tuning and
connectivity remain subsequent tasks in the maintainer's priority order.

## Explicit fallback

Change the default in configs/graphics-profile.mk from native to simpledrm,
then rebuild the complete ROM in a separate output directory. This selects
the previously verified bringup DTB, SwiftShader/ANGLE, generic allocation and
DRM framebuffer composer, with native GPU/display nodes disabled. Use
`--profile simpledrm` for its artifacts. Keep the known-working images available
while validating native scanout.

## First native device results and pending fixes

Build: 6.18.32-g0cd7ee117732-dirty #9, 2026-10-05 22:34:08 IST.
Android boot_completed is 1. The panel module loads, MSM registers card1,
DSI-1 is connected and exports 720x1560. SurfaceFlinger instead reports
1024x768 at 60 Hz. Its GLES renderer is Freedreno FD610, not SwiftShader.
GPU devfreq exists with simple_ondemand and the imported 320–950 MHz table;
the sampled frequency was 320 MHz. This single sample is not a scaling test.

Two concrete failures were found:

1. SimpleDRM registers card0, then MSM removes it and registers card1. The
   composer's default scan stops at the first missing numbered node, so it
   never opens card1 and creates its headless 1024x768 display. ResourceManager
   now enumerates existing numeric card nodes and sorts them by minor number.
   The fresh-sync patch is saved below; it is applied in this workspace:

   ```sh
   git -C external/drm_hwcomposer-upstream apply ../../kernel/mainline/sm6125-mainline-6.18/Documentation/android/rom-patches/external-drm-hwcomposer-upstream/0001-enumerate-drm-cards-with-minor-gaps.patch
   ```

2. The kernel warns that disp_cc_mdss_pclk0_clk_src cannot update its RCG
   configuration during DSI link setup. Panel DCS transfers time out (-110),
   panel initialization and backlight activation fail, and DPU waits for
   vblank time out. The SM6125 pixel-clock source now has
   CLK_OPS_PARENT_ENABLE, matching the existing SM6115 clock pattern in ACK
   and keeping the parent PLL enabled during clock operations. This is a
   candidate fix for the first display-clock failure, not a verified resolution.
   The generic pattern is also documented in upstream clock fixes:
   https://cos.googlesource.com/third_party/kernel/+/3b8786556313010d070ef3101196e798e7de3226/drivers/clk/qcom/dispcc-sm6350.c

Early apps-SMMU faults address the bootloader framebuffer during takeover;
they are additional evidence to revisit if they persist after proper modeset.
No later GPU fault was established by the captured logs.

Both fixes require a maintainer rebuild. They have not been compiled or
device validated. Capture logs again and confirm SurfaceFlinger selects the
real 720x1560 DSI output, the physical panel lights, and clock/DCS/vblank
timeouts clear before making a smoothness or stability claim. No wm size
override or live clock/register modification was applied to hide the failure.

Local diagnostic artifacts: out/recovery-sideload-debug/native-graphics-20261005.log
and native-graphics-clocks-20261005.log in the Android workspace.

### Build #10: card discovery fixed, panel still dark

The maintainer's build #10 (2026-10-05 22:57:28 IST) reports the real physical
size 720x1560, confirming the dummy-display problem cleared. Its kernel log
still contains the pixel RCG update warning, panel initialization/backlight
DCS timeouts and recurring vblank timeouts. The parent-enable change alone
did not resolve display scanout. Recovery and normal Android are both dark
according to the maintainer. The captured boot_completed property was empty;
the earlier boot-completion result must not be attributed to this capture.
Saved log: out/recovery-sideload-debug/native-display-build10-20261005.log.

A further PHY inconsistency was identified: dsi_14nm_phy_enable selects LDO
0x1c for standalone use, but pll_db_commit_14nm overwrites it with hardcoded
0x3c. Both now call the same existing usecase-dependent calculation, preserving
standalone 0x1c and the previous 0x3c value for master/slave operation. This
source correction has not been compiled or checked on-device. Panel wiring,
timing and init commands are unchanged. The surviving clock warning and
possible bootloader PHY clamping remain follow-up items if it still fails.

Reference and attribution: Huabin1010 <Huabin1010@users.noreply.github.com>,
ginkgo-mainline-linux commit c070fbf7ef08f15647063953e30404eb1a0ee1fd,
docs/ginkgo-display-bringup-methodology.md section 7.3 documents this LDO
overwrite and a shared-helper correction. This implementation factors the
existing ACK PHY calculation into a helper; original driver authorship and
headers remain intact. Reference:
https://github.com/Huabin1010/ginkgo-mainline-linux/blob/c070fbf7ef08f15647063953e30404eb1a0ee1fd/docs/ginkgo-display-bringup-methodology.md

### Build #11: LDO correction insufficient; clamp handling candidate

Live ADB confirmed 6.18.32-g3c9990df5cfd #11, built 2026-10-06 17:18:28 IST,
boot_completed=1 and physical size 720x1560. This kernel contains both the
pixel-clock parent-enable and standalone LDO corrections. It still reports
RCG update failures, panel command DMA timeouts (-110), failed panel
initialization and recurring DPU vblank timeouts. Neither correction has
resolved physical scanout. Scrcpy access does not validate the DSI panel.

A read-only debugfs clock snapshot reports VCO=1601232000, pixel=133436000
and byte/byte_intf=100077000 Hz with nonzero prepare/enable counts. These
reported rates are not proof that the electrical link is functional.
The temporary debugfs mount was removed after capture. No live clock or
register writes were performed.

The next source candidate adds an optional SM6125 PHY clamp resource at
0x05e01400, with the lane clamp enable at offset 0x54, bit 0. The downstream
Trinket DTS supplies precisely this mapping; dsi_phy_hw_v2_0_clamp_ctrl()
clears the register when disabling the clamp. Our driver clears only bit 0,
preserving other bits, after PHY lane power-up and before host transfers.
It logs the old and read-back register value once as `DSI PHY clamp:`.
Mapping is optional for compatibility with older DTBs and other PHY users.
The matching split DTS supplies the resource; rebuild kernel and DTB together.
No panel timings, init commands, GPU configuration or firmware were changed.

This is an uncompiled, device-unvalidated candidate. The bootloader clamp
state has not been observed directly. If the log shows the clamp was already
clear and timeouts remain, investigate byte-interface divider/host sequencing
and the surviving RCG failure rather than treating unclamping as a confirmed
solution. Acceptance requires successful panel commands and physical output
in both recovery and Android, without recurring vblank failures.

Provenance: Qualcomm/The Linux Foundation downstream PHY implementation,
preserving existing driver copyright and authorship. Pinned references:

- https://github.com/LineageOS/android_kernel_xiaomi_sm6125/blob/77d2912bc00eda19182a95a24bbe23543c792814/arch/arm64/boot/dts/qcom/trinket-sde.dtsi
- https://github.com/LineageOS/android_kernel_xiaomi_sm6125/blob/77d2912bc00eda19182a95a24bbe23543c792814/drivers/gpu/drm/msm/dsi-staging/dsi_phy_hw_v2_0.c

Huabin1010's methodology linked above identified clamp handling as a bringup
lead; the register address and operation were independently checked against
the pinned downstream sources before adapting them here.

Laurel-specific cross-check at the same downstream revision: its
laurel_sprout-trinket.dtsi includes ../qcom-base/trinket-sde-display.dtsi
and laurel_sprout-trinket-display.dtsi. The qcom-base symlink targets ../qcom/,
so the shared Trinket display/PHY descriptions are used by Laurel itself.
The Laurel display file selects DSI controller/PHY 0 and supplies reset GPIO
90, panel LDO GPIO 26 and panel IOVCC GPIO 124. Its Samsung S6E8FCO panel
description agrees with the imported module's 720x1560@60 Hz timing
(horizontal porches 350/294, pulse 40; vertical porches 17/5, pulse 2),
12/2/10 ms reset sequence and initialization command payloads. This is a
source comparison, not proof of the attached panel's identity or successful
electrical operation. No Ginkgo panel-specific settings were imported.
Board-specific references:

- https://github.com/LineageOS/android_kernel_xiaomi_sm6125/blob/77d2912bc00eda19182a95a24bbe23543c792814/arch/arm64/boot/dts/xiaomi/laurel_sprout/laurel_sprout-trinket-display.dtsi
- https://github.com/LineageOS/android_kernel_xiaomi_sm6125/blob/77d2912bc00eda19182a95a24bbe23543c792814/arch/arm64/boot/dts/xiaomi/laurel_sprout/display/dsi-panel-s6e8fco-samsung-amoled-hdp-video.dtsi

Saved captures in the Android workspace: out/recovery-sideload-debug/
native-display-build11-20261006.log and native-clock-state-build11-20261006.log.

### Build #12: clamp clear; defer video until panel preparation

Live ADB confirms 6.18.32-g3c9990df5cfd-dirty #12, built 2026-10-06
17:46:21 IST, boot_completed=1. Its live DT contains dsi_phy_clamp, and
the driver logs `DSI PHY clamp: 0x0 -> 0x0`: the lane clamp was already
clear at first PHY enable. The clamp candidate does not explain this boot's
black panel. RCG update warnings, DCS -110 and vblank timeouts remain.

The first DCS transfer additionally logs `wait for video done timed out`.
ACK dsi_mgr_bridge_pre_enable() powers the host and starts its video engine
before the S6E8FCO panel bridge prepares. Panel prepare_prev_first correctly
requires host power/clocks first, but the video engine then waits for pixel
data before panel initialization has completed. In Laurel's pinned 4.14
dsi_display_enable(), dsi_panel_enable() sends the panel on commands before
dsi_display_vid_engine_enable() starts video.

The next candidate keeps PHY/host power-up in pre_enable and moves SM6125
host engine startup to the bridge enable callback, after panel preparation.
The command transfer path only requires power_on and enables its command
engine separately, so panel DCS transfers remain available before video.
Other controller compatibles retain their existing pre_enable sequence.
Original driver authorship is preserved; this is a local adaptation of the
downstream ordering, not an imported upstream patch. Reference:
https://github.com/LineageOS/android_kernel_xiaomi_sm6125/blob/77d2912bc00eda19182a95a24bbe23543c792814/drivers/gpu/drm/msm/dsi-staging/dsi_display.c

This source candidate has not been compiled or device validated. It may
remove the premature video wait while the independent clock/PHY failure
remains; do not claim physical output before a maintainer build and logs.
No new DTS or panel module change is needed beyond the sources used in #12.
Saved log: out/recovery-sideload-debug/native-display-build12-20261006.log.

### Build #13: sequencing change insufficient; targeted diagnostics

Live capture identifies build #13, 2026-10-06 18:00:54 IST. The first panel
FC 5A 5A command no longer waits for video completion first, but DMA still
times out (-110). Later brightness commands still encounter video waits.
The clamp remains 0 -> 0 and pixel-clock update warnings persist. The
sequencing correction has not restored scanout.

A read-only DISPCC register snapshot shows PCLK0 CMD_RCGR (0x205c)=0x11,
with UPDATE still set, CFG (0x2060)=0x100 and M/N/D=1/0xff/0xff.
This is evidence of a pending clock update, not proof of its cause.
DISP_CC_MISC_CMD (0x8000)=0: downstream's pixel/PHY gating bits 5/9 are
already clear. No speculative byte-interface divider or clock gating change
was made: the pinned downstream Trinket clock driver also parents byte_intf
directly to byte0_clk_src. The large read-only register capture was interrupted
after extracting relevant offsets; it is saved as a partial capture.

The next source change is diagnostic only: log the first SM6125 command DMA
failure's controller/status/clock/lane registers and the first successful
14nm PLL prepare's lock status, control, dividers and LDO. These are one-time
reads at points where registers are already powered and accessible. They do
not change timings, register programming or clocks. A maintainer kernel rebuild
is needed to obtain these logs; it is not a claimed display fix.

Saved captures: native-display-build13-20261006.log and
native-clock-registers-build13-partial-20261006.log under the workspace's
out/recovery-sideload-debug/. sys.boot_completed was empty at capture time;
do not attribute the earlier build's boot-completion result to this one.

### Upstream comparison: missing Mi A3 MDSS reset

Research on 2026-10-06 found a directly relevant, device-tested upstream fix:
bb4d28e377cf04fbee8a01322059fa14808cdfe9 by Val Packett explicitly reports
that DSI on xiaomi-laurel-sprout can fail at boot without an MDSS core reset.
It carries Tested-by: Yedaya Katsman. Our split DTS lacked both the MDSS reset
consumer and DISPCC #reset-cells; the ACK SM6125 clock driver lacked its reset
map. msm_mdss_reset() already exists in ACK but optional reset lookup silently
does nothing without the DTS reference.

Backported with original authors, dates and review trailers:

- 0221b14be8aae98d687efab066133a114bea02d8: reset ID binding (Val Packett).
- a09a80b44b155e932601292b467d8445a556fd91: DISPCC reset map at 0x2000
  (Val Packett).
- bb4d28e377cf04fbee8a01322059fa14808cdfe9: MDSS reset wiring, adapted to
  the split DTS path and retained SM6125 power-domain names (Val Packett).
- dbabf6a32ffb69a604f966ec01a20a060836939d: permit reset-cells through the
  common clock schema (Biswapriyo Nath).

Rebuild kernel and DTB together. These upstream patches were tested on Mi A3
by their contributors, but this 6.18 ACK backport has not been built or tested
here. It is stronger evidence than the preceding clamp/sequencing hypotheses;
do not call this port fixed before the maintainer supplies device results.
Reference:
https://github.com/torvalds/linux/commit/bb4d28e377cf04fbee8a01322059fa14808cdfe9

Other comparison results: the SM6125 no-init-clock-parking correction is
already carried as e3c3f61ce7cb; the 14nm PHY byte-interface divider selection
and PLL/AHB runtime-PM fixes are already present in our base. Do not duplicate
those changes or adopt an unconditional divide-by-two from another platform.
The established Android graphics stack uses MSM DRM/KMS, Mesa Freedreno and
DRM hwcomposer; this port already follows it. Actual GPU faults should be
investigated with MSM devcoredumps and Mesa command-stream tools. Our captured
panel DCS/vblank failures instead identify the display/DSI path as the current
blocker. References:

- https://github.com/SoMainline/docs.somainline.org/blob/master/source/guides/minimal-android.rst
- https://docs.mesa3d.org/drivers/freedreno.html
- https://github.com/torvalds/linux/commit/0b3ccb76b95bd06cf80124d8adda647c82a6cc0f

### Build #14: reset applied; prepared PLL lost during reprogramming

Connected-device capture confirms 6.18.32-gc0748f3c9b7a-dirty #14,
2026-10-06 18:21:41 IST, boot_completed=1. Live DT contains MDSS resets
and DISPCC #reset-cells=1. SurfaceFlinger reports Mesa FD610 GLES, with EGL
mesa and Vulkan freedreno selected; this does not validate Vulkan execution.
The display remains physically black according to the maintainer.

The earlier PCLK RCG update warnings are absent in this capture. Instead
PCLK and byte_intf report stuck-off during device population. At first modeset,
byte0 reports stuck-off, host link enable returns -16 and panel on commands
return -22 because host power_on never succeeded. These failures occur during
kernel fbdev modeset before Android's graphics HALs render a frame.

PLL diagnostics show an early successful prepare at 0.748907 s with status
0x2f, pll_ctrl=1, clk_cfg0=0xf1 and LDO=0x1c. At 1.389 s PHY initialization
and restore precede byte-clock failure. The later read-only clock summary
still has VCO/PLL parents prepared/enabled, while byte0_src reports hardware
disabled and its leaf clocks have zero prepare/enable counts.

Source inspection identifies a likely state mismatch: pll_db_commit_14nm()
calls pll_14nm_software_reset(), which clears PLL_CNTRL, but does not clear
phy->pll_on. If already prepared by CCF, its consumers will not invoke prepare
again to restore hardware. The next SM6125-specific candidate remembers
whether the PLL was running before rate programming and then clears the stale
flag, restarts it and checks lock after programming. It also covers the rate
restore that follows PHY reset. Other 14nm compatibles retain existing behavior.
The actual PLL_CNTRL value at the failing modeset was not directly captured;
this mechanism is inferred from source plus the logged sequence, not a proven
electrical diagnosis. No live clock/register writes were made.

The regression began when the native graphics profile enabled MSM DPU/DSI in
addition to Mesa Freedreno/Turnip. The previous SimpleDRM profile used the
bootloader framebuffer and did not initialize this native link. This distinction
explains why working software scanout does not validate the native DSI path.
There is no captured evidence that Turnip shader execution caused this boot's
display failure. Native GPU throughput and power management remain unvalidated.

The PLL restart candidate is local source work, not an imported upstream fix.
Only diff whitespace checks were performed; no compile, flash or hardware test.
Rebuild the kernel with the existing #14 DTB/module sources and capture the
`Restarting prepared SM6125 DSI PLL after rate restore` log plus any clock,
panel or vblank errors. Physical display recovery must be confirmed on-device.

Saved logs under out/recovery-sideload-debug/: native-display-build14-20261006.log,
native-graphics-build14-20261006.log and native-clock-summary-build14-20261006.log.

### Build #15: native scanout and hardware composition confirmed

On 2026-10-06, the maintainer confirmed physical display output and smooth
Android rendering after rebuilding the prepared SM6125 PLL restart correction.
Live ADB identified 6.18.32-gc0748f3c9b7a-dirty #15, boot_completed=1,
720 x 1560 at 60 Hz, and GLES freedreno / FD610 / Mesa 25.3.3. The kernel
logged `Restarting prepared SM6125 DSI PLL after rate restore`. No panel,
RCG or vblank failures appeared in the captured diagnostic filter. This
validates this combined build; it does not isolate each preceding workaround.

The AIDL DRM composer is active. Subsequent authorized live captures during
Settings scrolling showed both the app and status bar using DEVICE composition,
with interval composition efficiency 1 and no new test/display commit failures.
Launcher swipes used DEVICE for the wallpaper and CLIENT for launcher/status
bar. Idle CLIENT snapshots must not be treated as a broken HWC: the composer
intentionally flattens static scenes. Limited usable display planes may explain
the launcher scene, but runtime plane enumeration was unavailable and its
exact fallback reason remains unconfirmed.

The running allocator already selects vendor.minigbm.debug=nocompression;
captured buffers had modifier 0 and compressed=false. ro.vendor.freedreno.ubwc
has no implementation in the checked-out Mesa/minigbm sources. FD_MESA_DEBUG
supports noubwc for internal allocations; our Android property mapping is
`debug.mesa.fd.mesa.debug=noubwc`. This flag was not applied, and it is not a
switch that forces DEVICE composition. EGL mesa and Vulkan freedreno are
selected, but these captures validate GLES, not Turnip execution. GPU cooling
registration still fails and GPU throughput/power management need further work.

Evidence remains in the workspace under out/recovery-sideload-debug/:
graphics-report-build15-20261006.txt, native-graphics-build15-20261006.log,
native-display-build15-20261006.log, hwc-motion-report-20261006.txt,
hwc-motion-{0..4}.txt and hwc-settings-motion-{0..2}.txt. Raw device logs are
not committed. No build, flash or compression change was performed by the agent.
