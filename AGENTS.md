# Repository Instructions

## General rule

- Do not add or preserve redundant backward-compatibility paths. When replacing an internal API, configuration shape, flag, request field, or UI contract, migrate current callers and remove old inputs, branches, constants, comments, and fallbacks in the same change unless explicitly requested otherwise.

## Project shape

- This project uses ESP-IDF through PlatformIO, not Arduino.
- Keep `sta2eth_main.c` as application orchestration only: mode selection, event wiring, reset/reconfigure flow, and bridge callback glue.
- Keep USB/NCM transport code in `usb_ncm_iface.c`. Do not put Wi-Fi credential, HTTP, profile, or UI policy there.
- Keep configuration HTTP/API, Wi-Fi scan backend, coredump HTTP endpoints, and provisioning flow in `manual_config.c`. If this file grows further, split by responsibility instead of creating a larger mixed module.
- Keep NVS profile storage in `wifi_profiles.c`, config access policy in `config_access.c`, LED behavior in `status_led.c`, and UART diagnostics/control in `debug_console.c`.
- Avoid god objects and broad cross-module globals. Expose narrow functions in headers and keep ownership of state inside the module that owns the behavior.

## ESP-IDF style

- Use `esp_err_t` returns for module APIs that can fail.
- Use `ESP_RETURN_ON_ERROR`, `ESP_ERROR_CHECK`, or explicit error handling with useful log messages. Do not silently ignore driver or allocation failures.
- Keep HTTP handlers and event callbacks short. Move long-running work to tasks or async state machines.
- Do not do heavy parsing or flash analysis in high-frequency status endpoints. Status APIs must stay fast enough to avoid watchdog resets.
- Give every task a clear name, bounded stack, and checked creation result.
- Keep log levels intentional. `DEBUG` is useful during bring-up, but noisy packet-path logs can hide real failures.
- Prefer ESP-IDF APIs and component patterns over Arduino-style wrappers or global singleton objects.
- Keep initialization order explicit: NVS, LED/logging, Wi-Fi driver, event loop, netifs, USB/NCM, then application tasks.
- Treat `ESP_ERROR_CHECK` as acceptable for unrecoverable boot/init failures. For runtime paths, return `esp_err_t`, update diagnostics, and keep the device recoverable.
- Use `sdkconfig.defaults` for project policy. Do not depend on local menuconfig-only state for behavior that must reproduce on another machine.

## ESP-IDF optimization policy

- Measure before optimizing. Use `pio run` memory summaries, UART `status`, diagnostics counters, heap tracing, and coredumps before changing architecture.
- Optimize the bridge data path first: packet callbacks must avoid heap allocation, formatting, blocking I/O, flash access, DNS/HTTP work, and verbose logging.
- Keep hot-path counters and flags in internal RAM. Use atomics or short critical sections only when shared state actually crosses tasks/callbacks.
- Prefer fixed-size buffers and bounded queues over unbounded dynamic allocation. Every queue/task/buffer allocation must have an explicit failure path.
- Use IRAM-related ESP-IDF options only for measured latency/throughput issues. IRAM improves speed but consumes scarce internal memory.
- Prefer `read`/`write` style APIs for streaming work; avoid stdio buffering and formatted output in throughput-sensitive paths.
- Disable Wi-Fi power save for bridge throughput/latency work with `esp_wifi_set_ps(WIFI_PS_NONE)` unless power consumption is the current task.
- Set Wi-Fi TX power explicitly only in one Wi-Fi setup path, document the chosen value, and keep regulatory limits in mind.
- Reconnect behavior should be state-machine based: counters, backoff, last reason, and clear user-visible state. Do not use blind resets as normal recovery.
- Add diagnostics with every optimization that changes timing, buffers, retries, or packet flow.

## PSRAM and memory

- The WEMOS/LOLIN S2 mini target has 2 MB PSRAM and 4 MB flash.
- Use PSRAM for large non-DMA buffers, diagnostics snapshots, UI/data caches, and scan result storage:
  - dynamic: `heap_caps_malloc/calloc(..., MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`
  - static: `EXT_RAM_BSS_ATTR` only for data that does not need DMA/internal RAM
- Keep DMA, TinyUSB, Wi-Fi driver, ISR-facing, and timing-critical buffers in internal RAM unless the ESP-IDF API explicitly allows PSRAM.
- Watch internal RAM pressure first. `pio run` reports DIRAM usage, and the UART `status` command reports heap/PSRAM at runtime.
- Handle allocation failures explicitly. PSRAM being present is not a guarantee that every allocation can move there.
- Do not move data to PSRAM only because it is large. Check lifetime, access frequency, DMA requirements, cache impact, and failure behavior.
- Keep `.noinit` usage rare and explicit. It is RAM that survives software reset, not persistent storage; never use it for long-lived mode/session state.
- Use NVS for persistent configuration and credentials. Use `.noinit` only for one-shot reset handoff flags.

## Networking policy

- Configuration mode must not steal the host internet route in `Local only` mode. Do not advertise gateway or DNS there.
- Captive portal behavior belongs only to the explicit captive mode.
- If credentials are missing, Wi-Fi cannot connect, or external connectivity checks fail, prefer configuration mode over a broken bridge.
- Do not redirect host traffic to the ESP when there is no useful upstream path.

## UI policy

- The firmware UI source lives in `web/`; generated assets are in `src/web_assets.h`.
- Change UI source first, then let `pio run` regenerate embedded assets.
- Keep CSS on the token system in `web/src/tokens.css`. `npm run style:check` should stay clean.
- When changing API fields, update `web/src/api.ts`, mocks, tests, and firmware handlers in the same change.

## Required checks

- Minimum firmware check: `pio run`.
- Static/web check: `sh scripts/quality.sh --static`.
- Before committing larger changes, run the strongest practical check for the touched area and record anything that could not be run.

## Documentation

- Update `README.md` or `docs/` when behavior, debug commands, wiring, partitions, memory policy, or host setup changes.
- Keep hardware facts in `docs/hardware/` and runtime/debug procedures in `docs/debugging.md`.
