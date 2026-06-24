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

W obecnym firmware:

- konfiguracja SSID/hasla jest przez `http://wifi.settings`;
- dziala tylko w trybie konfiguracji;
- zeby wejsc ponownie w konfiguracje, przytrzymaj `BOOT` okolo 2 sekundy albo zrob tryb reprovision/reset konfiguracji, a potem otworz `http://wifi.settings`.

## Uzycie jako internet przez USB

Po poprawnym polaczeniu z routerem MacBook powinien dostac adres IP z routera przez interfejs USB NCM.

Do testu wylacz Wi-Fi w MacBooku albo ustaw interfejs USB wyzej w kolejnosci uslug sieciowych macOS. Wtedy internet powinien isc sciezka:

```text
router Wi-Fi -> ESP32-S2 -> USB -> MacBook
```

## Sprawdzenie dzialania

- macOS powinien pokazac `Espressif Device`;
- interfejs ma status `active`;
- IP powinno przyjsc z routera, np. `192.168.0.14`;
- test: wylacz Wi-Fi MacBooka i otworz strone.

## Upload firmware

- Wejdz ESP32-S2 w download mode.
- Wybierz port `/dev/cu.usbmodem01`.
- W PlatformIO strzalka w prawo oznacza upload.
- Jesli w logu jest `Hash of data verified`, kod zostal wgrany.
- Po uploadzie nacisnij `RESET/EN` bez trzymania `BOOT`.

## Normalne zachowanie USB

- Po starcie firmware port serial moze zniknac.
- ESP32-S2 pojawia sie jako USB Ethernet/NCM.
- W macOS nie bedzie widoczny jako karta Wi-Fi.

## OLED diagnostyczny

Obslugiwany modul: `OLED 1.3" SH1106 I2C + EC11 + BACK + CONFIRM`.

Domyslne piny:

| Modul | GPIO |
| --- | --- |
| SDA | 39 |
| SCL | 37 |
| EC11 A / TRA | 33 |
| EC11 B / TRB | 18 |
| EC11 PUSH / PSH / CONFIRM | 35 |
| BACK / BAK | 16 |
| VCC | 3V3 |
| GND | GND |

- I2C adres: `0x3C`.
- Enkoder/BACK/CONFIRM zmieniaja ekran diagnostyki.
- Ekrany: status Wi-Fi, ruch USB/Wi-Fi, system.
- Monitor pokazuje, czy OLED odpowiada na I2C.
- Piny mozna zmienic w `sdkconfig.defaults`.

## Drugi ESP32 jako AP

ESP32-S2 moze polaczyc MacBooka z drugim ESP32 w trybie AP:

```text
ESP32 AP -> Wi-Fi -> ESP32-S2 -> USB -> MacBook
```

Menu drugiego ESP32 otwierasz po jego IP, np. `http://192.168.4.1` albo `http://192.168.0.16`.

OTA drugiego ESP32 zadziala tylko wtedy, gdy drugi ESP32 ma OTA w swoim firmware.

## Adresy i kolizje IP

- `http://wifi.settings` sluzy tylko do konfiguracji ESP32-S2.
- Menu drugiego urzadzenia otwierasz po jego IP.
- Przy kolizji adresow trzeba zmienic adres/podsiec w firmware odpowiedniego urzadzenia.

## Reset konfiguracji

Przytrzymaj przycisk `BOOT` / GPIO0 przez okolo 2 sekundy, zeby wymusic ponowna konfiguracje Wi-Fi.

## Uwagi

- ESP32-S2 obsluguje tylko Wi-Fi 2.4 GHz.
- To jest bridge USB NCM, nie klasyczny sterownik Wi-Fi widoczny w macOS jako karta Wi-Fi.
- Konfiguracja Wi-Fi przez `http://wifi.settings` nie jest szyfrowana, wiec traktuj ja jako tryb lokalny/testowy.
