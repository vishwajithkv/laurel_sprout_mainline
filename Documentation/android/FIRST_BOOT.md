<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Minimal Mi A3 first-boot profile

## Goal

Reach kernel logs, Android init, ADB, and then boot animation. Hardware
acceleration, touch, audio, modem, camera, Wi-Fi and Bluetooth are later
milestones. This profile is source preparation, not a successful boot claim.

## Kernel selection

Use `BoardConfigBringup.mk` instead of `BoardConfigKernel.mk` in the existing
Android device tree. The config order is:

```make
TARGET_KERNEL_CONFIG := laurel_pmos_defconfig android-mainline.config \
    laurel_sprout.config laurel_bringup.config
TARGET_KERNEL_DTB := qcom/sm6125-xiaomi-laurel-sprout-bringup.dtb
```

The new DTS includes the existing Mi A3 DTS and overrides optional nodes
with `status = "disabled"`. It retains the CPU/memory definitions, bootloader
IDs, reserved memory, RPM/regulators, pinctrl/PMIC, thermal monitoring, UFS,
USB/Type-C, power buttons, ramoops and simple-framebuffer.

Native MDSS/DSI and Adreno paths are disabled for the first framebuffer/ADB
milestone. This avoids display takeover and the GPU zap-firmware dependency.
The panel supplies are retained with their existing boot-on configuration.
Touch, SD/MMC and RTC nodes are disabled. Modem/DSP/audio/camera/Wi-Fi nodes
were not enabled by this Mi A3 tree; their generic driver groups are also
disabled in the final config fragment. Thermal protection is retained.

Do not delete memory reservations just because a peripheral is disabled:
bootloader or secure firmware may still own those regions. A node marked
okay permits probing; it does not guarantee success or imply a panic.

Agents must not run builds or tests. For a maintainer kernel-only build,
follow README.md but merge `laurel_bringup.config` last and build the new
`qcom/sm6125-xiaomi-laurel-sprout-bringup.dtb` target. Review the resolved
config for built-in core/thermal/USB drivers and disabled optional groups.
No generated artifact has been rebuilt for this profile.

## Android integration still required

The synced checkout found at `/home/vishwajithkv/android/lineage` uses
`lineage-23.2` and the downstream Mi A3/SM6125 device trees. At inspection,
device/mainline/common, hardware/mainline/common and this mainline kernel
were absent from that checkout. Do not assume syncing the downstream
dependencies installs the mainline integration.

For the existing Android device tree:

- Add matching mainline repositories/branches; do not mix 24.0 integration
  into 23.2 without reviewing its platform dependencies.
- Use the initial-bringup defaults: SwiftShader, a framebuffer-compatible
  composer/allocator, permissive debug policy, and suspend disabled.
- Replace downstream KGSL/ION-dependent graphics components. There is no
  working hardware-rendering path provided by this profile.
- Preserve actual partitions, A/B labels and metadata. Match fstab/init UFS
  paths to mainline, and use mainline USB gadget init for the discovered UDC.
- Include use_memfd.rc and configure matching kernel/module packaging.
- Prevent optional hardware services from blocking init; fix observed fatal
  services individually. A fatal service reboot cannot be dismissed as an
  optional HAL crash.

This snippet does not set boot-header, DTB-append or DTBO rules. Follow
BOOT_AUDIT.md and the actual bootloader evidence; the stock header-v2 setup
and our no-op overlay have not established a working mainline boot path.
Use clk_ignore_unused and regulator_ignore_unused while retaining the
bootloader framebuffer so unused-resource cleanup does not switch off its
clocks or panel supplies. These are debug-only settings; remove them after
native drivers own those resources. Use panic=-1 and console logging during
initial bringup. Preserve slot A.

## Re-enable in stages

After kernel logging and ADB work, use the original full DTS and config to
enable native display first, then GPU with its required firmware and Mesa.
Change one subsystem at a time and collect logs before progressing. Kernel
version 5.4 alone is not a universal boundary requiring new HALs: the actual
userspace ABI and driver implementation decide compatibility.
