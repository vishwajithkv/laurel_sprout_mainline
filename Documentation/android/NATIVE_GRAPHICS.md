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
