# ESP32-S2 USB Wi-Fi Bridge

Firmware dla topologii:

```text
router Wi-Fi <-> ESP32-S2 <-> USB <-> MacBook
```

MacBook widzi ESP32-S2 jako interfejs sieciowy USB NCM. ESP32-S2 laczy sie z routerem przez Wi-Fi i przekazuje ruch miedzy Wi-Fi STA a USB.

## Stan v1

Wersja `v1` celowo zostawia USB NCM przy sprawdzonym modelu z przykladu TinyUSB/ESP-IDF: firmware podnosi link NCM w init callbacku i po inicjalizacji interfejsu. Eksperymentalne opoznianie link-up zostalo usuniete, bo na testowanym macOS pogorszylo stan `en5 inactive`.

Zakres `v1`:

- bridge USB NCM <-> Wi-Fi STA;
- panel konfiguracji przez USB NCM;
- `Local only` jako bezpieczny domyslny tryb konfiguracji bez gateway/DNS;
- opcjonalny `Captive portal` z `wifi.settings`;
- mDNS w config mode jako `wifi.local`;
- profile Wi-Fi, skan, walidacja credentials przed zapisem;
- UART debug console na GPIO37/GPIO39;
- coredump do flash i lekkie metadane w diagnostyce.

Znane ograniczenia:

- `wifi.local` dziala tylko w config mode i tylko gdy macOS aktywuje USB NCM;
- fallback konfiguracji to zawsze `http://192.168.4.1`;
- przy problemach z AppleUSBNCM/macOS pierwszym faktem do sprawdzenia jest `ifconfig enX` i status `active/inactive`, nie mDNS.

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

Monitor firmware przez zewnetrzny USB-UART:

```sh
pio device monitor --port /dev/cu.SLAB_USBtoUART --baud 115200
```

## Debug UART i sterowanie firmware

Firmware ma osobna konsole UART1 na GPIO37/GPIO39, zeby logi i komendy byly dostepne nawet wtedy, gdy native USB pracuje jako NCM albo download mode.

Podlaczenie:

| USB-UART | ESP32-S2 |
| --- | --- |
| RXD | GPIO37, ESP TX |
| TXD | GPIO39, ESP RX |
| GND | GND |

Parametry: `115200 8N1`, 3.3 V TTL.

Najwazniejsze komendy:

- `status`: reset reason, heap, PSRAM, LED, bridge counters i coredump;
- `wifi`: aktualny STA SSID, RSSI, kanal i auth mode;
- `scan`: skan Wi-Fi bez uzycia przegladarki;
- `reprovision`: restart do trybu konfiguracji;
- `download uart0`: restart do ROM download mode po UART0, tylko gdy GPIO43/GPIO44 sa podlaczone;
- `log <none|error|warn|info|debug|verbose>`: zmiana poziomu logow w runtime.

Pelna procedura debugowania i odzyskiwania plytki jest w [docs/debugging.md](docs/debugging.md).

## Plytka i zasoby

Docelowa plytka to WEMOS/LOLIN S2 mini oparta o ESP32-S2FN4R2:

| Zasob | Wartosc |
| --- | --- |
| CPU | 240 MHz |
| Flash | 4 MB |
| PSRAM | 2 MB |
| USB | Type-C, USB OTG |
| I/O | 27 GPIO |

Zrodlo: <https://www.wemos.cc/en/latest/s2/s2_mini.html>

Informacja o 2 MB PSRAM jest istotna przy optymalizacji buforow, ale alokacje krytyczne dla DMA/sterownikow nadal moga wymagac wewnetrznej RAM. Przy zmianach pamieciowych sprawdzaj raport `pio run` oraz diagnostyke `Free heap`, `Min free heap` i `Flash chip` na stronie konfiguracji.

Dokumentacja sprzetowa, schemat, wymiary i pinout sa zebrane w [docs/README.md](docs/README.md).

## Coredump i diagnostyka

Firmware zapisuje coredump do partycji flash `coredump` z `partitions.csv`.

