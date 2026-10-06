# Boards

One firmware, three builds, picked by the PlatformIO environment. Pins: `include/pins.h`.

| | Waveshare ESP32-S3-Touch-LCD-2 | Touch-LCD-2 + W5500 Lite | Waveshare ESP32-S3-ETH |
|---|---|---|---|
| Environment | `waveshare_s3_lcd2` (default) | `waveshare_s3_lcd2_lite` | `waveshare_s3_eth` (`-e waveshare_s3_eth`) |
| Unit | fan_controller_02, 192.168.10.54 | Fan_Controller_02 (COM8, MAC ..F8:84); fanController_F924 (COM9, MAC ..F9:24) | fan_controller_01, 192.168.10.53, COM10 |
| Chip (esptool, 2026-09-27) | | | ESP32-S3 rev 0.2, 8 MB embedded PSRAM (R8, octal), 16 MB quad flash, USB-Serial/JTAG |
| W5500 | external module: SCK 12, MOSI 13, MISO 14, CS 11, INT 10, RST 9 | W5500 Lite on the right header: MOSI 9, SCK 14, CS 12, INT 11, RST 13, MISO 15 (board pins 12-7; GND 13, 3V3 1) | onboard: MOSI 11, MISO 12, SCK 13, CS 14, INT 10, RST 9 |
| Fan 1 PWM / tach | GPIO 2 / 4 | GPIO 6 / 16 | GPIO 1 / 2 |
| Fan 2 PWM / tach | GPIO 6 / 16 | GPIO 2 / 4 | GPIO 18 / 40 |
| DS18B20 | GPIO 8 | GPIO 17 | GPIO 21 |
| MicroSD | shares the LCD SPI bus: CS 41, MISO 40, MOSI 38, SCK 39 | as Touch-LCD-2 | own SPI bus (HSPI): CS 4, MISO 5, MOSI 6, SCK 7 |
| LCD / touch | ST7789T3 + CST816D | as Touch-LCD-2 | none (`HAS_LCD 0`: `display.cpp`, `touch.cpp` not built) |
| Default IP | 192.168.10.54 | 192.168.10.55 | 192.168.10.53 |
| Web page "Source File" | ETH_Touch_PWM (PlatformIO, ESP32-S3-Touch-LCD-2) | ETH_Touch_PWM (PlatformIO, ESP32-S3-Touch-LCD-2 W5500 Lite) | ETH_Touch_PWM (PlatformIO, ESP32-S3-ETH) |

W5500 Lite build (2026-09-29, the user's pins): Ethernet runs straight out of the right
header, fans and probe out of the left one. Left-header spares: GPIO 7, 8, 10, 18, 21. GPIO 19/20
are the USB port (flashing, serial log): never wire them. Its own board name, so OTA refuses
the other LCD build and the reverse: the first switch between the two is a USB flash.

All: 16 MB flash, `app3M_fat9M_16MB` partitions, OPI PSRAM, USB CDC on boot, 25 kHz PWM.
Ethernet runs on the ESP32 core's ETH driver (`src/fan_network.cpp`) since 2026-09-27; each
board uses its chip's own MAC (the ESP32-S3-ETH: `2E:84:85:53:86:65`), no longer the shared
`DE:AD:BE:EF:FE:ED`.

## Changing pins or adding a board

The firmware takes its pins from `include/pins.h` only; this file is the record. Change both.

**Moving a pin on an existing build** (e.g. the probe to another GPIO):

1. `include/pins.h`: find the build's block (`#if defined(BOARD_S3_ETH)`, `#if defined(BOARD_LCD2_LITE)`,
   or the plain Touch-LCD-2 one after `#else`) and change the number, e.g.
   `#define ONEWIRE_PIN 18`. Keep the comment beside it right.
2. This file: change the same row in the table at the top (one column per build; edit the text
   between the `|` marks, leave the marks), and the list of spare pins under it if needed.
3. Build and flash as usual (USB or OTA). Settings are kept.

Only for boards that are all wired the same way. Boards already out there get the new pins at
their next update, so a different wiring for some boards needs its own build (below).

**Pins to avoid on the Touch-LCD-2:** 19/20 (USB: flashing and the serial log), 47/48 (touch
I2C), 0, 3, 45, 46 (start-up pins), and the LCD and SD pins in `pins.h`. On any board, check
the board's pinout for pins it uses itself.

**Fans 3 and 4:** besides `PWM3_PIN`/`TACH3_PIN` etc. in `pins.h`, `src/sensors.cpp` lists the
channels (`pwmPins[]`, `tachPins[]`); 3 and 4 are `-1` there ("not wired"). Replace the `-1`s
with the new names. Then they can be ticked on the Fan Control tab.

**A new build (different wiring or a different board):** copy what `waveshare_s3_lcd2_lite`
did (commit `59d3b9b`):

