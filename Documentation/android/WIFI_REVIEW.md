<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Mi A3 Wi-Fi review entry point

## Current result, 2026-10-08

Google ACK android17-6.18-2026-09_r5, Linux 6.18.32, is the base.
The maintainer built and installed kernel `6.18.32-g634b198c40e1-dirty #23`.
Android reaches boot completion in QMI-only mode. Full WLAN startup still
hangs; this publication is a reviewable bringup state, not a working Wi-Fi fix.
Neither 2.4 GHz nor 5 GHz association has been validated.

The ROM default is `LAUREL_WIFI_STAGE=qmi-only`, deferred until Android boot
completion. Firmware negotiation remains powered, but CE registration is
withheld. No wlan0/wiphy is expected in this diagnostic profile. Selecting
`full` reproduces the unresolved hardware-access failure; deferring it does
not repair the driver. Stage selection is documented in
[WIFI_IMPLEMENTATION_20261008.md](WIFI_IMPLEMENTATION_20261008.md).

## Failure and comparison

Startup proceeds through QRTR/TQFTP readiness, MPSS startup, WCN3990 power
sequencing, QMI negotiation and firmware/board download. Full mode releases
boot power, enables HIF power and starts CE initialization. The log records
a read of CE0 SRRI at offset `0x240044` at 39.379083 seconds without its
completion. CPU 7 reports an RCU stall at 60.393039 seconds; later expedited
RCU stalls include CPUs 3 and 7. The maintainer reports frozen display updates.
The last logged read strongly localizes the stall, but the electrical/access
cause is unproven. A software timeout cannot interrupt a nonreturning MMIO read.

Firmware identifies as WLAN.HL.3.0.2.c1-00050-QCAHLSWMTPLZ-1,
version 0x30298032, chip 0x120, family 0x4007, board ff, SoC 40670000.
QMI-only reaches FW_READY and Android stays running for the captured interval.
Small diagnostic excerpts are in [references/](references/); complete logs
remain local under out/wifi-full-stall-20261008 and out/wifi-qmi-checkpoint-20261008.
Hardware identifiers, account details and full private logcat dumps are not published.

