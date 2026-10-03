<!-- SPDX-License-Identifier: GPL-2.0-only -->
# Xiaomi Mi A3 mainline kernel for Android

Experimental Linux 6.15 source for Xiaomi Mi A3 (`laurel_sprout`, SM6125),
with Android USB/memfd patches, kernel configuration, DTS/DTB sources,
and module integration for an existing Android device tree.

**Status:** the maintainer compiled the earlier configuration, but both
repacked boot candidates returned to fastboot. Kernel boot, ADB and device
hardware support have not been established. The latest config requires
a new maintainer build.

- [Build, Android integration and known gaps](Documentation/android/README.md)
- [Boot failures and documentation audit](Documentation/android/BOOT_AUDIT.md)
- [Pinned sources and config checksums](Documentation/android/config-sources.json)
- [Kernel/module BoardConfig snippet](Documentation/android/BoardConfigKernel.mk)

This repository publishes a complete source snapshot of SzczurekYT's
`laurel` branch at `39f1dc8c31fd87fa9179b6ed4d628fe69e0cd78a`:
<https://gitlab.postmarketos.org/SzczurekYT/linux.git>.
The snapshot does not include the upstream Git history. Android patches
retain their authorship and sign-offs; original mail patches are archived
under `Documentation/android/upstream-patches/`.

Only source is published. Generated kernels, modules and boot images,
device firmware and the downstream Android vendor image are not included.
Build and hardware validation are performed by the maintainer, as required
by [AGENTS.md](AGENTS.md).