W UI `Diagnostics` i `/api/status` pokazuja lekkie metadane: czy coredump jest wlaczony, czy jest zapisany, rozmiar i ewentualny blad. Status nie parsuje juz ELF/panic reason przy kazdym odswiezeniu, bo to moze blokowac HTTP task i wywolac task watchdog.

Pobranie coredumpa z trybu konfiguracji:

```sh
curl --interface en5 -o /tmp/esp32-s2-coredump.elf http://192.168.4.1/api/coredump
```

Analiza offline:

```sh
esp-coredump info_corefile --chip esp32s2 \
  --core /tmp/esp32-s2-coredump.elf \
  --core-format elf \
  .pio/build/esp32-s2-saola-1/firmware.elf
```

## Jakosc kodu

Podstawowy build:

```sh
pio run
```

Statyczna analiza kodu z `src`:

```sh
sh scripts/quality.sh --static
```

Ten check uruchamia tez frontend: TypeScript, testy regresyjne i build assetu HTML.

Pelny lokalny check:

```sh
sh scripts/quality.sh
```

Hooki przed commitem:

```sh
pre-commit install
pre-commit run --all-files
```

Konfiguracje w repo:

- `.clang-format` formatuje pliki C/H;
- `.editorconfig` trzyma wspolne zasady edytora;
- `.pre-commit-config.yaml` sprawdza whitespace, YAML, konflikty, literowki i format C/H;
- GitHub Actions buduje firmware i uruchamia `cppcheck`.

## Strona konfiguracji

UI jest w `web/` jako Vite + TypeScript.

Lokalny dev server z mockowanym backendem:

```sh
cd web
npm run dev
```

Regresje UI:

```sh
cd web
npm run style:check
npm run check
npm run test
```

Build firmware generuje `src/web_assets.h` automatycznie przez `scripts/build_web.py`.

Strona ma:

- menu z podstronami `Wi-Fi`, `Profiles`, `Diagnostics`, `Settings`, `Help`;
- skaner Wi-Fi 2.4 GHz z przewijana lista sieci;
- zapisane profile Wi-Fi w NVS;
- szybkie polaczenie z zapisanym profilem;
- walidacja polaczenia Wi-Fi przed restartem do bridge mode;
- mDNS w trybie konfiguracji: `http://wifi.local`;
- maskowanie hasel z opcja pokazania ich na stronie.

Style sa oparte o tokeny w `web/src/tokens.css`. `npm run style:check` blokuje surowe kolory poza tokenami, inline style, nietokenizowane promienie, nietokenizowane cienie i `!important`.

## Pierwsza konfiguracja Wi-Fi

Po pierwszym uruchomieniu albo po nieudanym polaczeniu ESP32-S2 uruchamia tryb konfiguracji przez USB.

1. Podlacz ESP32-S2 do MacBooka przez USB.
2. Poczekaj, az macOS wykryje nowy interfejs sieciowy USB.
3. Otworz w przegladarce:

```text
http://wifi.local
```

Jesli mDNS nie odpowiada na hoście, uzyj adresu IP:

```text
http://192.168.4.1
```

1. Wpisz SSID i haslo do sieci Wi-Fi 2.4 GHz.
2. Kliknij `Save and connect`.
3. Panel pokazuje wynik testu: stan, komunikat, IP, RSSI, kanal albo powod rozlaczenia.
4. Dopiero po poprawnym polaczeniu i otrzymaniu IP ESP32-S2 zapisuje credentials, czeka chwile na pokazanie wyniku i restartuje sie do bridge mode.

W obecnym firmware:

- konfiguracja SSID/hasla dziala tylko w trybie konfiguracji;
- domyslny adres konfiguracji to `http://wifi.local`, a fallback to `http://192.168.4.1`;
- recznie wpisane SSID/haslo trafia do listy zapisanych profili po poprawnej walidacji polaczenia;
- zeby wejsc ponownie w konfiguracje, przytrzymaj `BOOT` az LED zacznie szybko migac, potem pusc przycisk albo zrob tryb reprovision/reset konfiguracji.

