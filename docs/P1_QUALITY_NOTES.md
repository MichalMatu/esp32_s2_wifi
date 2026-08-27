# P1 quality refactor

P1 deliberately improves one low-risk architectural boundary without changing USB NCM, provisioning, Wi-Fi connection, scan, NVS, or HTTP endpoint behavior.

The ESP-IDF/NVS persistence layer stays in `config_access.c`. Pure access-mode policy lives in `config_access_policy.c` and covers:

- validation of supported access modes,
- parsing of `local` and `captive` names,
- gateway and DNS policy,
- user-facing names, labels, and host descriptions.

The policy module has no ESP-IDF dependency and is covered by a host-side C test compiled with `-std=c11 -Wall -Wextra -Werror -pedantic`. The full ESP32-S2 firmware build remains the integration gate.

## Reproducible build boundary

The previous configuration could silently resolve to a newer PlatformIO ESP32 platform and the tracked component lock contained an absolute path from one development machine. A clean Linux runner therefore could not reproduce the build.

P1 makes the build boundary explicit:

- PlatformIO Core is pinned in CI to `6.1.19`;
- PlatformIO `espressif32` is pinned to `6.13.0`;
- ESP-IDF is constrained to `5.5.3`;
- `espressif/esp_tinyusb` is pinned to `1.7.6~2`;
- its managed `espressif/tinyusb` dependency is pinned to `0.19.0~3`;
- `espressif/mdns` is pinned to `1.11.2`;
- the ESP-IDF `dns_server` example component is sourced from the `v5.5.3` Git tag and its exact subdirectory;
- the stale machine-specific `dependencies.lock` is removed and ignored because it is generated from the portable pinned manifest;
- frontend dependencies are installed from `web/package-lock.json` with `npm ci`.

GitHub Actions uses `actions/checkout@v7.0.1` and `actions/setup-python@v7.0.0`, an Ubuntu 24.04 runner, read-only repository permissions, a bounded job timeout, and concurrency cancellation for superseded runs.

## Compiler diagnostic exception

`WIFI_PROFILE_MAX` is 8, so profile indexes emitted by `profiles_handler` are always in the range 0..7. GCC does not infer this bound across the `wifi_profiles_load()` call and reports a possible `snprintf` truncation for the existing 8-byte ID buffer.

P1 keeps that diagnostic visible but demotes it from error only for `manual_config.c`. The exception is deliberately source-local; all other warnings remain build-failing. Removing the exception cleanly is included in P2 together with extraction and host testing of the form/profile presentation helpers, rather than making an isolated large-file edit in this refactor.

## P1 verification contract

A merge-ready P1 must pass all of the following on a clean GitHub Actions runner:

- full `pio run` firmware build for ESP32-S2;
- strict host C tests for config-access policy;
- frontend TypeScript checks, regression tests, style checks, and production build;
- `cppcheck` with warnings/style/performance/portability enabled;
- `markdownlint` for the README and P1/P2 engineering notes.

This remains intentionally narrower than a wholesale split of `manual_config.c`. The next architectural step is specified separately in `docs/P2_SCOPE.md`.
