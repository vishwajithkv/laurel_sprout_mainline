<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Xiaomi Mi A3 Android mainline kernel

This is a source preparation for Mi A3 (`laurel_sprout`, SM6125), targeting
an initial LineageOS 24.0 kernel boot and ADB connection. The maintainer
has generated kernel, DTB and module artifacts. Both repacked boot attempts
returned directly to the bootloader. No kernel log or successful Android
boot is available. The agent inspected the artifacts without building or
running runtime tests. `CONFIG_F2FS_FS_SECURITY=y` was added to the hardware
fragment after the first build and requires a rebuild. Experimental
repacked images and their limitations are documented in the accompanying
`out/boot-mainline-20261003/README.md` handoff outside this source tree.

The hardware baseline is SzczurekYT's `laurel` branch, Linux 6.15.0,
revision `39f1dc8c31fd87fa9179b6ed4d628fe69e0cd78a`, from
[the Mi A3 kernel fork](https://gitlab.postmarketos.org/SzczurekYT/linux).
This is a pinned community hardware port, not a claim that Linux 6.15 is
the latest release or that all its enabled devices work on Android.

The base config is now `laurel_pmos_defconfig`, derived from the published
postmarketOS SM6125 Linux 6.1 config. Its download matches the APKBUILD's
SHA-512 checksum. Symbols absent from this 6.15 source are omitted and
recorded in `config-sources.json`; Android requirements and hardware fixups
are merged afterwards. This is a static migration, not a distribution
config validated against this exact newer revision. New choices and
dependencies must be resolved by the maintainer's `olddefconfig`.
The existing compiled artifacts used ARM64 `defconfig`; they do not contain
this config migration or the F2FS security-label fix.

For a minimal framebuffer/ADB boot, use [the first-boot profile](FIRST_BOOT.md).
It disables optional probing while preserving core hardware and reservations.

## Contents

- `arch/arm64/configs/laurel_pmos_defconfig`: migrated distribution baseline.
- `arch/arm64/configs/android-mainline.config`: Android requirements,
  merged in the accompanying mainline documentation's fragment order.
- `arch/arm64/configs/laurel_sprout.config`: hardware fixups applied last.
- `arch/arm64/boot/dts/qcom/sm6125-xiaomi-laurel-sprout.dts`: existing
  device DTS, including bootloader IDs, RAM reservations and ramoops.
- `arch/arm64/boot/dts/qcom/sm6125.dtsi`, `pm6125.dtsi`, `pmi632.dtsi`:
  existing SoC and PMIC trees. Their upstream bindings are preserved.
- `Documentation/android/BoardConfigKernel.mk`: kernel and module settings
  to include from your existing Android device tree.
- `Documentation/android/config-sources.json`: exact source revisions,
  fragment hashes, Android patch URLs/hashes and unavailable symbols.

The three Android patches follow `device/mainline/common/docs/KERNEL_PATCHES.md`:
USB configfs userspace uevents (Lee Jones, with the original sign-offs),
the memfd ashmem ioctl shim, and its shmem hook (Isaac J. Manjarres).
Sources are pinned to AOSP `kernel/common-patches` revision
`95c6537fa2f92733e8b9fe23ab4ace2446070f41`. The shim's `ASHMEM_C`
dependency is removed as that guide requires. This does not add an ashmem
device or turn this kernel into the Android Common Kernel.
Original mail patches and their authorship/sign-off records are preserved
verbatim in `Documentation/android/upstream-patches/` for attribution;
their changes are already applied, so do not apply those copies again.

## Integration with your Android tree

Place this repository at `kernel/mainline/sm6125-mainline`. Include
`kernel/mainline/sm6125-mainline/Documentation/android/BoardConfigKernel.mk`
from your device BoardConfig after its old kernel settings, or copy the
settings into your existing tree. The config order is:

```make
TARGET_KERNEL_CONFIG := laurel_pmos_defconfig android-mainline.config laurel_sprout.config
```

The in-tree Android fragment merges the mainline pre-fragments, the AOSP
Android 6.12 base requirements, ARM64 conditional requirements, common
mainline additions, framebuffer console, and hardening/Rust disable
fragments. It records symbols absent in Linux 6.15 instead of inventing
Kconfig entries. This is a static text merge; it does not evaluate
dependencies. The migrated distribution baseline retains its broad ARM64
coverage.
Options that depended on removed or renamed symbols need review in the
resolved config; filtering declarations does not establish equivalence.

Replace old downstream kernel module lists and prebuilt module paths
before using the snippet; it appends the new modules to your lists.
Only modules built together with this kernel may be installed. Dependency
collection needs the Lineage build support documented in
`device/mainline/common/docs/KERNEL.md`. Keep generated dependency and alias
metadata, and arrange for your existing mainline init to load these lists.
Copying `.ko` files alone does not load their dependencies.

The snippet does not change partitions or boot image layout. Retain your
Mi A3 A/B geometry and follow the device Linux port for mainline DTB
packaging. The downstream Android boot header alone does not establish
mainline support. The build target is
`qcom/sm6125-xiaomi-laurel-sprout.dtb`; do not append a stock downstream
DTB or apply downstream DTBO overlays to it. This source supplies a full
DTB, not an Android DTBO image. Your existing packaging must select it
and must not expect a new downstream `dtbo.img` from this kernel.

Android userspace must also:

- Include `kernel/mainline/configs` in `PRODUCT_SOONG_NAMESPACES` and
  `use_memfd.rc` in `PRODUCT_PACKAGES`, as the kernel patch guide specifies.
- Use the C Binder implementation. Remove an inherited `binder.impl=rust`
  command-line argument; this baseline has no Rust Binder implementation.
- Use the mainline USB init path and discover the UDC under `/sys/class/udc`.
  The DWC3 child is at `4e00000`; confirm its runtime name instead of using
  a downstream `a600000.dwc3` setting. FunctionFS is built in for ADB.
- Match fstab/platform lookup paths to the mainline UFS controller at
  `/soc@0/4804000.ufshc`. Preserve partition labels and A/B suffixes.

## Module policy

UFS/SCSI, UFS PHY, GCC, pinctrl, PMIC regulators, RPM shared memory,
USB/DWC3/QUSB2/Type-C and configfs are requested built in. Reaching storage
and ADB must not require vendor module loading. Ramoops is also built in.

Display and touch are requested as modules:

| Module | Function |
|--------|----------|
| `dispcc-sm6125.ko` | Display clocks |
| `gpucc-sm6125.ko` | GPU clocks |
| `panel-samsung-s6e8fco.ko` | Existing Mi A3 AMOLED panel driver |
| `msm.ko` | Qualcomm DRM/DPU/DSI and GPU driver |
| `edt-ft5x06.ko` | FT3518 touch controller |

Dependencies such as DRM helpers and Qualcomm firmware loaders are
collected from the actual build. SimpleDRM is requested built in for
the existing bootloader framebuffer; successful display takeover remains
unverified. No firmware blobs are included. The GPU tree asks for
`qcom/sm6125/xiaomi/laurel/a610_zap.mbn`; firmware availability and userspace
graphics compatibility need separate validation.

## Maintainer build and review

Agents must not execute these commands. The maintainer can use a suitable
LLVM toolchain and host dependencies to build outside the source tree:

```sh
export MI_A3_KERNEL_OUT=/absolute/path/to/out/mi-a3-kernel
make ARCH=arm64 LLVM=1 O="$MI_A3_KERNEL_OUT" laurel_pmos_defconfig
./scripts/kconfig/merge_config.sh -m -O "$MI_A3_KERNEL_OUT" \
    "$MI_A3_KERNEL_OUT/.config" \
    arch/arm64/configs/android-mainline.config \
    arch/arm64/configs/laurel_sprout.config
make ARCH=arm64 LLVM=1 O="$MI_A3_KERNEL_OUT" olddefconfig
make ARCH=arm64 LLVM=1 O="$MI_A3_KERNEL_OUT" -j8 \
    Image.gz qcom/sm6125-xiaomi-laurel-sprout.dtb modules
make ARCH=arm64 LLVM=1 O="$MI_A3_KERNEL_OUT" modules_install \
    INSTALL_MOD_PATH="$MI_A3_KERNEL_OUT/module-staging" INSTALL_MOD_STRIP=1
```

Review the resolved `.config` for all boot-critical `=y` requests and
the five module requests above. Use the mainline config validator with
its documented arguments and review unavailable Android requirements.
Artifacts are under `$MI_A3_KERNEL_OUT/arch/arm64/boot/` and
`$MI_A3_KERNEL_OUT/module-staging/lib/modules/<kernel-release>/`.
Use your existing Android build integration to package them; no flashing
commands or repartitioning are part of this kernel change.

Read [the boot audit](BOOT_AUDIT.md) before another boot attempt.
For the first boot, report kernel logs or ramoops, UFS probe/partition
discovery, `/sys/class/udc`, configfs gadget state, and Android init errors.
If ADB works, collect `dmesg`, `logcat -b all`, the resolved `.config`,
`/proc/modules`, and `/sys/fs/pstore` contents for the next kernel changes.

## Known gaps

`DM_DEFAULT_KEY` is absent. Existing userdata with Android metadata
encryption is not supported by this preparation; do not assume it can
be mounted. Adding that driver or adapting the Android storage configuration
is a separate step based on the existing tree and its actual encryption
format. Ordinary dm-crypt and filesystem encryption are not replacements
for this missing target.

`CPU_FREQ_TIMES`, quota2 accounting and `UID_SYS_STATS` are also absent.
The omission list additionally contains removed options and ashmem options
replaced by memfd. Full Android compatibility and CTS/VTS are unverified.
Modem, calls, audio, Wi-Fi, Bluetooth, camera, sensors, fingerprint and
charging behavior have not been established by a hardware boot here.
Downstream 4.14/5.4 modules and proprietary HAL interfaces cannot be
assumed compatible with mainline. This deliverable covers kernel sources,
trees and module integration, while your existing Android tree remains
the integration point for HALs and init.
