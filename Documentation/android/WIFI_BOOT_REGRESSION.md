<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Radio-enabled boot regression investigation, 2026-10-08

Current source candidate: see [WIFI_IMPLEMENTATION_20261008.md](WIFI_IMPLEMENTATION_20261008.md). The default is deferred QMI-only bringup, not usable Wi-Fi. Upstream power-sequencing backports and capture repair are unbuilt and unvalidated. Earlier live results below describe preceding builds.

## Fresh capture after clearing logs: same CE read stall

Boot UUID bac3e1f8-7a50-4c07-a4d6-e9936f919685 is a new failed attempt,
saved under out/wifi-fresh-after-clear-20261008/boot-debug/. Ordered supplies,
early power, FW_READY, boot-vote release and subsequent HIF power markers
confirm the stock-sequence candidate executed. Kernel.log ends after successful
WLAN_CFG at 12.347231 s, but logcat.txt retains later kernel messages: at
approximately 12.594 s WLAN_MODE succeeds and CE initialization begins its
read at offset 0x240044. No read-completion marker follows. At approximately
33.601 s RCU reports CPU 7 stalled and sends an NMI to it. Activity service
remains unavailable; logcat continues through approximately 103 s and a state
snapshot begins at monotonic 105620 ms. Pstore remains empty. Metadata still
has 8084 KiB free, so this capture is not limited by the logger free-space guard.

The sequential enable and FW_READY vote-boundary corrections did not resolve
the first CE MMIO stall. Do not infer a different failure merely because the
executing CPU changed from 6 to 7. This capture confirms the startup-stage
failure after the new sequence, but does not distinguish the underlying
power/wake, clock/interconnect or access-permission mechanism. No additional
driver workaround was applied during log collection.

## Stock-sequence candidate: persisted markers confirmed, boot still fails

Recovery copy out/wifi-stock-sequence-retry-20261008/ contains additional
persisted kernel output for boot UUID 3324b6e1-e992-4664-92f3-604da080ab64.
This is the same UUID as the preceding capture, not proof of a separately
recorded retry. It now confirms FW_READY at 12.551231 s, release of the boot
vote at 12.556214 s, sequential supply re-enables, HIF power at 12.559174 s
and successful WLAN_CFG at 12.560665 s. Boot animation accesses the graphics
allocator through 12.671909 s. No WLAN_MODE completion or CE-init marker
is persisted, so the final blocking instruction cannot be inferred from this
capture. The maintainer still sees a frozen boot-animation frame. The vote
sequence corrections executed, but have not restored Android boot.

The smaller logger captured this failed boot; pstore remains empty. Initial
read-only recovery capture ended near 12.0 s, while the later copy contains
another 3272 bytes. Do not interpret the first copy's missing FW_READY as
proof firmware never reached that stage. No further driver change was made
during capture.

## Stock-sequence follow-up: sequential votes and FW_READY release

The current source supersedes the uninterrupted handover candidate below.
Stock ICNSS enables CX/XO/RF supplies sequentially; ACK regulator_bulk_enable
uses asynchronous scheduling. Laurel now enables its existing supply list
one at a time and unwinds only acquired references if enable fails. Stock
icnss_driver_event_fw_ready_ind releases negotiation power before probing
the host driver. Our extra boot vote is now released at the first FW_READY
before core registration; HIF later acquires normal power. Release failure
stops registration. The already-registered recovery path does not cycle HIF
power. Other boards retain bulk enable behavior.

Expected markers: WLAN ordered supply ... enabled, WLAN boot power released
at firmware-ready, WLAN HIF power enabled after firmware-ready. These are
source-supported sequencing corrections, not validated fixes for the CE
stall. No new device capture exists. Shared consumers may keep rails on;
dropping our vote is not proof of a physical power cycle.

The reviewed proposal's fixed-permission flag, guessed RF clock, index-zero
patch and post-mode sleep were not applied. Stock and Ginkgo require dynamic
MSA assignment; CE0 already uses direct indices regardless of DDR RRI. The
upstream WCN3990 pwrseq target does not call the delay callback despite its
50 ms pdata value. A complete shared VDDIO/PMU conversion remains separate.
See Android-root WIFI_FIX_REVIEW.md for the cause/change/validation table.
Source review and whitespace checks only; no build, test or flash.

## Early-power follow-up: live capture unavailable

