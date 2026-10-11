#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
"""Maintainer checks for the resolved config, kernel artifacts and v0 boot image."""
import argparse
import gzip
from pathlib import Path
import re
import struct
import sys
import zlib


def require(condition, message):
    if not condition:
        raise ValueError(message)


def check_config(out, profile):
    config = {}
    for line in (out / ".config").read_text().splitlines():
        match = re.fullmatch(r"CONFIG_(\w+)=(.*)", line)
        if match:
            config[match[1]] = match[2]
    builtins = """
        ARM64 ARM64_4K_PAGES ARCH_QCOM SMP BLK_DEV_INITRD DEVTMPFS
        SCSI BLK_DEV_SD SCSI_UFSHCD SCSI_UFSHCD_PLATFORM SCSI_UFS_QCOM
        SCSI_UFS_BSG CHR_DEV_SG PHY_QCOM_QMP_UFS SM_GCC_6125
        PINCTRL_SM6125 SPMI_MSM_PMIC_ARB MFD_SPMI_PMIC
        QCOM_SCM QCOM_SMEM QCOM_SMD_RPM QCOM_RPMPD QCOM_CLK_SMD_RPM
        REGULATOR_QCOM_SMD_RPM REGULATOR_QCOM_SPMI ARM_SMMU
        QCOM_SPMI_ADC5 QCOM_SPMI_ADC_TM5 QCOM_SPMI_TEMP_ALARM
        CHARGER_QCOM_PMI632 BATTERY_QCOM_PMI632_QG NVMEM_SPMI_SDAM
        VFAT_FS NLS_CODEPAGE_437 NLS_ISO8859_1
        EXT4_FS EXT4_FS_SECURITY F2FS_FS F2FS_FS_SECURITY F2FS_UNFAIR_RWSEM BLK_CGROUP
        TMPFS TMPFS_XATTR CONFIGFS_FS SECURITY_SELINUX
        ANDROID_BINDER_IPC ANDROID_BINDERFS FTRACE FTRACE_SYSCALLS EVENT_TRACING
        MEMFD_CREATE MEMFD_ASHMEM_SHIM
        CGROUPS CGROUP_SCHED CPUSETS CPUSETS_V1 CGROUP_CPUACCT CGROUP_BPF
        BPF_SYSCALL BPF_JIT DMABUF_HEAPS DMABUF_HEAPS_SYSTEM
        BLK_DEV_DM DM_VERITY BLK_INLINE_ENCRYPTION BLK_INLINE_ENCRYPTION_FALLBACK
        DRM DRM_SIMPLEDRM DRM_FBDEV_EMULATION DRM_CLIENT_DEFAULT_FBDEV
        INPUT_EVDEV INPUT_PM8941_PWRKEY TOUCHSCREEN_EDT_FT5X06
        I2C_QCOM_GENI QCOM_GENI_SE QCOM_GPI_DMA REGULATOR_FIXED_VOLTAGE
        REMOTEPROC QCOM_Q6V5_PAS QCOM_Q6V5_COMMON QCOM_RPROC_COMMON
        QCOM_MDT_LOADER QCOM_PIL_INFO QCOM_SYSMON QCOM_PD_MAPPER
        QCOM_QMI_HELPERS QCOM_RMTFS_MEM QCOM_SMEM QCOM_SMP2P
        RPMSG RPMSG_QCOM_GLINK RPMSG_QCOM_GLINK_SMEM QRTR QRTR_SMD
        USB USB_GADGET USB_DWC3 USB_DWC3_QCOM USB_DWC3_DUAL_ROLE
        PHY_QCOM_QUSB2 USB_ROLE_SWITCH TYPEC_QCOM_PMIC REGULATOR_QCOM_USB_VBUS
        USB_CONFIGFS USB_CONFIGFS_F_FS ANDROID_USB_CONFIGFS_UEVENT
        MODULES PSTORE PSTORE_RAM PSTORE_CONSOLE PSTORE_PMSG
        POWER_SEQUENCING POWER_SEQUENCING_QCOM_WCN
    """.split()
    for symbol in builtins:
        require(config.get(symbol) == "y", f"CONFIG_{symbol} must be built in; found {config.get(symbol, 'n')}")
    for symbol in ("CFG80211", "MAC80211", "ATH_COMMON", "ATH10K", "ATH10K_SNOC"):
        require(config.get(symbol) == "m", f"Wi-Fi requires CONFIG_{symbol}=m")
    excluded = "RUST ASHMEM MODULE_COMPRESS FRAMEBUFFER_CONSOLE MMC RTC_DRV_PM8XXX SND BT DRM_CLIENT_LOG".split()
    if profile == "native":
        native_builtins = """
            DRM_MSM DRM_MSM_KMS DRM_MSM_MDSS DRM_MSM_DPU DRM_MSM_DSI
            DRM_MSM_DSI_14NM_PHY SM_GPUCC_6125 SM_DISPCC_6125
            ARM_SMMU ARM_SMMU_QCOM BACKLIGHT_CLASS_DEVICE PM_DEVFREQ
            DEVFREQ_GOV_SIMPLE_ONDEMAND SYNC_FILE QCOM_UBWC_CONFIG
        """.split()
        for symbol in native_builtins:
            require(config.get(symbol) == "y", f"Native graphics requires CONFIG_{symbol}=y")
        require(config.get("DRM_PANEL_SAMSUNG_S6E8FCO") == "m", "Native profile requires the external panel module")
    else:
        excluded += ["DRM_MSM", "DRM_PANEL_SAMSUNG_S6E8FCO"]
    for symbol in excluded:
        require(config.get(symbol, "n") == "n", f"CONFIG_{symbol} must remain disabled for first boot")
    require(config.get("ANDROID_BINDER_DEVICES") == '"binder,hwbinder,vndbinder"', "Unexpected Binder device names")
    return config