1. `platformio.ini`: a new `[env:...]` that `extends = env:waveshare_s3_lcd2` and adds its own
   flag to `build_flags` (e.g. `-DBOARD_MY_NAME`). A board without an LCD also needs
   `build_src_filter` as in `waveshare_s3_eth`.
2. `include/pins.h`: a block for that flag, with its own `BOARD_NAME` (OTA refuses firmware
   with a different name, which keeps builds apart), `DEFAULT_IP_LAST_OCTET`, `HAS_LCD`, and
   every pin.
3. This file: a column in the table and a note under it.
4. Build all builds (`pio run -e <each>`); the first flash of a board onto the new build is
   over USB, since OTA refuses the other name.

## Recovering a board after a wrong flash

Firmware for the wrong board can make it crash and restart every few seconds; each restart
drops the USB port, so there's no time to flash it. The download mode in the chip's ROM can't
be broken by firmware:

1. Unplug USB (and any external power), **hold BOOT, plug USB back in**, release BOOT after
   ~2 s. The board stays on its COM port with a dark screen until it is flashed.
2. Or, without touching it: retry the flash whenever the port appears (esptool resets it into
   download mode over USB). From the repo, in Git Bash, with the right build already compiled:
   ```
   export PYTHONIOENCODING=utf-8
   for i in $(seq 1 60); do ~/.platformio/penv/Scripts/python.exe ~/.platformio/packages/tool-esptoolpy/esptool.py      --chip esp32s3 --port COM15 write_flash 0x0 .pio/build/waveshare_s3_lcd2_lite/firmware.factory.bin      && break; sleep 1; done
   ```
   `firmware.factory.bin` holds bootloader, partition table and app (written at 0x0).
   Used 2026-10-04 on the COM15 board. The flash itself doesn't touch the settings, but the
   wrong firmware may have: check the name, network and fan settings afterwards.

## Web page, login, OTA (both boards)

- Page: `web/index.html` (compiled in), API under `/api` (`src/web_server.cpp`). Tabs:
  Dashboard, Fan Control, Home Assistant, Network (Ethernet, WiFi, hotspot), History, System
  (firmware info, theme, time, OTA, device name, login, restart, factory reset).
  Opened from disk it shows a demo with made-up data. Tabs follow the URL (`/#system`).
- Login: viewing is open; every change needs it. Until one is set, changes are refused
  (setting the first one needs none). **Forgotten login:** erase the settings partition over
  USB; everything goes back to the defaults (Ethernet by DHCP, so open
  http://fancontroller-xx.local; node ID `fanController_xx`, 1 fan, MQTT login, theme, time
  zone America/Chicago, hotspot password 12345678), SD logs are kept:
  `python ~/.platformio/packages/tool-esptoolpy/esptool.py --chip esp32s3 --port COM10 erase_region 0x610000 0x9E0000`
- OTA: System tab, `.pio/build/<env>/firmware.bin`. The image's board name
  (`@@BOARD=<BOARD_NAME>@@`, `web_server.cpp`) must match the running board, so the other
  board's build is refused. From a PC: `curl -u user:pass -H "Content-Type:
  application/octet-stream" --data-binary @firmware.bin http://<ip>/api/ota`. A USB flash
  afterwards resets the boot slot to app0 (PlatformIO writes boot_app0.bin).

## WiFi backup, hotspot, device name (both boards)

- WiFi (Network tab; one tab with Ethernet since 2026-10-05) is only a backup: joined when the Ethernet link has been down for 30 s,
  left 60 s after Ethernet is back; DHCP or a static WiFi address. At boot (2026-10-04):
  no W5500 found = WiFi at once; W5500 without an address = after 10 s. MQTT reconnects at once
  when the network in use changes.
- Setup hotspot `FanController-XXXX` (last MAC bytes), page at http://192.168.4.1: starts
  after 60 s without Ethernet or WiFi (and not while a WiFi join is under way, up to 45 s), stops once one has worked for 30 s. Default password
  `12345678`; change it on the Network tab.
- Device name (mDNS, System tab): http://<name>.local on Ethernet and WiFi; default from the
  node ID (`fancontroller-01`).

## Restart, watchdog, boot events (both boards)

- **Restart** without changing anything: System tab (asks to confirm, needs the login), or on
  the LCD the gear (QR page), then hold Restart 2 s. Both log a RESTART event.
- **Watchdog** (2026-10-05): if loop() hasn't come round for 30 s the board restarts itself.
  Every restart logs a BOOT event with the reason; after anything but a cold boot it adds the
  last known state, including `stage=` (the part of loop() that was running: probe,
  dashboard, web, network, mqtt, touch, sd, loop, setup; inside those since 2026-10-06
  `net:wifi+`, `net:wifi-`, `net:ap+`, `net:ap-`, `net:dns`, `net:dhcp`, `mqtt:conn`,
  `mqtt:loop`, `mqtt:pub`, and `sd:write` for any SD card write) and `heap=` / `minHeap=`.
  `reason=TASK_WDT ... stage=mqtt` = stuck in MQTT for 30 s. The reset button and power-up
  read `POWERON` (no state). Opening the USB serial port can reset the board (`reason=USB`);
  a reader that sets DTR and RTS low before opening doesn't.