The maintainer reports another stuck boot after rebuilding the early-power
candidate. USB still enumerates as 18d1:4ee7 and adb devices reports serial
5781707fda83 in device state (transport 18). However, bounded adb exec-out
dmesg, adb shell -T dmesg and adb pull /metadata/boot-debug all timed out.
The attempted captures under out/wifi-freeze-early-power-20261008/ are empty;
they are not new boot evidence. The early-vote markers and current stalled
instruction therefore cannot be verified. Do not reuse the previous boot's
CE0/CPU6 diagnosis as confirmation of this boot's instruction.

Web review found the ChromeOS/Google Kingoftown CE-access fix, upstream
170c75d43a77dc937c58f07ecf847ba1b42ab74e, which uses Linux IRQ masking instead
of accessing CE registers during shutdown. Our snoc.c already has this
implementation. The report concerns shutdown/reboot, not the initial CE
source-index read; it is not a missing startup fix to apply again.
Reference: https://lists.openwall.net/linux-kernel/2023/11/07/571

Persistent /metadata/boot-debug and /sys/fs/pstore need to be captured from
recovery before choosing another source change. No reboot, power manipulation,
flash, build or additional workaround was performed in this investigation.

## Early WLAN power vote candidate

Internet review revisited Ginkgo's completed Wi-Fi report, rather than its
older TODO porting guide, and the Samsung WCN3990 bring-up report. Neither
report demonstrates a remedy for Laurel's first CE read hanging after a
successful WLAN_MODE response. Ginkgo's CAL/WMI changes are already carried;
its 1 MiB MSA guidance cannot override Laurel's stock fixed-resource path and
this firmware's observed request for the full 2 MiB. Keep the stock carveout,
SCM assignments and display baseline unchanged.

The stock ICNSS server-arrival path calls icnss_hw_power_on() before sending
its first QMI indication registration and MSA requests. Our host power vote
previously began only at FW_READY, after BDF/CAL had run. The next local
candidate acquires a Laurel-only extra supply/clock vote before registering
the WLFW QMI client. At HIF startup it first acquires the normal HIF vote,
then drops the extra vote so rails remain enabled across that handover.
QMI-init/modem-init error paths and removal release an unused boot vote.
Other boards retain the original sequence. This is not an always-on rail
workaround and adds no undocumented wake register writes.

This aligns the early powered negotiation interval with stock ICNSS. It does
not reproduce stock's brief vote cycle at FW_READY; uninterrupted handover is
chosen to avoid a new power interruption during initialization. Whether this
interval is required by Laurel's firmware is still a hypothesis.

Expected new markers are `WLAN boot power enabled before QMI registration`
(before QMI negotiation) and `WLAN boot power handed over to HIF` (before
WLAN_CFG/WLAN_MODE). The first CE read must complete, CE interrupts must
arrive, and Android must reach boot completion to establish a fix.

Reference reports:
- https://github.com/Huabin1010/ginkgo-mainline-linux/blob/main/docs/ginkgo-wifi-complete-2026-08-18.md
- https://github.com/Bentlybro/gts6l-mainline-linux/blob/master/docs/WIFI.md
- https://github.com/LineageOS/android_kernel_xiaomi_sm6125/blob/lineage-23.2/drivers/soc/qcom/icnss.c

This is new local code informed by the stock sequence, not an imported donor
commit. Original source history and authorship are retained. Only source
review and diff whitespace checks were performed, following AGENTS.md;
no build/test/flash or live power manipulation. Rebuild the kernel and ath10k
vendor modules through the existing ROM flow; the installed L17 DT correction
remains valid, but the running module cannot acquire this early vote retroactively.

## L17/host-version follow-up: still stalled

The maintainer's next boot was captured in `out/wifi-freeze-l17-20261008/`.
New module markers confirm this candidate is installed despite the unchanged
kernel #22 build string. At 12.860311 s, ath10k reports vdd-1.3-rfa at
1304000 uV; the other reported rails are 640000, 1800000 and 3000000 uV.
WLAN_CFG includes its host version and succeeds. WLAN_MODE succeeds at
13.109176 s. The first CE0 read at 13.109213 s is still offset 0x240044,
with no completion. CPU 6 subsequently stalls; init's live stack is blocked
in synchronize_rcu_expedited through cgroup_procs_write. No CE interrupts
are serviced. These corrections did not resolve the boot freeze.

