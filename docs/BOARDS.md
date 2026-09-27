# Boards

One firmware, two boards, picked by the PlatformIO environment. Pins: `include/pins.h`.

| | Waveshare ESP32-S3-Touch-LCD-2 | Waveshare ESP32-S3-ETH |
|---|---|---|
| Environment | `waveshare_s3_lcd2` (default) | `waveshare_s3_eth` (`-e waveshare_s3_eth`) |
| Unit | fan_controller_02, 192.168.10.54 | fan_controller_01, 192.168.10.53, COM10 |
| Chip (esptool, 2026-09-27) | | ESP32-S3 rev 0.2, 8 MB embedded PSRAM (R8, octal), 16 MB quad flash, USB-Serial/JTAG |
| W5500 | external module: SCK 12, MOSI 13, MISO 14, CS 11, INT 10, RST 9 | onboard: MOSI 11, MISO 12, SCK 13, CS 14, INT 10, RST 9 |
| Fan 1 PWM / tach | GPIO 2 / 4 | GPIO 1 / 2 |
| Fan 2 PWM / tach | GPIO 6 / 16 | GPIO 18 / 40 |
| DS18B20 | GPIO 8 | GPIO 21 |
| MicroSD | shares the LCD SPI bus: CS 41, MISO 40, MOSI 38, SCK 39 | own SPI bus (HSPI): CS 4, MISO 5, MOSI 6, SCK 7 |
| LCD / touch | ST7789T3 + CST816D | none (`HAS_LCD 0`: `display.cpp`, `touch.cpp` not built) |
| Default IP | 192.168.10.54 | 192.168.10.53 |
| Web page "Source File" | ETH_Touch_PWM (PlatformIO, ESP32-S3-Touch-LCD-2) | ETH_Touch_PWM (PlatformIO, ESP32-S3-ETH) |

Both: 16 MB flash, `app3M_fat9M_16MB` partitions, OPI PSRAM, USB CDC on boot, 25 kHz PWM.
Ethernet runs on the ESP32 core's ETH driver (`src/fan_network.cpp`) since 2026-09-27; each
board uses its chip's own MAC (the ESP32-S3-ETH: `2E:84:85:53:86:65`), no longer the shared
`DE:AD:BE:EF:FE:ED`.

## Web page, login, OTA (both boards)

- Page: `web/index.html` (compiled in), API under `/api` (`src/web_server.cpp`). Tabs:
  Dashboard, Fan Control, Home Assistant, Network, System (firmware info, OTA, theme, login).
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

- WiFi (WiFi tab) is only a backup: joined when the Ethernet link has been down for 30 s,
  left 60 s after Ethernet is back; DHCP or a static WiFi address. MQTT reconnects at once
  when the network in use changes.
- Setup hotspot `FanController-XXXX` (last MAC bytes), page at http://192.168.4.1: starts
  after 60 s without Ethernet or WiFi, stops once one has worked for 30 s. Default password
  `12345678`; change it on the WiFi tab.
- Device name (mDNS): http://<name>.local on Ethernet and WiFi; default from the node ID
  (`fancontroller-01`).

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
