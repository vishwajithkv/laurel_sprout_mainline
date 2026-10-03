<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Mi A3 boot and repository audit

## Current stage

Stage 2 (kernel boots) has not passed: the maintainer reported an immediate
return to fastboot with both the header-v2/separate-DTB image and the
header-v0/appended-DTB image. Both attempts retained the experimental
no-op DTBO on slot B. There is no kernel log proving where either failed.

Read-only fastboot inspection found unlocked bootloader, active slot B,
64 MiB boot B, 24 MiB DTBO B, and successful slot A. Keep A as the fallback.
The larger DTBO transfer was explained by fastboot relocating its AVB footer
to the physical partition's end; it is not evidence of a wrong input file.

## Guides audited

| Repository/reference | Relevant guidance | Result |
|----------------------|-------------------|--------|
| vendor/mainline | docs/REPOSITORIES.md and AGENTS.md | Located the existing local repository; no clone needed |
| device/mainline/common | Bringup, kernel/patches, boot/partitions, init, debugging and optional modules docs | Shell first, distribution baseline, mainline init and USB integration |
| device/mainline/qcom-common | NEW_DEVICE.md, ADDING_A_SOC.md, optional docs | Device Linux port decides bootloader; firmware/daemons are later work |
| hardware/mainline/common | AGENTS.md and shared workflow/style/scope/review docs | No agent build/flash/tests; reuse mainline HAL implementations |
| kernel/mainline/configs | android-base-pre and conditional fragment READMEs | Fragment order preserved; resolved dependencies need maintainer review |
| device/xiaomi/mi7150-mainline | README and BoardConfig | Uses U-Boot/GRUB; its header-v4 setup does not establish Mi A3 support |
| postmarketOS Mi A3 | Wiki installation and deviceinfo | Appended DTB, clk_ignore_unused, and DTBO removal |
| postmarketOS SM6125 | APKBUILD and published config | Older 6.1 source/config; checksum-verified config migrated to 6.15 |
| Linaro Qualcomm boot guide | DTBO preparation and image packaging | Explains overlay interference and zero-filled DTBO fallback |

This is an audit of bringup-relevant documentation. It does not claim that
every Linux subsystem or later-stage HAL document has been audited.

## Missed requirements and corrections

The original build used ARM64 defconfig instead of the distribution config.
The source now starts with `laurel_pmos_defconfig`, then Android requirements,
then Mi A3 fixups. The baseline retains only symbols declared in this source;
removed symbols and the input/output checksums are recorded. The distribution
used Linux 6.1, so this migration is not a proven Linux 6.15 configuration.

F2FS security labels were missing from the compiled configuration.
`CONFIG_F2FS_FS_SECURITY=y` is now requested by the final hardware fragment.
Both changes require a new kernel/module build and resolved config review.

The original candidate's separate DTB differed from deviceinfo's appended
DTB. The second candidate appended it, but its v0 header was experimental:
deviceinfo does not explicitly state a header version. Neither candidate
establishes a working bootloader layout.

Replacing stock DTBO with our no-op overlay was not the documented Mi A3
installation step. The wiki specifies erasing DTBO. A valid overlay table
does not prove this firmware accepts it. Do not reuse the no-op DTBO as
a known-working component.

## Next diagnostic step, performed by the maintainer

Keep the existing appended-DTB candidate for this experiment so the only
changed factor is DTBO handling. With slot B still active, follow the Mi A3
wiki's DTBO removal on **B only**, then request temporary boot of the existing
candidate. These are manual commands; the agent must not execute them:

```sh
fastboot getvar current-slot
# Proceed only if this reports b. Keep the original boot/DTBO backups.
fastboot erase dtbo_b
fastboot boot /home/vishwajithkv/android/mi-a3-mainline/out/boot-mainline-appended-20261003/boot-mainline-appended.img
```

Capture the full fastboot output and any screen messages. This is an
experiment, not a confirmed fix. If erasure is rejected or firmware fails
with an empty DTBO partition, Linaro documents flashing 4096 zero bytes as
a separate fallback. Do not change both slots or combine additional kernel
changes with this diagnostic attempt.

Do not blindly transplant postmarketOS rootfs or vbmeta flashing steps onto
the installed Android system. The boot chain/AVB state still needs review
if DTBO removal does not allow the kernel to start.

## Android integration after kernel boot

Replacing only the downstream 4.14 kernel does not supply mainline userspace.
The existing Android device tree must package matching modules and load
them, provide mainline fstab/UFS paths, configure the discovered USB UDC,
and include use_memfd.rc. Use TARGET_INITIAL_BRINGUP and the common mainline
HAL defaults in a coherent Android build. The original proprietary vendor
HALs are not assumed compatible with this kernel.

The original boot ramdisk's inspected fstab has filesystem encryption but
no explicit metadata_encryption flag. DM_DEFAULT_KEY is absent in this
kernel; actual userdata compatibility requires the vendor fstab and logs.
No userdata format or repartitioning is part of this work.

## Primary references

- [Mi A3 postmarketOS wiki](https://wiki.postmarketos.org/wiki/Xiaomi_Mi_A3_%28xiaomi-laurel%29)
- [Mi A3 deviceinfo](https://gitlab.com/postmarketOS/pmaports/-/raw/master/device/testing/device-xiaomi-laurel/deviceinfo)
- [SM6125 APKBUILD](https://gitlab.com/postmarketOS/pmaports/-/raw/master/device/testing/linux-postmarketos-qcom-sm6125/APKBUILD)
- [SM6125 distribution config](https://gitlab.com/postmarketOS/pmaports/-/raw/master/device/testing/linux-postmarketos-qcom-sm6125/config-postmarketos-qcom-sm6125.aarch64)
- [Linaro Qualcomm mainline boot guide](https://www.linaro.org/blog/let-s-boot-the-mainline-linux-kernel-on-qualcomm-devices/)