The exact Laurel downstream reference is
[LineageOS sm6125 4.14](https://github.com/LineageOS/android_kernel_xiaomi_sm6125/tree/77d2912bc00eda19182a95a24bbe23543c792814).
Its SNOC register base is 0x0c800000, size 8 MiB, and CE0 SRRI offset is also
0x240044. Stock logs show the same reported firmware version reaching CE
configuration and HTC TargetReady. Matching version strings do not establish
identical firmware bytes. The stock SNOC target-sleep/link-down callbacks are
no-ops, so a PCIe force-wake sequence is not an established SNOC fix.
The stock user build permits kernel logcat but denies detailed regulator,
clock and ICNSS state inspection. Its proprietary Wi-Fi HAL is not a drop-in
replacement for the mainline ath10k path; indirect firmware/calibration setup
still needs comparison. No vendor .so cause has been demonstrated.

The [Ginkgo HL3 investigation](https://github.com/Huabin1010/ginkgo-mainline-linux/blob/main/docs/ginkgo-wifi-complete-2026-08-18.md)
is a comparison resource, not proof that board data, supplies or panel wiring
are interchangeable. Keep Laurel's 2 MiB MSA reservation and SCM permissions;
do not infer a replacement reservation from another device.

## Source ownership and authorship

* ACK owns ath10k, QMI/SNOC, power sequencing and upstream modules.
* The devicetrees companion owns Laurel WCN3990 PMU wiring, supplies and bindings.
* The modules companion owns the external Samsung panel driver; it does not
  duplicate ath10k. Its current brightness worker uses the committed 20 ms delay.
* The ROM tree owns firmware packaging, QRTR/TQFTP services, SELinux policy,
  stage selection, startup helper and persistent capture.

Google history and imported author/date/trailers are retained. The attributed
Dmitry Baryshkov WCN399x power-sequencing commits remain separate in kernel
history; local board adaptations are separate commits. See WIFI.md,
WIFI_IMPLEMENTATION_20261008.md, patch-provenance.json and companion import
history for provenance. Display frame-wait/splash experiments were reverted;
they are not part of the published active driver implementation.

## Recreating Android integration

Sync the ROM tree's local manifests with the companion kernel/DTS/modules pins.
The modifications to upstream Android repositories are carried below instead
of being pushed to LineageOS remotes. Apply each patch once from its target
repository, using an absolute patch path and `git apply --check` first.
These are exact working-tree diffs against the listed bases:

| Target repository | Base commit | Patch |
| --- | --- | --- |
| `system/core` | `eb2de7321317226bbc1951382b3171fa59bb4d1d` | [rom-patches/system-core/0001-init-recognize-legacy-normal-boot.patch](rom-patches/system-core/0001-init-recognize-legacy-normal-boot.patch) |
| `bootable/recovery` | `acd99f7c48e0229eaedd40aa3e2adcbabe2dc283` | [recovery-battery-ui-lock.patch](recovery-battery-ui-lock.patch) |
| `packages/modules/Connectivity` | `c9f3e7795256bd4a3f99d02e604e24fd1552c2b3` | [rom-patches/connectivity/0001-netbpfload-cache-elf-startup.patch](rom-patches/connectivity/0001-netbpfload-cache-elf-startup.patch) |
| `external/zstd` | `b41f7c427a3f03a975ad382ea42e77e2787007c2` | [rom-patches/external-zstd/0001-allow-mainline-tqftpserv.patch](rom-patches/external-zstd/0001-allow-mainline-tqftpserv.patch) |
| `external/mesa` | `5a6bb76986d2d111599baf18e1dcd20b5ffd02c4` | [rom-patches/external-mesa/0001-android-preserve-linker-path-components.patch](rom-patches/external-mesa/0001-android-preserve-linker-path-components.patch) |
| `hardware/mainline/common` | `6939f596fe349e0531de191eb344c90a03662d26` | [rom-patches/hardware-mainline-common/0001-drmfb-import-image-planes-and-pace-simpledrm.patch](rom-patches/hardware-mainline-common/0001-drmfb-import-image-planes-and-pace-simpledrm.patch) |
| `external/drm_hwcomposer-upstream` | `8976786556df09c42b4ad41dd93969c6792f324d` | [rom-patches/external-drm-hwcomposer-upstream/0001-enumerate-drm-cards-with-minor-gaps.patch](rom-patches/external-drm-hwcomposer-upstream/0001-enumerate-drm-cards-with-minor-gaps.patch) |

For hardware/mainline/qcom, use manifest base
`27e403da346c84c3ba687fd613e8b15d61f2ba3c` and apply, in order:

1. [TQFTP build and firmware paths](rom-patches/hardware-mainline-qcom/0001-tqftpserv-build-and-firmware-path.patch).
2. [PIL squasher Soong API](rom-patches/hardware-mainline-qcom/0002-pil-squasher-soong-host-tool-api.patch).

Use `git am` for these two mail patches to retain their author metadata.
These reproduce local commits 99943be and 53b62da. Firmware binaries and
build outputs are not included; use the documented existing firmware inputs.
A kernel-only rebuild cannot install the Connectivity/recovery/service edits.
The maintainer builds matching ROM, kernel, DTB and fresh modules and supplies
hardware results; no build, test or flash is performed for this publication.

## Next review boundary

Compare actual SNOC resource votes/accessibility at FW_READY and HIF/CE entry
with Laurel ICNSS, including SCM/MSA permissions and firmware/calibration
handshake. Do not substitute zero CE indices, guessed voltage/clock votes or
forced permissions for a demonstrated fix. Successful QMI negotiation alone
is not successful ath10k registration or WLAN connectivity.
