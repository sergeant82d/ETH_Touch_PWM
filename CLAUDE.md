# ETH_Touch_PWM

Two-channel 25 kHz PWM fan controller: DS18B20 probe plus a network temperature from Home
Assistant, fan curve with failsafes, SD logging, a web page (tabs, themes, login, OTA),
Home Assistant over MQTT (discovery), Ethernet with a WiFi backup and a setup hotspot, and
a touch LCD on the board that has one. Two boards from one code base (`docs/BOARDS.md`).

Started 2026-09-27 as a PlatformIO port of the Arduino IDE sketch
`ESP32_S3_FanController_2inch` (https://github.com/sergeant82d/ESP32S3-Ethernet-Fan-Controller,
commit `c23de85`); that repo stays as the Arduino reference and is not changed from here.

**Read `docs/PROJECT_HISTORY.md` first:** current status, build settings and why, Windows
build gotchas, what was learned. Open items: `docs/TODO.md`. Home Assistant (MQTT topics,
entities, the HA automations it needs): `docs/MQTT.md`. Boards, pins, OTA, login recovery,
WiFi/hotspot: `docs/BOARDS.md`. LCD design ideas (LVGL archive, top-bar icons):
`docs/LCD_DESIGN.md`. General rules (working principles, testing, git, TODO and
status reports, Windows/PlatformIO build) are in the user's global `~/.claude/CLAUDE.md`.

## Hardware

- Waveshare ESP32-S3-Touch-LCD-2: ST7789T3 240x320 LCD, CST816D touch, microSD,
  16 MB flash, OPI PSRAM, native USB (USB CDC on boot).
- External W5500 Ethernet on GPIO 9-14, on the core's ETH driver (`src/fan_network.cpp`).
  All pins are in `include/pins.h`; keep them there.
- Second board, Waveshare ESP32-S3-ETH (onboard W5500, no LCD; COM10): environment
  `waveshare_s3_eth`. Third build `waveshare_s3_lcd2_lite`: Touch-LCD-2 + W5500 Lite on the
  user's pins (COM8/COM9 boards). Differences, and how to change pins or add a build:
  `docs/BOARDS.md`. `pio run` alone builds only the Touch-LCD-2; always pass `-e` when
  uploading to the other boards.
- LCD and SD share one SPI bus. W5500 has its own.
- Power: the board locks up on PC USB power with LCD + Ethernet + SD running. Use external power.

## Web page

- `web/index.html` (HTML + CSS + JS in one file) is compiled into the firmware; `src/web_server.cpp`
  serves it and the JSON API. Don't build HTML with `client.print` again.
- Colours are CSS variables; the preset themes (NUT, Navy & gold, Classic dark, Classic light)
  are the `[data-theme]` blocks at the top; Custom is edited on the System tab.
- After editing the script, check it: extract the `<script>` block and run `node --check`.
- Open the file from disk to see it with demo data; headless Edge can screenshot it.
- Every change needs the web login; OTA refuses the other board's firmware (`docs/BOARDS.md`).

## Build

- `pio run` builds the Touch-LCD-2 (`default_envs`); `pio run -e waveshare_s3_eth` the other
  board (add `-t upload --upload-port COM10` to flash it over USB).
- Platform is pioarduino 55.03.311 = Arduino-ESP32 core 3.3.11, matching the Arduino IDE build.
- Partitions `app3M_fat9M_16MB.csv`: LittleFS lives on the `ffat` partition (see `config.cpp`).
  Changing the partition table wipes saved settings.
- Library versions in `platformio.ini` are pinned (the Arduino IDE ones, plus PubSubClient 2.8
  and the core's own SD / Ethernet (ETH) / Network libraries). Change deliberately.
- Settings are a raw struct in LittleFS (`config.h`). New fields go at the end with a version
  bump; `loadSettings()` upgrades older files in place (see the size notes in `config.cpp`).

## Rules

- The MQTT, web and WiFi passwords are entered on the web page (never sent back to it); the
  simulator's login is in git-ignored `tools/mqtt_secrets.json`; anything else goes in
  git-ignored `secrets.h`. The firmware needs no HA token since MQTT Phase 3.

## Success criteria

Since the move to MQTT (2026-09-27) the firmware does far more than the Arduino build, so it
is no longer measured against it (the original port criteria and size baseline are in
`docs/PROJECT_HISTORY.md`).

1. Both environments (`waveshare_s3_lcd2`, `waveshare_s3_eth`) build with no new warnings.
2. On each board, checked after a change that touches it:
   - Fans: PWM and RPM per active channel, fan curve, manual override, both-probes-failed
     failsafe (full speed).
   - Temperatures: local probe, network temperature from HA, blending.
   - Home Assistant: entities appear, controls work both ways, availability, daily summary.
   - Web page: every tab, login, OTA (and refusal of the other board's firmware).
   - Network: Ethernet (static and DHCP), WiFi backup and back, hotspot, device name (.local).
   - SD logging; LCD and touch (Touch-LCD-2 only).
3. Settings survive every firmware update (older settings files upgrade in place).
4. Size: the app image stays under ~2.5 MB (80 % of the 3 MB OTA slot). Free internal memory
   (System tab, "lowest ... during ...") stays above ~200 KB on Ethernet and ~120 KB while
   the WiFi backup is joined (its driver takes ~110 KB; agreed 2026-10-06). 2026-10-06:
   1.48 MB; Ethernet ~252 KB, lows 143-147 KB on WiFi (COM15).