Stock ICNSS powers WLAN before QMI indication/MSA negotiation, then cycles
its votes at FW_READY before probing QCACLD. Our SNOC path only acquires the
host's power votes after FW_READY. This is a verified sequencing difference,
not yet an established cause. Investigate power ownership and firmware wake
sequencing before introducing another MMIO access or changing CE addresses.
Do not diagnose the unavailable Activity/Input services as independent faults.
No further source workaround, build, flash or live power manipulation was
performed during this capture.

## Next candidate: stock RFA voltage and WLAN_CFG host version

Fresh live inspection still shows bootanim running, no boot completion, and
CPU 6 RCU stalls. The first unmatched CE read remains offset 0x240044.
`out/wifi-freeze-rri-off-20261008/power-debugfs.txt` records L17 enabled at
1248 mV, with its ath10k consumer making no voltage vote. Stock ICNSS's
vdd-1.3-rfa entry explicitly sets both voltage bounds to 1304000 uV before
enabling the rail. Trinket has no override for that entry. The board DTS now
pins L17 to 1304000 uV so ath10k's enable-only power path uses the stock
voltage. L8 is already at stock Trinket's 640 mV and L16 at 1800 mV; L23's
3000 mV lies within Trinket's stock CH0 range. No unrelated rails change.

Stock ICNSS also requires a non-null host version and includes it in WLAN_CFG.
Our ath10k API accepted a version argument but ignored it; the SNOC caller
passed NULL. QMI now includes the field when supplied, and Laurel passes
`ath10k-laurel-hl3`. This identifies the actual driver instead of pretending
it is the stock QCACLD version. The firmware already acknowledged WLAN_CFG
without this field, so its omission is not proven to cause the MMIO stall.

Laurel logs each enabled WLAN supply's voltage and the QMI debug log records
the host version. Existing initial CE MMIO tracing and the disabled DDR RRI
experiment stay unchanged for comparison. Disabling DDR RRI alone failed;
it is not a boot fix. No arbitrary force-wake writes, permission bypasses or
foreign-board MSA relocations are introduced.

Source references:
- https://github.com/LineageOS/android_kernel_xiaomi_sm6125/blob/lineage-23.2/drivers/soc/qcom/icnss.c
- https://github.com/LineageOS/android_kernel_xiaomi_sm6125/blob/lineage-23.2/drivers/soc/qcom/icnss_qmi.c
- https://github.com/LineageOS/android_kernel_xiaomi_sm6125/blob/lineage-23.2/arch/arm64/boot/dts/qcom/trinket.dtsi
- https://github.com/Bentlybro/gts6l-mainline-linux/blob/master/docs/WIFI.md

The other WCN3990 port documents enable-only regulator behavior, but its
Samsung-specific MSA relocation does not apply here: our SCM assignments
succeed and Laurel's fixed stock reservation is preserved.

No build, test or flash was performed, as required by repository AGENTS.md.
The maintainer must rebuild the DTB and ath10k vendor modules with the kernel.
Next logs must show L17 at 1304000 uV, the WLAN_CFG host version and a completed
CE read/init before this candidate can be called a fix. Android boot completion
and 2.4/5 GHz operation remain separate acceptance requirements.

## Evidence and limits

The maintainer reports a frozen physical boot animation with the Wi-Fi
candidate. USB ADB enumeration survived, but contemporaneous shell/logcat
requests timed out. Recovery pstore was empty. Metadata captures were from
older #16 boots which reached boot_completed; they cannot diagnose this
failure. Display experiments were reverted independently, so the succeeding
build changes more than one variable: it is not a controlled WLAN-only test.

The currently connected #22 build (compiled 2026-10-08 17:17:29 IST) reports
sys.boot_completed=1 and bootanim=stopped. Its only loaded module is the
Samsung panel. Live DT reports both WLAN and MPSS disabled; there is no MPSS
remoteproc class instance. Saved evidence is under
out/boot-stuck-20261008/wifi-investigation-current-dmesg.txt and
wifi-disabled-current-status.txt. SurfaceFlinger exposes the physical display
and application layers; its snapshot powerMode=Off is not proof of a GPU crash.
No radio services were started during this investigation.

## Confirmed integration mistake: MSA resource size