def check_dtb(data):
    require(len(data) >= 40, "DTB is truncated")
    magic, size = struct.unpack_from(">II", data)
    require(magic == 0xD00DFEED and 40 <= size <= len(data), "Invalid DTB header/size")
    require(b"xiaomi,laurel-sprout\0" in data[:size], "DTB is not for Mi A3")
    return size


def check_boot(path, out, dtb_name):
    image = path.read_bytes()
    require(len(image) <= 67108864, "boot.img exceeds the physical 64 MiB partition")
    require(len(image) >= 1632 and image[:8] == b"ANDROID!", "Invalid Android boot image")
    kernel_size, kernel_addr = struct.unpack_from("<II", image, 8)
    ramdisk_size, ramdisk_addr = struct.unpack_from("<II", image, 16)
    page_size, header_version = struct.unpack_from("<II", image, 36)
    require(page_size == 4096 and header_version == 0, "Expected 4096-byte pages and header v0")
    require(kernel_addr == 0x8000 and ramdisk_addr == 0x01000000, "Unexpected load addresses")
    require(ramdisk_size > 0, "Combined recovery-as-boot ramdisk is missing")
    payload_end = page_size + ((kernel_size + page_size - 1) // page_size) * page_size + ramdisk_size
    require(payload_end <= len(image), "Boot payload is truncated")
    kernel = image[page_size:page_size + kernel_size]
    decoder = zlib.decompressobj(16 + zlib.MAX_WBITS)
    decoder.decompress(kernel)
    require(decoder.eof, "Compressed kernel is truncated")
    dtb = decoder.unused_data
    dtb_size = check_dtb(dtb)
    require(len(dtb) == dtb_size, "Expected exactly one appended DTB")
    require(kernel.startswith((out / "arch/arm64/boot/Image.gz").read_bytes()), "Boot image kernel differs from this build")
    require(dtb == (out / "arch/arm64/boot/dts" / dtb_name).read_bytes(), "Boot image DTB differs from this build")
    cmdline = image[64:576].split(b"\0", 1)[0] + image[608:1632].split(b"\0", 1)[0]
    for parameter in (b"androidboot.hardware=laurel_sprout", b"androidboot.boot_devices=soc@0/4804000.ufshc"):
        require(parameter in cmdline.split(), f"Missing boot parameter {parameter.decode()}")
    print(f"Verified header-v0 recovery-as-boot image: {path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True, help="Kernel output directory (ROM: obj/KERNEL_OBJ)")
    parser.add_argument("--config-only", action="store_true")
    parser.add_argument("--modules-root", type=Path, help="Directory containing this build's collected .ko files")
    parser.add_argument("--boot", type=Path, help="Optional ROM boot.img to inspect")
    parser.add_argument("--profile", choices=("simpledrm", "native"), default="simpledrm")
    args = parser.parse_args()
    dtb_name = "qcom/sm6125-xiaomi-laurel-sprout-" + ("native" if args.profile == "native" else "bringup") + ".dtb"
    config = check_config(args.out, args.profile)
    print("Resolved config retains the required built-ins and first-boot exclusions")
    if args.config_only:
        return
    kernel = (args.out / "arch/arm64/boot/Image.gz").read_bytes()
    require(len(gzip.decompress(kernel)) > 0, "Empty kernel")
    check_dtb((args.out / "arch/arm64/boot/dts" / dtb_name).read_bytes())
    release = (args.out / "include/config/kernel.release").read_text().strip()
    require(release.startswith("6.18.32"), f"Unexpected release: {release}")
    modules_root = args.modules_root or args.out / "module-staging/lib/modules" / release
    require(modules_root.is_dir(), f"Missing module directory: {modules_root}")
    require((modules_root / "modules.dep").is_file(), "Missing depmod metadata")
    modules = list(modules_root.rglob("*.ko"))
    require(modules or "m" not in config.values(), "Config requests modules but none were staged")
    require(not list(modules_root.rglob("*.ko.*")), "Compressed modules cannot be collected by this ROM setup")
    for name in ("cfg80211.ko", "mac80211.ko", "ath.ko", "ath10k_core.ko", "ath10k_snoc.ko"):
        require(any(m.name == name for m in modules), f"Missing Wi-Fi module: {name}")
    if args.profile == "native":
        require(any(m.name == "panel-samsung-s6e8fco.ko" for m in modules), "Missing freshly built Samsung panel module")
    for module in modules:
        match = re.search(rb"vermagic=([^\x00]+)", module.read_bytes())
        require(match and match[1].split()[0].decode() == release, f"Module release mismatch: {module}")
    print(f"Verified Image.gz, Mi A3 DTB and {len(modules)} matching modules for {release}")
    if args.boot:
        check_boot(args.boot, args.out, dtb_name)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, zlib.error) as error:
        print(f"Artifact check failed: {error}", file=sys.stderr)
        sys.exit(1)
