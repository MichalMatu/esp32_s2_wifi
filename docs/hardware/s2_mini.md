# WEMOS/LOLIN S2 mini hardware notes

Source board page: <https://www.wemos.cc/en/latest/s2/s2_mini.html>

## Board resources

| Resource | Value |
| --- | --- |
| Module / SoC listed by WEMOS | ESP32-S2FN4R2 |
| CPU | 240 MHz |
| Flash | 4 MB |
| PSRAM | 2 MB |
| USB | Type-C, native USB OTG |
| GPIO count on board | 27 |
| Board size | 34.3 x 25.4 mm |

The 2 MB PSRAM is useful for larger buffers and diagnostics, but DMA-capable buffers,
USB/TinyUSB internals, Wi-Fi driver allocations, and some ESP-IDF subsystems can still require
internal RAM. Treat PSRAM as available extra memory, not as a drop-in replacement for every buffer.

## Pins useful in this firmware

| Function | Pin |
| --- | --- |
| BOOT button | GPIO0 |
| Status LED | GPIO15 |
| Native USB D- | GPIO19 |
| Native USB D+ | GPIO20 |
| Firmware log UART TX | GPIO37 |
| Firmware log UART RX | GPIO39 |
| Default UART0 TX | GPIO43 |
| Default UART0 RX | GPIO44 |
| EN / reset | EN |

The firmware console is configured for UART1 on GPIO37/GPIO39 so logs remain available when
native USB is occupied by USB NCM or download mode work:

- board `GPIO37 / UART1 TX` -> USB-UART adapter RX
- board `GPIO39 / UART1 RX` -> USB-UART adapter TX
- common GND
- use 3.3 V TTL only
- do not connect adapter VCC if the board is powered from USB-C

## Debug and optimization notes

- The firmware exposes runtime heap and flash values in the Diagnostics page.
- `pio run` reports static memory pressure; watch DIRAM because ESP32-S2 has limited internal RAM.
- Coredump storage uses the custom flash partition in `partitions.csv`.
- If Wi-Fi scan or USB networking becomes unstable, check internal heap first, then move non-DMA buffers to heap/PSRAM deliberately.
