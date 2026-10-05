# SPDX-License-Identifier: Apache-2.0
# Kernel-only first-boot settings. See FIRST_BOOT.md for Android integration.

# Kernel
BOARD_KERNEL_CMDLINE += clk_ignore_unused regulator_ignore_unused panic=-1
BOARD_KERNEL_IMAGE_NAME := Image.gz
TARGET_KERNEL_ARCH := arm64
TARGET_KERNEL_CONFIG := laurel_pmos_defconfig android-mainline.config \
    laurel_sprout.config laurel_bringup.config
TARGET_KERNEL_CONFIG_EXT :=
TARGET_KERNEL_DTB := qcom/sm6125-xiaomi-laurel-sprout-bringup.dtb
TARGET_KERNEL_SOURCE := kernel/mainline/sm6125-mainline-6.18
include kernel/mainline/sm6125-mainline-6.18-devicetrees/BoardConfigDevicetrees.mk
include kernel/mainline/sm6125-mainline-6.18-modules/BoardConfigModules.mk

# Kernel modules
# This profile needs no loadable module to reach storage, framebuffer or ADB.
# Replace downstream/prebuilt module lists in the existing Android device tree.
BOARD_RECOVERY_RAMDISK_KERNEL_MODULES_LOAD :=
BOARD_VENDOR_KERNEL_MODULES_LOAD :=
RECOVERY_KERNEL_MODULES :=
