# ESP32-S2 USB Wi-Fi Bridge

Firmware dla topologii:

```text
router Wi-Fi <-> ESP32-S2 <-> USB <-> MacBook
```

MacBook widzi ESP32-S2 jako interfejs sieciowy USB NCM. ESP32-S2 laczy sie z routerem przez Wi-Fi i przekazuje ruch miedzy Wi-Fi STA a USB.

## PlatformIO

Projekt uzywa ESP-IDF, nie Arduino:

```ini
framework = espidf
```

Budowanie:

```sh
pio run
```

Wgrywanie:

```sh
pio run --target upload --upload-port /dev/cu.usbmodem01
```

Monitor:

```sh
pio device monitor --port /dev/cu.usbmodem01 --baud 115200
```

## Pierwsza konfiguracja Wi-Fi

Po pierwszym uruchomieniu albo po nieudanym polaczeniu ESP32-S2 uruchamia tryb konfiguracji przez USB.

1. Podlacz ESP32-S2 do MacBooka przez USB.
2. Poczekaj, az macOS wykryje nowy interfejs sieciowy USB.
3. Otworz w przegladarce:

```text
http://wifi.settings
```

4. Wpisz SSID i haslo do sieci Wi-Fi 2.4 GHz.
5. Po zapisaniu ESP32-S2 zrestartuje sie i przejdzie w tryb bridge.

## Uzycie jako internet przez USB

Po poprawnym polaczeniu z routerem MacBook powinien dostac adres IP z routera przez interfejs USB NCM.

Do testu wylacz Wi-Fi w MacBooku albo ustaw interfejs USB wyzej w kolejnosci uslug sieciowych macOS. Wtedy internet powinien isc sciezka:

```text
router Wi-Fi -> ESP32-S2 -> USB -> MacBook
```

## Reset konfiguracji

Przytrzymaj przycisk `BOOT` / GPIO0 przez okolo 2 sekundy, zeby wymusic ponowna konfiguracje Wi-Fi.

## Uwagi

- ESP32-S2 obsluguje tylko Wi-Fi 2.4 GHz.
- To jest bridge USB NCM, nie klasyczny sterownik Wi-Fi widoczny w macOS jako karta Wi-Fi.
- Konfiguracja Wi-Fi przez `http://wifi.settings` nie jest szyfrowana, wiec traktuj ja jako tryb lokalny/testowy.