Stock Laurel uses Trinket's wlan_msa_region@53300000 reservation, size
0x200000. Although ICNSS also has qcom,wlan-msa-memory=0x100000, the fixed
region takes priority. drivers/soc/qcom/icnss.c parses
qcom,wlan-msa-fixed-region, calls of_get_address(), maps prop_size and assigns
priv->msa_mem_size=prop_size. Only the fallback allocation path reads the
1 MiB property. Our initial candidate mistakenly made the fixed region
1 MiB and reserved its second half separately, based on that fallback
property and another board's notes.

The candidate now restores the full 2 MiB fixed region, preserving the
original total reservation. This corrects a demonstrable stock mismatch;
it does not prove the mismatch caused the display freeze. Upstream ath10k's
MSA response handling validates the firmware's requested subregions before
SCM assignment, so an out-of-range response should fail registration rather
than legitimately granting access to arbitrary display memory.

Reference: https://github.com/LineageOS/android_kernel_xiaomi_sm6125/blob/lineage-23.2/drivers/soc/qcom/icnss.c
and arch/arm64/boot/dts/qcom/trinket.dtsi in that repository.
Reference file hashes are recorded in references/wifi-source-hashes.txt.

## Startup ordering and firmware dependency

Our original modules.load included ath10k_snoc on the mainline loader's init
action; TQFTP/RMTFS started only at post-fs-data. Upstream ath10k itself waits
for WLFW QMI discovery and FW_READY before registering the core, so early
module probe alone is not evidence of a bug. However, the two init `start`
commands do not establish that TQFTP's QRTR service 4096 is published before
RMTFS starts MPSS. RMTFS correctly publishes its own service before its -s
thread starts MPSS; TQFTP readiness was never verified. Nor was standalone
MPSS startup with this device's firmware/NV layout validated before WLAN
protocol changes were enabled.

WCN3990 wlanmdsp is served to MPSS over TQFTP, not loaded directly by ath10k.
The upstream firmware discussion documents modem fatal errors when the DSP
firmware/signature combination is incompatible. Use only this device's
matching modem and wlanmdsp set; no other device firmware or RMTFS address
is a valid substitute.
https://lists.infradead.org/pipermail/ath10k/2023-August/014701.html

The OnePlus 7T Pro mainline report provides a useful isolation method: first
validate MPSS with WLAN disabled, using the exact device-specific RMTFS
reservation, then enable only WLAN. Its incorrect RMTFS layout lost the
whole userspace/USB connection, illustrating why a radio failure can affect
more than the Wi-Fi interface. Its addresses must not be copied to Laurel.
https://github.com/Sr-0w/hotdog-linux-bringup/blob/main/docs/evidence/2026-08-04-mainline616-wifi-mpss.md

## Firmware protocol changes remain unverified

The Laurel-specific HL.3 CAL/VDEV/peer changes were written from Ginkgo notes
and downstream WMI definitions, not cherry-picked from a validated public
Laurel patch series. Ginkgo uses a different firmware build. Correct-looking
TLV prefixes and a successful compilation do not establish firmware ABI
compatibility. CAL empty-file replies, the 200 ms CAL-report heuristic, VDEV
stream TLVs, channel flags and peer sequencing require separate validation.
OnePlus WCN3990 reports also show that a firmware-advertised quiet command
can crash WLAN/MPSS; our thermal skip addresses that known class, but does
not prove Laurel's other commands are compatible.
https://www.mail-archive.com/ath10k@lists.infradead.org/msg17638.html
https://github.com/Huabin1010/ginkgo-mainline-linux/blob/main/docs/ginkgo-wifi-complete-2026-08-18.md

## Display comparison and next isolation

Stock WLAN SID is 0x80 (mask 1); our MDSS SID is 0x400 (mask 0), on separate
SMMU stream matches. WLAN MSA is 0x53300000..0x53500000; bootloader display
reservation is 0x5c000000..0x5cf00000. They do not overlap in the source.
The Wi-Fi diff contains no panel timings, display clocks, MDSS reset or GPU
firmware change. Shared SoC firmware/SCM failures, IRQ storms or a blocked
userspace service can still leave the last frame visible, but none has been
captured for this failed boot. Do not label it direct display corruption.

Keep WLAN and MPSS disabled in the normal build. Future maintainer isolation:

1. Establish a successful boot with the same display source, logging active.
2. Enable MPSS alone with verified firmware mount, RMTFS storage mapping and
   TQFTP/RMTFS service readiness. Preserve factory storage; capture remoteproc,
   SCM, QRTR, crash reasons and display behavior before enabling ath10k.
