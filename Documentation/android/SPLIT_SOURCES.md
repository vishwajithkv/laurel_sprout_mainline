# Mi A3 6.18 source separation

## Verified Lineage reference

Avalon uses device/oneplus/sm8650-common. Its dependency list declares separate
kernel, devicetrees and modules repositories:
https://github.com/LineageOS/android_device_oneplus_avalon/blob/lineage-23.2/lineage.dependencies
https://github.com/LineageOS/android_device_oneplus_sm8650-common/blob/lineage-23.2/lineage.dependencies
Its BoardConfig uses TARGET_KERNEL_SOURCE, TARGET_KERNEL_EXT_MODULE_ROOT and
TARGET_KERNEL_EXT_MODULES, including the kbuild module mechanism:
https://github.com/LineageOS/android_device_oneplus_sm8650-common/blob/lineage-23.2/BoardConfigCommon.mk

This implements that source separation using the existing Lineage build hooks.
It does not turn the Mi A3 into a certified GKI target. Google's complete ACK
history remains in the kernel repository. Generic drivers and early-boot
storage/USB/power/thermal support remain there. This phone retains header v0,
Image.gz plus appended DTB, recovery-as-boot, existing physical partitions,
erased test-slot DTBO and vendor module packaging.

## Repositories

- sm6125-mainline-6.18: ACK core, shared subsystem patches, Kconfig and configs.
- sm6125-mainline-6.18-devicetrees: qcom/ SM6125 and Mi A3 board/PMIC sources.
- sm6125-mainline-6.18-modules: external panel driver, Kbuild and module selection.

Place these as siblings under kernel/mainline/. The kernel contains thin DTS
includes; DTC_INCLUDE resolves the external DTS repository and ACK binding
headers while retaining the existing DTB target and output path. Upstream
shared DTS files unrelated to this board remain in ACK. The panel Kconfig
entry remains in ACK but is module-only; its source and object rule are owned
by the modules repository. No optional driver is enabled by this refactor.

## Revision and baseline

Kernel split branch: mainline-6.18-split.
ROM split branch: lineage-23.2-6.18-split.
Companion source branches: mainline-6.18.
The ROM's laurel-mainline.xml pins all source repositories after local commits.
Publish the companion repositories and split branches before a fresh network
repo sync; they are initially local. Do not sync the split manifest prematurely.

The published unsplit baseline remains kernel mainline-6.18 at
5b501da414fdc6bc2ef8e37f7178396ae67b490e and ROM lineage-23.2-6.18 at
6347be6cbddda6faeb5814711916e7367436c984. Local tags
mi-a3-6.18-before-split identify that source baseline.
The device booted 6.18.32-g47faf8ef4e7b before this refactor. That boot result
does not validate the split. Driver/DTS input bytes and configuration are
preserved, but the extraction needs a new maintainer build and device check.

## Maintainer validation

Use a fresh output directory (for example OUT_DIR=out-6.18-split) with the
normal breakfast laurel_sprout and m -j8 bacon commands. The standalone helper
also requires both companion repositories and builds matching external modules.
Run verify-artifacts.py against the outputs as documented in README.md.
Confirm the single appended DTB, matching module vermagic/depmod metadata,
recovery, ADB and normal sys.boot_completed=1. Keep known-working images.
Agents do not build, test or flash under AGENTS.md.

To return to the baseline, switch the kernel to mainline-6.18 and ROM device
tree to lineage-23.2-6.18. Restore the latter branch's local manifests and use
its matching images/output directory. Companion repositories may remain on disk.
