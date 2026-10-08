<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Mi A3 WCN3990 Wi-Fi candidate

Current source candidate: see [WIFI_IMPLEMENTATION_20261008.md](WIFI_IMPLEMENTATION_20261008.md). The default is deferred QMI-only bringup, not usable Wi-Fi. Upstream power-sequencing backports and capture repair are unbuilt and unvalidated. Earlier live results below describe preceding builds.

Latest source candidate (2026-10-08): stock-ordered sequential WLAN supply
enable replaces asynchronous bulk enable on Laurel. The early negotiation
vote is released at the first FW_READY before core registration, matching
stock ICNSS; normal HIF power is then acquired separately. This supersedes
the continuous handover described historically below. No device validation
exists for this change. See WIFI_BOOT_REGRESSION.md and Android-root
WIFI_FIX_REVIEW.md. No fixed-MSA-permission flag, guessed RF clock, initial
index bypass or post-WLAN_MODE sleep was introduced.

Status: re-enabled at the maintainer's request for a diagnostic build after
a reported boot freeze. WLAN/MPSS nodes and Android WLAN autoload are enabled;
RMTFS/TQFTP start with diagnostic logging. MSA uses the corrected stock 2 MiB
fixed region. Display experiments remain reverted. Factory NV stays read-only
with RMTFS -r. Both bands and boot stability are unvalidated; the exact earlier
freeze's first stalled access is CE0 MMIO offset 0x240044. Recovery does not
start the radio services. The next candidate pins L17 to stock ICNSS's
1.304 V and supplies a truthful host version in WLAN_CFG; neither correction
fixed the stall in the next device capture. The next source candidate enables
WLAN power before QMI negotiation and hands its vote to normal HIF ownership.
See WIFI_BOOT_REGRESSION.md for evidence and pending validation.

## Source ownership

The Avalon reference splits kernel, board devicetrees and external drivers.
Here ath10k, mac80211 and cfg80211 already belong to the Google ACK kernel:
keep their history there and build them as vendor modules. The external
modules repo still owns the Samsung panel; duplicating upstream ath10k there
would introduce two implementations. The devicetrees repo owns WLAN address,
interrupts, SM6125 IOMMU SID, memory reservations and board power supplies.
The ROM tree owns Android services, firmware links, regulatory packaging,
SELinux and Wi-Fi resource overlays. Recovery does not load WLAN or start MPSS.

## Firmware and transport

Laurel's stock WLAN firmware inspected on the connected device reports
`WLAN.HL.3.0.2.c1-00050-QCAHLSWMTPLZ-1`. It resides in the physical A/B modem
firmware partition alongside modem.mdt and modem.bXX. The ROM mounts the
selected partition read-only at /mnt/vendor/laurel_firmware and installs
source-defined symlinks to its files. No factory blobs are committed. Only
the existing linux-firmware firmware-5.bin metadata package is selected;
generic HL.2 wlanmdsp and board-2.bin must not replace Laurel's matching files.
MPSS firmware-name is now qcom/sm6125/xiaomi/laurel/modem.mdt so the MDT loader
requests matching modem.bXX segments directly; no header renaming is used.

MSA now exposes the full stock 2 MiB fixed region at 0x53300000. The original
candidate incorrectly reduced it to 1 MiB by interpreting the fallback
qcom,wlan-msa-memory property instead of following the stock fixed-region
driver path. Stock ICNSS reads the referenced resource size, which is 2 MiB. Stock Trinket ICNSS specifies SID 0x80 and CX/MX at 640 mV.
The ath10k changes are scoped to xiaomi,laurel-sprout: CAL-download handshake,
firmware-advertised chain counts, extended HL.3 VDEV messages, channel flags,
and AP peer creation before station VDEV start. Firmware-reported board IDs
select the corresponding stock bdwlan filename; missing files fail visibly
instead of substituting calibration from another board. These protocol and
filename adaptations remain candidates until this firmware supplies logs.
The HL.3 quiet-mode command is skipped because the reference reports firmware
crashes; this is not a replacement thermal-throttling implementation.

