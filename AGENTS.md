# Mi A3 Google ACK 6.18 kernel

- Read Documentation/android/README.md, FIRST_BOOT.md and patch-provenance.json.
- Device is laurel_sprout, SM6125 / Snapdragon 665 / Trinket.
- Google ACK history and original patch authorship must be preserved.
- Do not build, compile, flash, run tests, erase partitions or format data;
  the maintainer performs these steps and supplies results.
- Keep boot-critical storage, USB, power and thermal dependencies built in.
- Keep the minimal SimpleDRM profile selected until Android boot parity is verified.
- Preserve memory reservations even for disabled peripherals.
- Do not add firmware blobs or downstream proprietary HAL dependencies.
- No boot or hardware support claim is validated without device logs.