- **Self-heal** (2026-10-06, both logged as `RESTART source=self-heal` with the reason; the
  NET and MQTT events before it show what went wrong):
  - MQTT set up, Ethernet or WiFi working, but MQTT not connected for 15 minutes = restart.
    In a row it waits longer each time: 15, 30, 60, 120, 240 min. Without any network it
    doesn't restart (that wouldn't help).
  - Ethernet link up but no address (DHCP): the DHCP request is restarted after 20 s and
    every 3 min. If WiFi isn't carrying the traffic, the board restarts after 3 min, then 6,
    12 ... up to ~3 h in a row.
  - The W5500 Lite build runs the W5500 at 10 MHz (core default 20 MHz) since 2026-10-06,
    a test for the "no address" cases, all seen on that build.
- **Ethernet details in NET events** (2026-10-06): link up with speed and duplex; address with
  the time it took (normally ~4.5 s); link down, address lost with the link still up (= lease
  lost, not the cable) and every "no address" step with the DHCP client state (running /
  stopped / not started) and the W5500's own link register. "Not started" with the link up =
  the driver never asked for an address; "running" = it asked and got no answer (router,
  switch); the W5500 disagreeing with the driver = SPI trouble.
- **Slow steps** (2026-10-06): a part of loop() taking over 5 s is logged as
  `LOOP <part> took N s (last stage ...)` (History filter "Slow"); over 0.5 s only on serial.

## SD card files (both boards)

Plain CSV; the first line holds the column names (files created since 2026-09-28; a download
adds it to older files). The web page's History tab reads them (`/api/history/...`).

| File | Row |
|---|---|
| `/logs/YYYY-MM.csv` | every minute: `timestamp, local, network, blended` (°C; empty = probe failed, since 2026-09-28), `fan 1 RPM, fan 2 RPM`, then since 2026-09-28 `fan duty %, override (1/0)` |
| `/rollups/daily.csv` | per day (30 days kept): `date, local min/max, network min/max, blended min/max, fan 1 min/max, fan 2 min/max` |
| `/rollups/alltime.csv` | one row `ALL, ...`: the all-time highs and lows, same columns |
| `/events.csv` | `timestamp, category, description`: BOOT, CONFIG, OVERRIDE, OTA, DISPLAY, RESTART, SD, NET (Ethernet link/address, WiFi, hotspot, network in use), MQTT (connected, lost, failed, network temperature, HA status) |

Without a card, rows go to an internal buffer (LittleFS, 200 KB) and are written into the
current month's file when a card is back; those rows are out of time order at the end of the
file, and the History day view can miss them.

## ESP32-S3-ETH source

Pins come from its Arduino sketch `ESP32_S3_ETH_PWM_Fans_VER_1_0_1_WORKING_NO_LCD.ino`
(Arduino repo, branch `Monolithic-file-predecessor-to-main-branch-files`, commit `7411e66`),
which the board ran until 2026-09-27 (web page: built Sep 22). That sketch declares external
displays on GPIO 39/41-48, but none are fitted ("NO_LCD"); this firmware doesn't drive them.

Moving it to this firmware:
- The partition table changes (old sketch: LittleFS on the default `spiffs` partition), so
  the old settings are gone: it starts on the defaults (IP .53, node ID `fanController_xx`,
  no HA token, no MQTT login). Set them on the web page.
- Full flash backup of the Arduino build (settings, incl. the HA token, so kept outside the
  repo): `D:\GitHub\VSCodeProjects\ETH_Touch_PWM_backups\`. Restore:
  `esptool --chip esp32s3 --port COM10 write_flash 0 <file>.bin`.
- Flash reads over this board's USB fail on some blocks with the esptool stub ("Packet
  content transfer stopped"); `--no-stub` reads them (slowly).
- The backed-up Arduino build used a 4 MB layout (app0 3 MB, `spiffs` 896 KB at 0x310000).
  Its web server stalls after a PC has opened and closed its USB serial port (heavy Serial
  output blocking); power-cycle it after using USB. This firmware has the fix
  (`Serial.setTxTimeoutMs(0)`, see PROJECT_HISTORY).

Flashed 2026-09-27 (`mqtt-phase1-test` + this board variant). Checked on the board: boots,
web page at .53 (build label ESP32-S3-ETH, MQTT section), DS18B20 on GPIO 21 reads (88.6 F vs
88.1 F on the Arduino build just before), both fans run and report RPM (1151/1169, so PWM
and tach pins are right). Then, with node ID `fanController_01` and the MQTT login set: MQTT
connected, 13 discovery configs (2 fans) and all states on the broker, °C sent as UTF-8; SD
card inserted while running was detected (60 s retry); REST network temperature pull works.
