#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
# Maintainer command: build a coherent Mi A3 kernel/DTB/module set.
set -euo pipefail

kernel_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
android_root=$(CDPATH= cd -- "$kernel_root/../../.." && pwd)
dt_root="$kernel_root-devicetrees"
modules_root="$kernel_root-modules"
for source in "$dt_root/qcom/sm6125-xiaomi-laurel-sprout-bringup.dts" "$modules_root/panel/Kbuild"; do
    [[ -f "$source" ]] || { echo "Missing split source: $source" >&2; exit 1; }
done
out=${MI_A3_KERNEL_OUT:-"$android_root/out/kernel-laurel-6.18"}
llvm_bin=${LLVM_BIN:-"$android_root/prebuilts/clang/host/linux-x86/clang-r563880c/bin"}
case "$out" in
    /*) ;;
    *) echo "MI_A3_KERNEL_OUT must be an absolute path" >&2; exit 1 ;;
esac
for tool in clang clang++ ld.lld llvm-ar llvm-nm llvm-objcopy llvm-objdump llvm-readelf llvm-strip; do
    if [[ ! -x "$llvm_bin/$tool" ]]; then
        echo "Missing $llvm_bin/$tool; set LLVM_BIN to a complete LLVM toolchain" >&2
        exit 1
    fi
done
export PATH="$llvm_bin:$PATH"
mkdir -p -- "$out"
cd -- "$kernel_root"
kmake=(make ARCH=arm64 LLVM=1 HOSTCC=clang HOSTCXX=clang++ O="$out" DTC_INCLUDE="$dt_root $kernel_root/scripts/dtc/include-prefixes")

"${kmake[@]}" laurel_pmos_defconfig
./scripts/kconfig/merge_config.sh -m -O "$out" "$out/.config" \
    arch/arm64/configs/android-mainline.config \
    arch/arm64/configs/laurel_sprout.config \
    arch/arm64/configs/laurel_bringup.config \
    "$android_root/device/xiaomi/laurel_sprout/configs/ufs-bsg.config" \
    "$android_root/device/xiaomi/laurel_sprout/configs/android-boot.config"
"${kmake[@]}" olddefconfig
python3 Documentation/android/verify-artifacts.py --out "$out" --config-only
"${kmake[@]}" -j"${JOBS:-8}" Image.gz qcom/sm6125-xiaomi-laurel-sprout-bringup.dtb modules
"${kmake[@]}" M="$modules_root/panel" modules
"${kmake[@]}" modules_install INSTALL_MOD_PATH="$out/module-staging" INSTALL_MOD_STRIP=1
"${kmake[@]}" M="$modules_root/panel" modules_install INSTALL_MOD_PATH="$out/module-staging" INSTALL_MOD_STRIP=1
depmod -b "$out/module-staging" "$(cat "$out/include/config/kernel.release")"
python3 Documentation/android/verify-artifacts.py --out "$out"
echo "Kernel, bringup DTB and matching modules: $out"
echo "Use the ROM build to package recovery-as-boot and vendor modules."