## Tryb dostepu do konfiguracji

Tryb wybierasz na stronie konfiguracji w sekcji `USB config access`.

- `Local only`: konfiguracja przez `http://wifi.local` albo `http://192.168.4.1`; ESP nie podaje gateway ani DNS, wiec macOS nie powinien przelaczac internetu na ESP.
- `Captive portal`: konfiguracja przez `http://wifi.local`, `http://wifi.settings` albo `http://192.168.4.1`; ESP podaje gateway/DNS, wiec macOS moze chwilowo przekierowac internet na ESP.

Zmiana trybu zapisuje sie w NVS i dziala od nastepnego wejscia w tryb konfiguracji.

## Jezyk strony konfiguracji

Strona konfiguracji w firmware jest po angielsku. Nie mieszamy tekstow PL/EN w UI.

Jesli beda potrzebne dwa jezyki, trzeba dodac osobny mechanizm i18n zamiast dopisywac tlumaczenia recznie w tych samych widokach.

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

- Najpewniejsza metoda: wejdz ESP32-S2 w download mode recznie: trzymaj `BOOT`, nacisnij i pusc `RESET/EN`, pusc `BOOT` po pojawieniu sie portu.
- Komenda UART `download uart0` ma sens tylko z podlaczonym ROM UART0 na GPIO43/GPIO44. Konsola firmware GPIO37/GPIO39 nie jest ROM bootloaderem.
- Wybierz port `/dev/cu.usbmodem01`.
- W PlatformIO strzalka w prawo oznacza upload.
- Jesli w logu jest `Hash of data verified`, kod zostal wgrany.
- Po uploadzie nacisnij `RESET/EN` bez trzymania `BOOT`.

## Normalne zachowanie USB

- Po starcie firmware port serial moze zniknac.
- ESP32-S2 pojawia sie jako USB Ethernet/NCM.
- W macOS nie bedzie widoczny jako karta Wi-Fi.

## Drugi ESP32 jako AP

ESP32-S2 moze polaczyc MacBooka z drugim ESP32 w trybie AP:

```text
ESP32 AP -> Wi-Fi -> ESP32-S2 -> USB -> MacBook
```

Menu drugiego ESP32 otwierasz po jego IP, np. `http://192.168.4.1` albo `http://192.168.0.16`.

OTA drugiego ESP32 zadziala tylko wtedy, gdy drugi ESP32 ma OTA w swoim firmware.

## Adresy i kolizje IP

- `http://192.168.4.1` sluzy tylko do konfiguracji ESP32-S2.
- `http://wifi.local` jest nazwa mDNS panelu ESP32-S2 w trybie konfiguracji.
- `http://wifi.settings` dziala tylko w trybie `Captive portal`.
- Menu drugiego urzadzenia otwierasz po jego IP.
- Przy kolizji adresow trzeba zmienic adres/podsiec w firmware odpowiedniego urzadzenia.

## Reset konfiguracji

Przytrzymaj przycisk `BOOT` / GPIO0 az LED zacznie szybko migac, potem pusc przycisk, zeby wymusic ponowna konfiguracje Wi-Fi.

Restart do config mode jest wykonywany dopiero po puszczeniu `BOOT`. To zapobiega przypadkowemu wejsciu w ROM download mode podczas trzymania GPIO0 w stanie niskim.

## Status LED

- LOLIN/WEMOS S2 Mini ma niebieska LED na `GPIO15`.
- LED pokazuje stan systemu wzorem migania.
- Schemat, wymiary i pinout sa w `docs/`.

## Uwagi

- ESP32-S2 obsluguje tylko Wi-Fi 2.4 GHz.
- To jest bridge USB NCM, nie klasyczny sterownik Wi-Fi widoczny w macOS jako karta Wi-Fi.
- Konfiguracja Wi-Fi przez `http://wifi.local`, `http://192.168.4.1` albo `http://wifi.settings` nie jest szyfrowana, wiec traktuj ja jako tryb lokalny/testowy.
