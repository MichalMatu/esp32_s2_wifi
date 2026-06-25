# Development roadmap

Short plan for safe, reusable improvements after v1.

## Product direction

- Build a universal ESP-IDF multi-tool firmware, not a Wi-Fi bridge-only product.
- Treat Wi-Fi bridge as one selectable operating mode.
- Keep feature modules isolated: bridge, diagnostics, OLED, HID, firewall, CSI, storage, and debug console.
- Support multiple ESP32 hardware targets over time. Keep chip-specific features behind capability checks and per-board config.
- Do not add OTA as a default feature; keep app partition space for product features.
- Support cable-based updates through ROM download mode and tooling.
- The web UI may expose firmware version, build info, coredump tools, and reboot-to-download help.
- Browser-based firmware upload requires a safe staging/OTA-style partition; do not promise it with a single-app partition.
- Prefer physical USB update flow for v1 product reliability.

## Phase 1: bridge stability

- Disable Wi-Fi power save with `esp_wifi_set_ps(WIFI_PS_NONE)`.
- Set Wi-Fi TX power explicitly with `esp_wifi_set_max_tx_power(...)`.
- Replace fixed reconnect loop with retry counters and backoff.
- Keep bridge mode active while Wi-Fi reconnects; do not fall back to config mode unless BOOT is requested or credentials are missing.
- Record last disconnect reason, RSSI, retry count, and last reconnect time.

## Phase 2: live diagnostics

- Add live RSSI and channel to `/api/status`.
- Add STA IP, gateway, netmask, and DNS to `/api/status`.
- Add USB-to-Wi-Fi and Wi-Fi-to-USB packet counters.
- Add TX/RX failure counters for both directions.
- Add bridge uptime and current bridge state.
- Add FreeRTOS task diagnostics: task name, state, priority, stack high-water mark, and optional runtime percentage.
- Expose all counters in the Diagnostics page and debug UART `status`.

## Phase 3: throughput measurement

- Add byte counters for USB-to-Wi-Fi and Wi-Fi-to-USB.
- Calculate short-window throughput, for example 1 s and 10 s.
- Show current and peak throughput in Diagnostics.
- Add UART command to print counters without opening the web panel.

## Phase 4: memory and data-path cleanup

- Keep packet forwarding path free of dynamic allocation.
- Move large scan/status snapshots to PSRAM only when they are not latency critical.
- Keep small hot-path counters in internal RAM.
- Reduce logs inside packet forwarding; use counters instead.
- Add comments only around ownership and lifetime of buffers.

## Phase 5: optional modules

- Add OLED as a status display module using the same bridge state and counters.
- Add simple firewall rules for IP/CIDR/port filtering.
- Add DNS blocking only if DNS traffic is routed through the ESP.
- Add packet injection only after passive counters and filtering are stable.

## Phase 6: HID control mode

- Add TinyUSB HID as a composite USB interface next to USB NCM.
- Support keyboard, mouse, scroll, and media-key reports.
- Keep HID locked by default after boot.
- Require physical confirmation, for example encoder click, before sending HID actions.
- Add OLED status: `HID LOCKED`, `HID READY`, selected macro, and last action.
- Add UART/debug commands only after lock state is enforced.
- Keep HID code in a separate module, for example `usb_hid_iface.c`.
- Refactor USB initialization only once, so NCM and HID share one TinyUSB device setup.

## Phase 7: Wi-Fi CSI sensing

- Add optional Wi-Fi CSI capture module, disabled by default.
- Enable CSI through ESP-IDF APIs: `esp_wifi_set_csi_rx_cb`, `esp_wifi_set_csi_config`, and `esp_wifi_set_csi`.
- Store only small rolling summaries in normal bridge mode: sample count, RSSI/noise floor, channel, amplitude stats.
- Keep raw CSI capture behind explicit debug mode because it can be high-rate and memory heavy.
- Never parse or log CSI inside the Wi-Fi callback; copy minimal data into a bounded queue or ring buffer.
- Expose summary data in Diagnostics and optional OLED views.
- Add UART commands to start/stop CSI sampling and dump a bounded sample window.
- Verify CSI does not reduce bridge throughput before enabling it outside debug builds.

## Product mode ideas

These are optional product modes. Keep each one modular and selectable.

### USB multi-tool

- USB NCM network mode.
- USB HID keyboard, mouse, scroll, and media keys.
- USB CDC/debug console where supported.
- OLED and encoder for local mode selection.
- Macro profiles stored in NVS or LittleFS.

### Wi-Fi lab dongle

- Wi-Fi scan, fast scan, roaming tests, and power-save tests.
- iPerf throughput test mode.
- RSSI/channel/noise diagnostics.
- FTM/ranging where supported.
- ESP-NOW test mode.
- Wi-Fi Aware/NAN exploration on capable targets.
- CSI lab mode with bounded capture.

### Debug and recovery dongle

- UART console and command runner.
- Reboot-to-download helper.
- Coredump download and erase.
- Heap, task, watchdog, and reset diagnostics.
- GDB stub and trace modes for debug builds.
- Flash/log export over USB or web UI.

### Industrial gateway

- RS485/Modbus RTU and Modbus TCP.
- TWAI/CAN diagnostics and forwarding.
- Wiegand reader/debug mode.
- NMEA0183 GPS parser.
- MQTT telemetry bridge.
- SNTP timestamps for logs and events.

### Smart remote and automation stick

- IR NEC transmit/receive.
- Addressable LED control.
- Buzzer/audio feedback.
- Touch or GPIO buttons.
- HID macro output.
- MQTT/web-triggered actions.

### Sensor logger

- ADC continuous capture.
- Internal temperature monitoring.
- I2C sensor discovery and polling.
- I2S recorder mode.
- LittleFS or SD card logging.
- MQTT or HTTP export.

### Audio and signal tool

- I2S capture/playback experiments.
- DAC waveform generation where supported.
- Sigma-delta DAC output.
- PWM/LEDC test outputs.
- Basic signal generator and monitor views.

### Motor and robotics controller

- Servo control.
- BLDC/Hall control.
- FOC/SVPWM experiments on capable boards.
- DShot ESC output.
- Stepper motor output.
- Ultrasonic capture and timing tools.

### Display and camera device

- OLED status UI.
- SPI LCD and touch UI where connected.
- JPEG encode/decode experiments.
- Camera/display modes on ESP32-S3 or boards with suitable camera/display hardware.

### Storage and config vault

- NVS profile storage.
- NVS encryption for credentials and secrets.
- LittleFS for larger profiles, logs, and macro files.
- Wear levelling for frequent writes.
- Partition diagnostics and export/import tools.

### Bluetooth product line

- BLE provisioning with Blufi on capable targets.
- BLE GATT server/client modes.
- BLE HID device and host modes.
- BLE SPP-like serial mode.
- BLE Mesh exploration.
- Bluetooth Classic A2DP/SPP/HID on classic ESP32 targets only.
- Wi-Fi/Bluetooth coexistence diagnostics on capable targets.

### Mesh and IoT product line

- ESP-NOW multi-device control.
- ESP Wi-Fi Mesh experiments.
- OpenThread modes on Thread-capable targets.
- Zigbee gateway/device modes on Zigbee-capable targets.
- Multi-device provisioning and local control.

### Low-level bus tool

- Soft I2C, SPI, and UART experiments.
- I2C scanner and simple register read/write.
- UART REPL/bridge modes.
- GPIO monitor and pulse tools.
- Packet/bus capture with bounded buffers.
