<!-- SPDX-License-Identifier: GPL-2.0-only -->
# WCN3990 sequencing and reliable capture candidate, 2026-10-08

Status: source implementation only. No build, compile, flash, device test,
format or erase was performed. The first full CE read remains a hardware
failure until a matching maintainer build proves otherwise.

## Evidence and source audit

Fresh failed boot bac3e1f8-7a50-4c07-a4d6-e9936f919685 reached WLAN_MODE and
stalled in CE0 source-index read offset 0x240044 around 12.594 seconds. CPU7
RCU stalls followed around 33.601 seconds. Kernel.log ended earlier at
WLAN_CFG, but logcat recorded the later failure; metadata had 8084 KiB free.
That proves a capture-path problem independently of the CE stall. It does
not prove whether a rail, clock, wake protocol or secure bus permission is
responsible. Earlier ordered-power and FW_READY vote-boundary attempts failed.

The upstream WCN399x series explicitly describes IO-first startup. Our old
ath10k supply array did not acquire IO/L9 at all, even though stock Trinket
Bluetooth names that rail. L9 also powers the panel: missing Wi-Fi ownership
is not proof L9 was electrically off. The new sequencer establishes ownership
and ordering; success remains to be measured.

Original upstream authored commits are cherry-picked intact in the kernel:

| Upstream | Carried | Subject |
| --- | --- | --- |
| a5fae429ec2a | 6810e8a2a889 | WCN3990 PMU binding |
| 0eb85f468ef5 | c06fe542a750 | WCN39xx power sequencing |
| afcf3ec615c9 | 69621b238100 | ath10k SNOC pwrseq consumer |
| c4b6ad0e14f5 | b13ef803c239 | select POWER_SEQUENCING |

Author: Dmitry Baryshkov <dmitry.baryshkov@oss.qualcomm.com>. Original
review/sign-off trailers and author dates are preserved. Local adaptations
are separate changes: VDDIO probe errors propagate PTR_ERR, Laurel defers
until its PMU is available, existing early-power references are balanced,
and a read-only qmi_only parameter withholds core registration at FW_READY.

References:
- https://patchew.org/linux/20260106-wcn3990-pwrctl-v2-0-0386204328be@oss.qualcomm.com/
- https://lists.openwall.net/linux-kernel/2026/04/23/1665
- https://github.com/LineageOS/android_kernel_xiaomi_sm6125/blob/lineage-23.2/arch/arm64/boot/dts/qcom/trinket.dtsi
- https://github.com/LineageOS/android_kernel_xiaomi_sm6125/blob/lineage-23.2/drivers/soc/qcom/icnss.c
- https://github.com/Huabin1010/ginkgo-mainline-linux/blob/main/docs/ginkgo-wifi-complete-2026-08-18.md

Stock CE MMIO base, resource size and IRQs agree with our SM6125 description.
The MSA reservation is 2 MiB at 0x53300000, with dynamic secure assignment;
do not replace it with another board's 1 MiB allocation or fixed-permission
flag. Ginkgo's firmware revision differs from Laurel's HL3 c1 firmware.
Its later calibration/WMI guidance is not evidence of our CE access cause.
The Kingoftown shutdown IRQ-access fix is already in ACK and does not fix
this startup instruction. No index-zero workaround, guessed RF clock or
post-QMI sleep is added. Upstream WCN3990 targets do not invoke the delay
callback despite the 50 ms pdata value.

## Source ownership and wiring

ACK owns ath10k and the generic sequencer. Both sequencer components are
built in; ath10k/cfg80211/mac80211 remain freshly built vendor modules.
The external modules repository still owns the panel driver; it does not
need a duplicate ath10k fork. The devicetrees repository owns the new Laurel
PMU and its carried schema, linked through the existing kernel compatibility
path. BINDING_HISTORY.json records the original binding commit and hash.

PMU: IO=L9, XO=L16, RF=L17 (1.304 V), CH0=L23. CX/L8 remains an ath10k
SoC supply. No separate CH1 input is named in stock ICNSS; the old duplicate
L23 vote is removed. Unspecified CH1 follows upstream's optional-chain model.
SWCTRL and reference clocks remain unspecified until Laurel wiring is known.
The panel's direct L9 consumer remains shared with Wi-Fi; graphics is unchanged.

The ROM launcher is a userdebug root diagnostic service, using the existing
su domain like the boot logger. Before starting RMTFS it subscribes to QRTR
and requires local service 4096 version 1 within ten seconds. RMTFS then
publishes service14 before its existing -s startup boots MPSS. Factory NV
remains shadowed by -r; no factory partitions are written. Service process
existence is not used as readiness. A timeout leaves MPSS stopped and reports
failure. The launcher refuses incompatible already-loaded ath10k parameters.

## Build profiles and validation order

