<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Mi A3 Android Common Kernel 6.18

Latest validation: build #33 restores SDAM SOC with the PM6125 RTC and
progresses to 98% on laptop USB. See [PMIC_BUILD_33.md](PMIC_BUILD_33.md)
for measurements and remaining charging limitations. Earlier candidate
descriptions below record prior stages.

Repositories, branches and revisions: [SOURCE_REPOSITORIES.md](SOURCE_REPOSITORIES.md).

Latest SOC/completion candidate (2026-10-11): [PMIC_SOC_CONTINUITY.md](PMIC_SOC_CONTINUITY.md). It supersedes the historical filesystem persistence and read-only shutdown-state descriptions below. Source-only; maintainer build and validation required.

Charging correction source (2026-10-09): see [PMIC_CHARGING_FIX.md](PMIC_CHARGING_FIX.md).
Includes QG cadence/full/recharge fixes, bounded QC/SMB1355 support and a patch
to reuse the existing AIDL Health service. Unbuilt; complete ROM rebuild and
maintainer validation required. No measured 18 W claim.

Device: Xiaomi Mi A3 (laurel_sprout), SM6125 / Snapdragon 665 / Trinket.
Branch: `mainline-6.18-split` (published unsplit baseline: `mainline-6.18`).

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

The first maintainer build failed in F2FS: ACK's read-lock hook references
`read_waiters` even when `F2FS_UNFAIR_RWSEM` is disabled. The board fragment
now enables that option, matching Google's GKI config, with `BLK_CGROUP=y`.
The resolved build config also omitted Binder because ACK now requires
`EVENT_TRACING`. The fragment enables `FTRACE` and `FTRACE_SYSCALLS` to select
the tracing dependency. The maintainer rebuilt and booted these corrections.

On 2026-10-04, the maintainer's build booted recovery and normal Android 16
on slot B with kernel `6.18.32-g47faf8ef4e7b`. Live ADB confirmed
`sys.boot_completed=1`, running Zygote and SurfaceFlinger, and the launcher
as the resumed activity. Storage, metadata and USB ADB were accessible.
Physical display usability remains unverified: the screen retained console
contents, and the composer logged unsupported DRM VSync waits. The persistent
boot logger exited with status 1; diagnostics were captured directly over ADB.
This establishes Android userspace boot, not daily-use hardware support.

CPU frequency scaling integration was prepared after recovery sideload counters
showed CPU-bound payload processing and no cpufreq policies. See CPUFREQ.md for
the attributed hardware references, SM6125 LUT limit, separate DT changes and
required maintainer validation. These new source changes are not yet boot tested.

## Native graphics profile

The ROM now defaults to the new native A610/DPU/DSI profile, using Mesa
Freedreno/Turnip, MSM minigbm and the upstream AIDL DRM composer. It also
packages the freshly built Samsung panel module in recovery-as-boot and vendor.
The maintainer's 2026-10-06 build #15 boots Android with physical native
DSI output and Freedreno FD610 GLES. Live Settings scrolling confirms DEVICE
composition for the app and status bar with no new display commit failures.
The SM6125 prepared-PLL restart correction is part of this validated build.
Vulkan execution and GPU power/performance remain unverified.
The earlier SimpleDRM milestone remains the known-working display fallback. See NATIVE_GRAPHICS.md
for firmware inputs, a separate build output, acceptance and explicit rollback.
The standalone helper still defaults to SimpleDRM; select native explicitly with
MI_A3_GRAPHICS_PROFILE=native. Profile-specific instructions supersede the
historical bringup selection statements below.

## Android compatibility

ACK supplies Android Binder, USB gadget uevents, dm-default-key and inline
encryption support. Do not apply the old USB uevent patch a second time.

ACK's ashmem and memfd ioctl path depends on Rust. This bringup profile keeps
Rust disabled, matching the established 6.15 configuration. Retain the authored
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
source support is retained. FT3518 touch and its I2C/GPI/supply dependencies
are now enabled; native display/GPU and other optional probing stay disabled.
Touch enablement awaits a maintainer build and device validation.

## Lineage integration

The ROM split branch is `lineage-23.2-6.18-split` in
https://github.com/vishwajithkv/android_device_xiaomi_laurel_sprout.
It includes `Documentation/android/BoardConfigBringup.mk` from this source tree.
Source path: `kernel/mainline/sm6125-mainline-6.18`.
DTB: `qcom/sm6125-xiaomi-laurel-sprout-bringup.dtb`.

The split build requires sibling `sm6125-mainline-6.18-devicetrees` and
`sm6125-mainline-6.18-modules` repositories. See SPLIT_SOURCES.md for source
ownership and the verified Lineage reference. This refactor is not boot-tested.
Lineage compiles the core, external DTS and selected external modules together. Fresh modules and
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
    --profile native \
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
The kernel branch is `mainline-6.18` in
https://github.com/vishwajithkv/laurel_sprout_mainline. The device branch
`lineage-23.2-6.18` carries the matching pinned manifest and ROM integration.

## Touch and physical display follow-up

The bringup fragment now builds the existing EDT FT3518 driver and GENI/GPI
transport into the kernel, so touch does not depend on vendor module loading
in either recovery or Android. The companion bringup DTS enables I2C2, QUP0,
GPI DMA0 and the existing 3.3 V touch supply. The imported GPIO/reset/interrupt
wiring and 720 x 1560 touch coordinates are unchanged. GPI DMA1 stays disabled.

FRAMEBUFFER_CONSOLE is disabled to prevent kernel text and penguins sharing
the scanout framebuffer with Android. DRM_SIMPLEDRM, DRM_FBDEV_EMULATION and
DRM_CLIENT_DEFAULT_FBDEV remain enabled for Android and recovery graphics.
Early on-screen kernel diagnostics disappear; kernel logs remain available
through ADB, the persistent logger and pstore when configured and working.
This does not enable Adreno/MDSS or fix software-rendering performance.

These changes are source-only. After rebuilding matching kernel/DTB/modules,
validate recovery graphics and touch first, then Android input events and GUI.
See TOUCH_DISPLAY.md for diagnostics if the physical GUI still fails.

Latest maintainer validation (2026-10-05): the split-source kernel boots normal
Android, FT3518 touch works in recovery, CPU frequency scaling works in Android,
recovery sideload completes, and the physical display transitions to Android.
Rendering remains software based. See CPUFREQ.md and TOUCH_DISPLAY.md for
evidence and remaining limits.

Modem source integration now carries the five attributed laurel-connectivity
patches. This is unvalidated kernel support with manual firmware startup;
Android telephony and the RMTFS userspace service remain pending. See the
companion kernel Documentation/android/MODEM.md and modem-provenance.json.

Wi-Fi source integration and its validation limits are documented in [WIFI.md](WIFI.md).

Current publication and Wi-Fi failure: [WIFI_REVIEW.md](WIFI_REVIEW.md).
Its patch inventory supersedes incomplete historical fresh-sync instructions.

PMI632 battery/normal charging source integration and unfinished Quick Charge: [PMIC_BATTERY.md](PMIC_BATTERY.md).