RMTFS publishes its QMI service and then starts MPSS by modalias. Its -P -r
mode maps named factory NV partitions read-only and shadows writes in RAM;
calibration updates do not persist across reboot. TQFTP serves firmware and
its temporary writes under /data/vendor/tmp/tqftpserv. Services are oneshot
and do not request a fatal Android reboot on failure. Telephony remains
disabled: this change does not implement RIL, IPA or mobile data.

## Android and reproducibility

Use the framework's no-vendor-HAL station path: wificond, the mainline-common
selected AIDL supplicant APEX and nl80211, with wifi.interface=wlan0. Do not
package standalone wpa_supplicant alongside that APEX: they declare the same
ISupplicant/default instance. No qcwcn HAL is advertised. The Wi-Fi
resource overlay enables 2.4/5 GHz and leaves 6 GHz disabled. Regulatory.db
and normal country handling govern allowed channels. Hotspot, vendor offload
and concurrent interfaces are outside this station-mode candidate.

Sync hardware/mainline/qcom at 27e403da346c84c3ba687fd613e8b15d61f2ba3c, then
apply the tracked local build/API/path patches from the Android root:

```
git -C hardware/mainline/qcom apply ../../../kernel/mainline/sm6125-mainline-6.18/Documentation/android/rom-patches/hardware-mainline-qcom/0001-tqftpserv-build-and-firmware-path.patch
git -C hardware/mainline/qcom apply ../../../kernel/mainline/sm6125-mainline-6.18/Documentation/android/rom-patches/hardware-mainline-qcom/0002-pil-squasher-soong-host-tool-api.patch
git -C external/zstd apply ../../kernel/mainline/sm6125-mainline-6.18/Documentation/android/rom-patches/external-zstd/0001-allow-mainline-tqftpserv.patch
```

That upstream checkout retains its authors/history. The Wi-Fi adaptations
are newly written changes based on the sources below; they are not attributed
to an invented donor commit. A complete ROM rebuild is required because a
boot image alone lacks vendor modules, services, firmware links and overlays.
The maintainer should first confirm MPSS/QRTR/WLFW discovery and wlan0 creation,
then scan and associate with separate 2.4 GHz and 5 GHz APs, verify IP/DNS and
traffic, and check disconnect/reconnect, roaming, suspend and reboot. Capture
dmesg, logcat, dumpsys wifi and iw link/station output on failure. Report actual
TX/RX rates rather than inferring one direction from the other.

## References

- [Lineage Avalon dependencies](https://github.com/LineageOS/android_device_oneplus_sm8650-common/blob/lineage-23.2/lineage.dependencies)
- [Stock Trinket DTS](https://github.com/LineageOS/android_kernel_xiaomi_sm6125/blob/lineage-23.2/arch/arm64/boot/dts/qcom/trinket.dtsi)
- [Stock WMI definitions](https://github.com/LineageOS/android_kernel_xiaomi_sm6125/blob/lineage-23.2/drivers/staging/fw-api/fw/wmi_unified.h)
- [Huabin1010 HL.3 bringup notes](https://github.com/Huabin1010/ginkgo-mainline-linux/blob/main/docs/ginkgo-wifi-complete-2026-08-18.md): protocol guidance only, not Laurel hardware validation or a published patch series.
- [Android Wi-Fi interfaces](https://source.android.com/docs/core/connect/wifi-hal)

Investigation and corrected assumptions: [WIFI_BOOT_REGRESSION.md](WIFI_BOOT_REGRESSION.md).
# CPU-stall workaround candidate (2026-10-08)

The enabled diagnostic boot reaches WLAN firmware-ready but stalls CPU 3
during initial ath10k CE setup. Laurel now uses register read indices instead
of optional DDR RRI, with initialization-only MMIO tracing to identify any
remaining stall. This is an unvalidated workaround, not confirmed Wi-Fi
support. See WIFI_BOOT_REGRESSION.md for captured stacks and acceptance.