The source default is deliberately **qmi-only, deferred until Android boot
completion**. It cannot connect to Wi-Fi: no wiphy/wlan0 is expected. This
checks the new sequencing and firmware path without entering the known CE
stall. The module stays out of vendor modules.load and is explicitly loaded
with dependencies and the selected parameter by laurel_wifi_start.

Use the normal documented full-ROM build command. Export these variables before
invoking it, or select them in wifi/product.mk for the corresponding profile:

| LAUREL_WIFI_STAGE | Effect |
| --- | --- |
| off | Android only; no ath10k, TQFTP, RMTFS or MPSS startup |
| mpss | TQFTP readiness, RMTFS and MPSS only; no ath10k |
| qmi-only | ath10k qmi_only=1; firmware negotiation ends at FW_READY |
| full | ath10k qmi_only=0; normal WLAN_MODE, CE, HTC/WMI and interface startup |

LAUREL_WIFI_DEFERRED=true is the default: start after sys.boot_completed=1.
LAUREL_WIFI_DEFERRED=false requests post-fs-data startup; select it only after
full deferred startup passes. Properties ro.vendor.laurel.wifi.stage and
ro.vendor.laurel.wifi.deferred identify the installed profile. These are
build-time choices; the kernel parameter is read-only after module loading.
A boot-only replacement does not update the launcher, properties or logger.
Rebuild a coherent full ROM containing kernel, DTB, modules and vendor/system_ext.
Keep a working recovery and stop blind retries if the first CE read still stalls.

Maintainer checkpoints:
1. off: five normal/recovery boots; confirm stable GUI/ADB and fresh capture.
2. mpss: QRTR4096/14 and remoteproc startup; no firmware crash or RCU stall.
3. qmi-only: WCN3990 pwrseq marker, correct MSA, successful FW_READY and
   explicit checkpoint; no CE access marker and Android remains responsive.
4. full deferred: first CE read completion, firmware/WMI initialization,
   wiphy/wlan0, wificond, nl80211, single supplicant APEX and activity service.
5. Both 2.4/5 GHz WPA2 networks: DHCP, DNS and bidirectional traffic for
   30 minutes each; twenty toggles plus suspend/resume and reboot reconnect.
6. Only then enable normal boot-time startup and repeat boot/network checks.

If full mode still stalls, preserve the before-read marker and later RCU
warnings, restore qmi-only/off, and compare stock target wake/access and
secure ownership. A software timeout cannot interrupt a bus transaction
that never returns. Do not compensate with register writes or extra voltage.
If CE succeeds but firmware later asserts, use the firmware crash/SFR evidence
to address HL3 board data/calibration and WMI ABI independently. If the kernel
interface works but Android fails, inspect framework/supplicant/VINTF/permissions.

## Capture implementation and retrieval

Kernel reading runs in a separate collector process with a bounded 1 MiB
queue and an independent disk-writer thread. Queue locking never spans I/O.
On overflow, oldest queued records are dropped and counted. /dev/kmsg sequence
gaps and EPIPE overruns are counted; all terminal read/write errors are visible.
ACK formats at most 2048 bytes per record; the collector supplies 4096 bytes.

Kernel files: kernel.log preserves the first 256 KiB; kernel-tail.log and
kernel-tail.log.1 retain two 256 KiB tail chunks. Read tail .1 before tail
current. Logcat remains independent with three 256 KiB rotating files.
collector-status.txt reports monotonic read/write times, byte/loss counters,
errno, reader/writer phases and raw waitpid statuses (-1 means not reaped).
Phase key: idle0/open1/io2/flush3/finished4/error5. Snapshot phase identifies
space/status/logcat flush/state read/state flush/rename/boot-info work. Status
is flushed before state reads; blocked snapshot phases are also sent to kmsg.
Inspect status with kernel
and logcat files, not kernel.log alone. State snapshots run in separate workers;
one stalled worker is terminated after two seconds, with no new workers until
it is reaped. A task stuck in uninterruptible I/O may ignore SIGKILL until its
kernel operation returns; kernel collection remains independent.

Files flush individually: kernel once per second, logcat/status on the
snapshot cadence. No O_DSYNC per message, global sync or metadata-wide syncfs.
The first and latest snapshots commit via rename and are capped at 48 KiB;
a partial state.pending is diagnostic evidence, not a completed snapshot.
Capture lasts five minutes, needs 3 MiB free to start and stops at a 1 MiB
reserve. Existing captures are never automatically deleted. Abrupt power loss
can still lose the most recent unflushed interval, or more if filesystem I/O
itself stalls. No logger can guarantee persistence through a hardware lockup.

Archive the complete boot-UUID directory from recovery before clearing only
its boot-debug contents for another attempt. Include boot-info, initial-state,
state, collector-status, kernel and kernel-tail files plus all logcat rotations.
Pstore remains a secondary channel. Record recovery ramoops initialization
errors and verify a harmless warm-reboot marker before changing memory layout;
reserved memory is unchanged. Do not trigger a deliberate panic for this check.
