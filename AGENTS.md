# AGENTS.md - Mi A3 mainline Android kernel

Agents must read this file before touching this repository.

## Scope

Linux 6.15 hardware port for Xiaomi Mi A3 (`laurel_sprout`, SM6125),
with Android compatibility patches, configuration, device trees, and
module integration metadata. The Android device tree is maintained
separately; do not replace it as part of a kernel task.

## Read first

- `Documentation/android/README.md` for integration and known gaps.
- `Documentation/admin-guide/README.rst` and `Documentation/process/`.
- `device/mainline/common/docs/KERNEL.md` and `KERNEL_PATCHES.md` in
  the accompanying Android repository set.

## Hard rules

- Do not build, compile, flash, or run tests; the maintainer does.
- Search this kernel or precise Android directories, never the AOSP root.
- Preserve the existing mainline device-tree bindings and memory reservations.
- Preserve upstream authorship, SPDX licenses, and source attribution.
- Do not add firmware blobs or downstream proprietary HAL dependencies.
- Keep storage and USB boot dependencies built in; hardware enablement
  beyond the first ADB boot is validated from the maintainer's logs.
- No support claim is verified until the maintainer builds and boots it.
