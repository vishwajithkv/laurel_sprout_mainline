> Update: the Wi-Fi candidate now selects split modem.mdt firmware and starts
> read-only RMTFS/TQFTP in Android. See [WIFI.md](WIFI.md); the earlier manual
> startup prerequisites below describe the modem-only baseline. Telephony is
> still unimplemented and MPSS/WLAN runtime validation remains pending.

<!-- SPDX-License-Identifier: GPL-2.0-only -->
# SM6125 modem bringup

Status: kernel source integration only; not built or device validated.
The previous CPU/touch/display/recovery baseline is published separately.
No calls, SMS, mobile data or modem firmware startup are claimed by this port.

## Donor and attribution

Donor: https://gitlab.postmarketos.org/SzczurekYT/linux/-/tree/laurel-connectivity
at dcd165612d7f939c0348e11edac848b434e13e0b. The five modem patches are
by Kamil Gołda <kamil.golda@protonmail.com>, not by the repository owner.
Original authors, author dates, sign-offs and original commit IDs are retained
in imported commits. modem-provenance.json maps both repositories.
The PD mapper match context is adjusted for ACK 6.18 and indentation normalized.
DTS patches are relocated into the separate devicetree repository.
Wi-Fi and the independent ramoops-address change are not imported here.

## Kernel integration

The SoC uses the PAS loader with PAS ID 4 and the existing SM6115/sc8180x
MPSS descriptor; it does not use qcom_q6v5_mss. The port supplies the modem
remoteproc, SMP2P handshake, GLINK edge, SM6125 protection-domain map and
RMTFS memory at 0xfca01000, size 2 MiB, VMID MSS_MSA. Existing reservations
are retained. The board enables remoteproc_mpss with firmware-name
qcom/sm6125/xiaomi/laurel/modem.mbn.

ACK integration adds the SM6125 PAS binding, an explicit CX power-domain name,
and built-in PAS/SMEM/SMP2P/GLINK/QRTR/PD-mapper/RMTFS dependencies. The
artifact verifier expects these dependencies to be built in. Other DSPs,
Wi-Fi, IPA and native display/GPU remain disabled. The descriptor retains
**auto_boot=false**, so probe does not automatically start modem firmware in
Android or recovery. No restart loop or recovery modem-start service is added.

## Firmware and Android requirements

The connected phone currently has only regulatory.db files in /vendor/firmware.
Its modem_a, modem_b, modemst1, modemst2, fsg and fsc partition names exist,
but their presence does not supply firmware to the PAS loader or implement
remote filesystem service. Firmware blobs must not be committed here.

Before starting MPSS, provide the matching device firmware at the DT name
above, using the mainline firmware packaging procedure for this device. A
split modem.mdt plus modem.bXX set requires either reconstruction into the
expected monolithic modem.mbn or a deliberate firmware-name/path adaptation;
simply renaming the MDT header is not sufficient. Preserve firmware provenance.

The in-kernel PD mapper replaces pd-mapper only; QCOM_RMTFS_MEM exposes shared
memory and does **not** implement the userspace RMTFS QMI file service. A
mainline-compatible RMTFS service must be integrated and running before MPSS
validation. Its storage mapping must preserve the device's calibration data;
do not erase or substitute modemst/fsg/fsc to make initialization succeed.

Android still intentionally has ro.radio.noril=true and no working radio HAL.
This donor branch contains no Android radio HAL. Enabling the existing stub
rild or reusing a downstream proprietary RIL would not establish compatible
telephony. Calls/SMS require a mainline QMI/QRTR radio integration; mobile data
also needs an appropriate modem data path (this port does not add IPA). These
are subsequent userspace/data-plane work, not results of kernel probe.

## Maintainer validation

Build a matching kernel/DTB/module set using the existing Lineage integration.
Check normal Android boot, recovery sideload, touch and physical GUI first.
Collect dmesg for remoteproc, PAS, SCM, GLINK, QRTR, PD mapper and RMTFS; verify
that MPSS registers offline with the expected firmware name and that the
RMTFS memory device is exposed. Enumerate remoteprocs by name rather than
assuming a fixed remoteproc index. Do not start other DSPs.

After firmware and RMTFS service are prepared, the maintainer can explicitly
start the MPSS remoteproc through its state attribute and capture authentication,
ready/handover, firmware crash reason and QRTR service discovery. Do not retry
a failing authenticated load repeatedly without examining the first failure.
Verify that stopping/restarting the modem does not disturb Android or recovery.
Until these results are supplied, source support remains unvalidated.

Agents do not build or flash under this repository's AGENTS.md.
