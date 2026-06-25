# Debugging and recovery

This firmware has two control paths:

- native USB-C used by USB NCM, configuration HTTP, and ROM download mode;
- external USB-UART adapter on GPIO37/GPIO39 used by the firmware debug console.

## UART debug console

Wiring:

| USB-UART adapter | ESP32-S2 |
| --- | --- |
| RXD | GPIO37, firmware UART TX |
| TXD | GPIO39, firmware UART RX |
| GND | GND |

Use 115200 8N1, 3.3 V TTL. Do not connect adapter VCC when the board is powered from USB-C.

GPIO37/GPIO39 are an application-level console. They are not the ESP32-S2 ROM UART0 pins. ROM UART0 is GPIO43 TX / GPIO44 RX, so if firmware is not running, this console cannot control the chip.

Current console commands:

| Command | Purpose |
| --- | --- |
| `help` | Print command list. |
| `status` | Print reset reason, uptime, heap, PSRAM, LED state, bridge counters, and coredump presence. |
| `wifi` | Print STA mode, SSID, connection flag, AP RSSI/channel/security, and BSSID. |
| `scan` | Start Wi-Fi scan from UART and print results. |
| `reconnect` | Disconnect and reconnect STA. |
| `disconnect` | Disconnect STA. |
| `profiles` | List saved Wi-Fi profiles without passwords. |
| `led <off|on|status|identify>` | Change LED mode. |
| `log <none|error|warn|info|debug|verbose>` | Change runtime log verbosity. |
| `reprovision` | Restart into USB configuration mode. |
| `download uart0` | Restart into ROM UART0 download mode. Use only when GPIO43/GPIO44 are wired. |
| `reboot` | Restart firmware normally. |

## Entering download mode

The firmware console cannot reliably switch the board into native USB download mode by itself. GPIO37/GPIO39 are application UART pins, not the ROM bootloader UART.

If ROM UART0 is wired on GPIO43/GPIO44, firmware can request ROM UART download mode:

```text
esp32> download uart0
```

This command sets the ESP32-S2 force-download bit and restarts. It is not useful with only GPIO37/GPIO39 connected.

Primary method for native USB download mode:

1. Hold `BOOT` / GPIO0.
2. Press and release `RESET` / `EN`.
3. Release `BOOT` after the native USB serial port appears.
4. Upload with:

```sh
pio run -t upload --upload-port /dev/cu.usbmodem01
```

If only GPIO37/GPIO39 are connected, they cannot recover a board stuck before the app console starts. For automated recovery, add hardware control of `BOOT` and `EN`, or wire ROM UART0 GPIO43/GPIO44.

## Configuration mode checks

In `Local only` mode, macOS should receive a local address without gateway or DNS:

```text
IP address: 192.168.4.2
Router: (null)
```

Useful host checks:

```sh
networksetup -getinfo "Espressif Device"
ifconfig en5
curl --interface en5 --max-time 10 http://wifi.local/api/status
curl --interface en5 --max-time 10 http://192.168.4.1/api/status
curl --interface en5 --max-time 30 -X POST http://192.168.4.1/api/wifi/scan
curl --interface en5 --max-time 10 http://192.168.4.1/api/wifi/connect
```

`en5` can differ on another Mac. Use `networksetup -listallhardwareports` to find the current device name.

The configuration panel advertises mDNS as `wifi.local` on the USB-NCM config interface. `192.168.4.1` remains the fallback address when the host does not resolve mDNS.

Wi-Fi connection validation is asynchronous:

```sh
curl --interface en5 --max-time 10 \
  -X POST \
  -H 'Content-Type: application/x-www-form-urlencoded' \
  --data 'ssid=<ssid>&password=<password>' \
  http://192.168.4.1/api/wifi/connect

curl --interface en5 --max-time 10 http://192.168.4.1/api/wifi/connect
```

The status object reports `idle`, `running`, `succeeded`, or `failed`, plus SSID, message, failure reason, IP, RSSI, channel, and whether restart to bridge mode is pending. Credentials are written to flash and saved as a profile only after the board receives an IP address from the target AP.

## Bridge mode checks

When bridge mode is working, macOS should get an address from the upstream router through the USB NCM interface, not `192.168.4.x`.

Examples:

```sh
networksetup -setdhcp "Espressif Device"
networksetup -getinfo "Espressif Device"
curl --interface en5 --max-time 8 -sS -o /dev/null -w 'http_code=%{http_code}\n' http://example.com/
ping -S <en5-ip> -c 3 <router-ip>
```

The default route should stay on the normal Mac Wi-Fi unless the user intentionally changes service order. This keeps the chat/browser connection alive during local configuration work.

## Core dumps

Core dumps are enabled to flash in `sdkconfig.defaults` and stored in the `coredump` partition from `partitions.csv`.

The web diagnostics page and `/api/status` intentionally report only lightweight coredump metadata: enabled, present, size, and error. They do not parse the ELF or panic reason during refresh, because that can block the HTTP task long enough to trip the task watchdog.

Download the stored ELF from configuration mode:

```sh
curl --interface en5 -o /tmp/esp32-s2-coredump.elf http://192.168.4.1/api/coredump
```

Analyze offline with the matching firmware ELF:

```sh
esp-coredump info_corefile --chip esp32s2 \
  --core /tmp/esp32-s2-coredump.elf \
  --core-format elf \
  .pio/build/esp32-s2-saola-1/firmware.elf
```

Erase after saving it:

```sh
curl --interface en5 -X POST http://192.168.4.1/api/coredump/erase
```

## v1 validation state

Current v1 expectations:

- `pio run` builds firmware and regenerates web assets.
- Native USB upload works when the board is in ROM download mode; if `Hash of data verified` is printed, the firmware image was written even when pySerial later fails to reset the port.
- UART console on GPIO37/GPIO39 responds to `help`, `status`, `wifi`, `scan`, `config`, and `reprovision` while firmware is running.
- PSRAM is detected as 2 MB and runtime `status` reports PSRAM free/largest block.
- Config mode advertises `http://wifi.local` through mDNS and still serves `http://192.168.4.1`.
- `Local only` mode should not advertise gateway/DNS to the Mac.
- `Captive portal` mode may advertise gateway/DNS and expose `http://wifi.settings`.
- Long BOOT press arms config mode; release BOOT after fast LED blinking to restart into config mode.
- Bridge mode can pass traffic when macOS activates the `Espressif Device` USB NCM interface.

Known caveats:

- mDNS is a convenience alias. If `wifi.local` does not resolve, use `http://192.168.4.1`.
- If macOS shows the USB NCM interface as `inactive`, HTTP and mDNS will fail regardless of firmware HTTP state.
- `download uart0` is only for ROM UART0 on GPIO43/GPIO44. GPIO37/GPIO39 are firmware console pins, not ROM bootloader pins.
