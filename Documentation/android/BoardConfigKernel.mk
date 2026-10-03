# SPDX-License-Identifier: Apache-2.0
# Include from the existing device BoardConfig after its old kernel settings.
# See README.md before adopting the boot-image and module settings.

# Kernel
BOARD_KERNEL_IMAGE_NAME := Image.gz
TARGET_KERNEL_ARCH := arm64
TARGET_KERNEL_SOURCE := kernel/mainline/sm6125-mainline
TARGET_KERNEL_CONFIG := laurel_pmos_defconfig android-mainline.config laurel_sprout.config
TARGET_KERNEL_CONFIG_EXT :=
TARGET_KERNEL_DTB := qcom/sm6125-xiaomi-laurel-sprout.dtb

# Kernel modules
BOARD_VENDOR_KERNEL_MODULES_LOAD += dispcc-sm6125.ko gpucc-sm6125.ko \
    panel-samsung-s6e8fco.ko msm.ko edt-ft5x06.ko
BOARD_RECOVERY_RAMDISK_KERNEL_MODULES_LOAD += dispcc-sm6125.ko gpucc-sm6125.ko \
    panel-samsung-s6e8fco.ko msm.ko edt-ft5x06.ko
RECOVERY_KERNEL_MODULES += dispcc-sm6125.ko gpucc-sm6125.ko panel-samsung-s6e8fco.ko \
    msm.ko edt-ft5x06.ko
TARGET_AUTO_COLLECT_KERNEL_MODULE_DEPS := true
