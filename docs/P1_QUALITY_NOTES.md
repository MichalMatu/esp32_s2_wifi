# P1 quality refactor

This change intentionally keeps the ESP-IDF/NVS persistence layer in `config_access.c` and moves only pure access-mode policy into `config_access_policy.c`.

The extracted policy covers:

- validation of supported access modes,
- parsing of `local` and `captive` names,
- gateway and DNS policy,
- user-facing names, labels, and host descriptions.

The module has no ESP-IDF dependency and is covered by a host-side C test compiled with `-std=c11 -Wall -Wextra -Werror -pedantic`. The existing firmware build remains the integration gate for the ESP32-S2 target.

## Reproducible build

The previous build configuration could resolve to a newer PlatformIO ESP32 platform and contained a generated dependency lock with an absolute path from one development machine. A clean Linux CI runner therefore could not reproduce the build.

P1 makes this portable by:

- pinning `espressif32` to `6.13.0`, which keeps this project on ESP-IDF 5.5.3,
- sourcing the ESP-IDF `dns_server` example component through a pinned Git dependency instead of `${IDF_PATH}` plus a machine-local lock entry,
- removing the stale machine-specific `dependencies.lock`; the component manager can regenerate it from the portable manifest.

GCC cannot infer across translation units that Wi-Fi profile indexes are bounded by `WIFI_PROFILE_MAX` (8), so the existing 8-byte profile-id buffer triggers a false-positive `-Wformat-truncation` warning. That warning is demoted from error only for `manual_config.c`; it remains visible, and other warnings remain build-failing.

This is deliberately narrower than a wholesale split of `manual_config.c`: provisioning HTTP state, Wi-Fi lifecycle, scan state, and NVS behavior remain unchanged in this P1 refactor.