3. Enable WLAN after stable MPSS, retaining the corrected stock MSA size.
   Capture WLFW negotiation and separate firmware-ready failures from host
   registration/HTT/WMI failures. Avoid simultaneous display changes.
4. Apply only firmware protocol adaptations demonstrated necessary by those
   logs, then validate scan, association and traffic on both bands.

No builds, flashing, tests or firmware startup were performed. The corrected
MSA description and disabled defaults are source-reviewed, not validated fixes
for the previously frozen animation.

## Diagnostic re-enable requested

The maintainer requested re-enabling WLAN and MPSS in the next build. Their
DT nodes, ath10k_snoc autoload and daemon starts are enabled again with the
corrected 2 MiB MSA. TQFTP -d and RMTFS -v log via stdio_to_kmsg; ath10k
post-fs-data debug mask 0x00300022 selects BOOT, SNOC, QMI and WMI, without
per-packet tracing. This does not establish TQFTP readiness or a working
firmware ABI. Display source stays at the reverted committed baseline.
This configuration supersedes the disabled-default recommendation above
for this maintainer-directed experiment. No live service start, build or
flash was performed by the agent.
# Live Wi-Fi CPU stall, 2026-10-08

## Register-index follow-up

The maintainer's next boot was captured in
`out/wifi-freeze-rri-off-20261008/`. The new module markers are present,
confirming that the workaround and instrumentation were installed, despite
the unchanged kernel build string. DDR RRI was skipped. At 12.778332 s the
first CE0 current source read-index access logged `CE init read offset
0x240044`; its completion marker never appeared. With the DT register base
0x0c800000, this is physical address 0x0ca40044. CPU 6 then stopped servicing
timer/RCU requests, and init/Zygote again blocked in RCU synchronization.
No WLAN CE interrupts arrived. ADB shell and log capture remained accessible.

Disabling DDR RRI did not fix boot and cannot be presented as a validated
solution. The stall is now localized to the very first direct CE MMIO read,
before ring programming or WLAN interface registration. Further investigation
must address WLAN register accessibility (power/wake, clock/interconnect or
firmware access permissions), rather than framework InputManager or rendering.
These possible mechanisms are not distinguished by the captured logs.

The diagnostic-enabled boot was captured in
`out/wifi-freeze-20261008/`. ADB shell and logcat worked, but boot completion
was absent. MPSS started, TQFTP delivered the stock WLAN DSP, QMI assigned
the full 2 MiB MSA, downloaded BDF, completed CAL and reported FW_READY at
12.757 s. WLAN_ENABLE completed at 13.012 s. No CE IRQ fired or wlan0 appeared.
CPU 3 then stopped servicing its timer/RCU requests; the ath10k workqueue task
564 was running there. Init was blocked in cgroup_procs_write -> synchronize_rcu
and Zygote in namespace_unlock -> synchronize_rcu_expedited. Activity/Input
services never registered. The scrcpy InputManager exception is consistent
with that incomplete framework startup, not evidence of a separate GPU fault.

The exact blocked instruction was not recoverable: CPU 3 did not answer the
RCU backtrace request and its task stack was not usable. A register-access
stall in initial CE setup is a hypothesis, not a demonstrated root cause.

The next source candidate disables optional DDR RRI on Laurel only, using
ath10k's existing register-index fallback. ce_alloc_rri now respects the
rri_on_ddr capability, so disabling it actually skips hardware programming.
The stock CE driver makes this feature conditional on ADRASTEA_RRI_ON_DDR;
this does not establish whether the shipped stock binary selected it.
Initial SNOC MMIO reads/writes are logged before access, with read completion
markers. Logging ends after CE interrupts are enabled and does not trace
normal traffic. If the workaround fails, the last unmatched read or write
narrows the next investigation. No timeout can recover a CPU blocked inside
a physical MMIO transaction; do not add an apparent software timeout around it.

Reference: https://github.com/LineageOS/android_kernel_xiaomi_sm6125/blob/lineage-23.2/drivers/staging/qca-wifi-host-cmn/hif/src/ce/ce_main.c

This candidate has not been compiled, flashed or validated. The maintainer
must rebuild matching kernel and vendor modules; a userspace-only update
cannot apply it. Acceptance requires no RCU stalls, sys.boot_completed=1,
CE init completion and wlan0. Scanning/association on both bands remains
separate validation. All previous findings below remain historical evidence.
