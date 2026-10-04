<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Mi A3 Android Common Kernel 6.18

Device: Xiaomi Mi A3 (laurel_sprout), SM6125 / Snapdragon 665 / Trinket.
Branch: `mainline-6.18`.

## Provenance and status

Base: https://android.googlesource.com/kernel/common,
tag `android17-6.18-2026-09_r5`, commit
`926a323dc2667901ddcf1d5b024b82eb00aa89cc` (Linux 6.18.32).
This checkout retains Google's complete history; it is not a source snapshot.
Do not confuse this custom board kernel with Google's certified GKI binaries.

Hardware donor: SzczurekYT/linux `laurel`, commit
`39f1dc8c31fd87fa9179b6ed4d628fe69e0cd78a`:
https://gitlab.postmarketos.org/SzczurekYT/linux.git.
The 17 hardware changes are accounted for in `patch-provenance.json`:
15 were replayed with original authors/dates/trailers and donor commit IDs;
two UFS fixes were already upstream. Context/API adaptations are documented
in the carried commits. The connectivity branch's additional work is deferred.

This migration is source-prepared, not compiled or boot-tested. Its acceptance
target is the existing 6.15 Android boot milestone: recovery, USB ADB,
`sys.boot_completed=1` and Settings through scrcpy. Android 16 compatibility
must be demonstrated on the phone; ACK's Android 17 release provenance does
not establish it.

## Android compatibility

ACK supplies Android Binder, USB gadget uevents, dm-default-key and inline
encryption support. Do not apply the old USB uevent patch a second time.

ACK's ashmem and memfd ioctl path depends on Rust. The established Lineage
kernel build does not configure a kernel Rust toolchain. Retain the authored
Isaac J. Manjarres C memfd shim from the 6.15 bringup, adapted to coexist
with ACK's dispatcher. `MEMFD_ASHMEM_SHIM` depends on `!ASHMEM`; first boot
uses `RUST=n`, `ASHMEM=n`, `MEMFD_CREATE=y`, `MEMFD_ASHMEM_SHIM=y` and
the existing ROM `sys.use_memfd=true`. Native ACK ashmem code remains present.

Configuration starts from the previous postmarketOS-derived distribution
config, followed by Android, board and minimal-bringup fragments. Symbols
absent from the new Kconfig declarations are removed and recorded in
`config-sources.json`. Available ACK Android requirements are restored.
This source audit does not substitute for resolving dependencies during a build.
Use 4 KiB pages and built-in boot dependencies. The full panel/GPU/touch/OTG
source support is retained, with optional probing disabled in the bringup DTS.

## Lineage integration

The ROM branch is `lineage-23.2-6.18` in
https://github.com/vishwajithkv/android_device_xiaomi_laurel_sprout.
It includes `Documentation/android/BoardConfigBringup.mk` from this source tree.
Source path: `kernel/mainline/sm6125-mainline-6.18`.
DTB: `qcom/sm6125-xiaomi-laurel-sprout-bringup.dtb`.

Lineage compiles kernel, DTBs and modules from this checkout. Fresh modules and
depmod metadata go into vendor; initial module load lists remain empty.
No reference binaries from 6.15 are copied into the build.

Preserve the current ROM init/fstab/cgroup fixes, software graphics, shader
cache workaround, DMA-heap permissions, persistent logger and console handover.
The required legacy-normal-boot init patch is retained at
`rom-patches/system-core/0001-init-recognize-legacy-normal-boot.patch`.
Keep it applied to `system/core` exactly once, using the device README's commands.

This phone uses header-v0 boot, 4096-byte pages, Image.gz plus one appended
DTB, and recovery-as-boot. Physical partition sizes remain boot 64 MiB,
system 3 GiB and vendor 1 GiB. There is no separate recovery/vendor_boot/dlkm
partition. Keep the established erased-DTBO prerequisite on the test slot.
See FIRST_BOOT.md for the maintainer validation sequence.

## Maintainer build commands

Agents do not execute these commands under this repository's instructions.
The build helper uses the checked-out Lineage LLVM toolchain including
llvm-objcopy, explicit output/module staging paths and fail-fast commands.
Host make, Python 3, flex, bison, OpenSSL development headers, depmod and the
standard Lineage host dependencies must be installed.

```sh
cd /home/vishwajithkv/android/lineage
JOBS=8 bash kernel/mainline/sm6125-mainline-6.18/Documentation/android/build-kernel.sh
```

Default standalone output: `out/kernel-laurel-6.18`.
Override with an absolute `MI_A3_KERNEL_OUT` and a complete `LLVM_BIN` directory.
The script builds Image.gz, the bringup DTB, all selected modules and installs
them into `module-staging`. It does not create or flash an Android boot image.

For a full ROM with recovery-as-boot and vendor modules:

```sh
cd /home/vishwajithkv/android/lineage
export OUT_DIR=out-6.18
source build/envsetup.sh
breakfast laurel_sprout
m -j8 bacon
```

Keep the separate output directory throughout all incremental rebuilds.
To inspect artifacts produced by that ROM build:

```sh
python3 kernel/mainline/sm6125-mainline-6.18/Documentation/android/verify-artifacts.py \
    --out out-6.18/target/product/laurel_sprout/obj/KERNEL_OBJ \
    --modules-root out-6.18/target/product/laurel_sprout/vendor/lib/modules \
    --boot out-6.18/target/product/laurel_sprout/boot.img
```

The verifier checks built-in dependencies, first-boot exclusions, kernel
release, DTB identity, module vermagic and depmod metadata. With `--boot`, it
also checks header/load addresses, ramdisk presence, partition size, required
boot parameters and exact kernel/DTB correspondence with the build.
It does not validate the bootloader, ramdisk contents, AVB chain or live hardware.

## Reproduction and fallback

Keep the old 6.15 kernel checkout and ROM branch intact. The matching source
revision was `63e1f779b9c4c60409b5ac074e5f1ad9050d43af`.
Switch the ROM device tree back to `lineage-23.2-6.15` to restore its kernel
selection; use the old output directory and matching images/modules.
The device's local manifest pins this kernel's final committed revision.
Fresh network sync requires those local commits to be published first.
