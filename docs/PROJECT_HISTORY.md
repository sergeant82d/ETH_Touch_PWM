# Project history

How this project came to be, and what was learned (2026-09-27). Read this before starting
work; it saves re-discovering things.

## Current status (2026-10-06)

- Everything is on `main`. Three builds: `waveshare_s3_lcd2` (Touch-LCD-2 + W5500 module),
  `waveshare_s3_lcd2_lite` (Touch-LCD-2 + W5500 Lite on the user's pins), `waveshare_s3_eth`
  (ESP32-S3-ETH, no LCD). Settings version 10. Image ~1.48 MB (47 % of the 3 MB slot).
- Watchdog: a hung loop() restarts the board after 30 s and the BOOT event names where it hung
  (`docs/BOARDS.md`). The cause of the 2026-10-05 overnight freeze is still unknown.
- Event log (NET, MQTT, CONFIG, LOOP ...), self-heal restarts and Ethernet diagnostics:
  `docs/BOARDS.md`. Firmware audit and its open items: `docs/AUDIT_2026-10-06.md`, `docs/TODO.md`.
- **ESP32-S3-ETH (fan_controller_01, COM10):** fully tested up to 2026-09-28; not reflashed
  since.
- **Touch-LCD-2 W5500 Lite boards (COM8/COM9/COM14/COM15 on different days):** the bench boards
  since 2026-09-29; everything since then was tested on them (`docs/TODO.md`, Done).
- **Touch-LCD-2 (plain build):** flashed and works (user, 2026-10-04).
- **Home Assistant:** MQTT only; the network-temperature automation needs one action per
  controller name (`docs/MQTT.md`).
- Success criteria: `CLAUDE.md`. Setup of a new board: `docs/SETUP.md`.

## Origin

- Ported from the Arduino IDE sketch `ESP32_S3_FanController_2inch/` in
  https://github.com/sergeant82d/ESP32S3-Ethernet-Fan-Controller, commit `c23de85`.
  That repo stays as the Arduino reference; it is not changed from here.
- The repo held two versions of the main `.ino`:
  - `ESP32_S3_FanController_2inch.ino`: `TOUCH_ENABLED 0`, last changed 2026-09-16.
  - `TOUCH_ENABLED-COPY-INTO-ACTIVE-FILE...ino` (same content also in
    `ESP32S3-with-LCD_Remote-W5500-FanController/`): `TOUCH_ENABLED 1`, 2026-09-17.
  
  The touch version is newer and is what runs on the board, so it became `src/main.cpp`. Touch
  had been disabled only to chase overnight lockups, which turned out to be power (PC USB could
  not supply LCD + Ethernet + SD). External power fixed it.
- Changes from the Arduino source: `#include <Arduino.h>` added to `main.cpp`, and
  `SKETCH_FILENAME` (shown on the web page) set to "ETH_Touch_PWM (PlatformIO)". Nothing else.

## Build settings and why

Taken from the Arduino IDE Tools menu used for the working board:

| Setting | Value | PlatformIO |
|---|---|---|
| ESP32 core | 3.3.11 | pioarduino `55.03.311` (stock `espressif32` only has core 2.x) |
| Flash size | 16 MB | `board_upload.flash_size` |
| Partition scheme | 16M Flash (3MB APP/9.9MB FATFS) | `app3M_fat9M_16MB.csv` |
| PSRAM | OPI | `memory_type = qio_opi`, `-DBOARD_HAS_PSRAM` |
| USB CDC On Boot | Enabled | `-DARDUINO_USB_CDC_ON_BOOT=1` |

- **Partition scheme matters.** Settings live in LittleFS on the partition named `ffat`
  (`config.cpp`), which exists only in this scheme. A different scheme loses saved settings
  (login, MQTT, WiFi, ...) on the next flash. Its two 3 MB app slots are what OTA uses. The built table matches the board's: app0/app1
  3 MB, `ffat` at 0x610000.
- **SD library:** the Adafruit ST7789 library declares `SD` as a dependency, so PlatformIO
  installs the generic Arduino SD 1.3.0 and prefers it over the core's. `platformio.ini`
  points `SD` at the core's own library (3.3.11), as the Arduino IDE uses.
- Library versions are pinned to `Documents\Arduino\libraries` at the time of the port.

## Build environment gotchas (Windows)

- Windows long paths must be enabled (`LongPathsEnabled = 1`), or the 3.3.11 core fails to unpack.
- Build from PowerShell. pioarduino's tool installer refuses Git Bash ("MSys/Mingw is not
  supported") and the build then fails with `xtensa-esp32s3-elf-g++ not recognized`.
- "Firmware metrics can not be shown": console codepage; `chcp 65001` shows them.
- **USB serial stalls loop()** (found 2026-09-27): with the USB cable in a PC that isn't
  reading the port (after a flash, or a closed serial monitor), each `Serial.print` on the
  native USB-Serial/JTAG retries 20 x 100 ms. A dozen prints dropped the MQTT connection.
  `Serial.setTxTimeoutMs(1)` in `setup()` fixes it (output is dropped when nobody reads).
  1, not 0: 0 was fine on this core (3.3.11), but on core 3.0.x it wraps to ~4 billion
  retries and hangs every print (Wifi_Fan_Knob, 2026-09-28); 1 works on both.
  The Arduino builds still have it. Also: a test that waits for `<node>/status = online`
  right after a flash sees the retained message of the previous session; wait for a small
  `uptime` instead.
- `HTTPClientError` installing a library (PubSubClient, 2026-09-27) while curl downloads the
  same URL fine: download the archive, check its sha256 against the registry API, `pio pkg
  install --no-save -l file://<archive>`, then set its `.piopm` spec to the registry owner/id
  (e.g. `{"owner": "knolleary", "id": 89, "name": "PubSubClient", ...}`), or `pio run` tries
  to download it again.
- **`pio pkg install -l <lib>` rewrites `platformio.ini`** (drops every comment, and gives
  the `extends` environment its own `lib_deps`, which replaces the inherited list): always add
  `--no-save`, or add the library to `platformio.ini` by hand and just build (2026-09-28).
- Wifi_Fan_Knob is pinned to pioarduino 51.03.04 (core 3.0.4) and shares the one core
  folder with this project, so the first build after switching projects re-downloads the
  core (~3 min). Both are pinned by URL, so each gets its own version back.
- The board profile header reads "8 MB, No PSRAM"; that is the generic devkit description.
  The `platformio.ini` overrides apply.

## Size

The original port's criterion (close to the Arduino build):

| | Arduino IDE | PlatformIO (`be88824`) |
|---|---|---|
| Program | 549,279 B | ~566,700 B (+3 %) |
| Global variables | 26,956 B | 27,028 B |

The +17 KB was unexplained (likely build option differences); accepted as close. Since then
(2026-09-27, `firmware.bin`): ESP32-S3-ETH 1,358,144 B, Touch-LCD-2 1,390,736 B of the
3,145,728 B app slot; the ESP-IDF network stack (+246 KB) and WiFi (+466 KB) are most of the
growth. Current limit: `CLAUDE.md`, success criteria.

## Secrets

- Two Home Assistant tokens were once committed to the Arduino repo (a `HA_Token-6.txt` and
  one hard-coded in an old `.ino`). Both, and all other old tokens, were revoked 2026-09-27.
- Since MQTT Phase 3 the firmware needs no HA token; its stored copy is wiped at boot. The
  MQTT, web and WiFi passwords are entered on the web page and never sent back to it. The
  simulator's login is in git-ignored `tools/mqtt_secrets.json`, anything else secret in
  git-ignored `secrets.h`. Never commit tokens or passwords. Flash backups (they contain
  settings) live outside the repo (`../ETH_Touch_PWM_backups`).

## Log

- 2026-09-27: port created, builds, size close to the Arduino build (`be88824`). Comparing it
  on the board with the Arduino build (the original criterion 2) was overtaken by the MQTT
  work; the Touch-LCD-2 checks are now in `docs/TODO.md`.
- 2026-09-27: fixed the sensor-blackout failsafe (**untested on the board**). With both probes
  down, `evaluateSensorFailsafes()` set full duty but `calculateFanCurve(0)` ran right after
  and set duty 0 (0 °C < tMin), so fans stopped instead of running flat out. The check now
  lives in `calculateFanCurve()`. First deliberate change from the Arduino source; the Arduino
  repo still has the bug.
- 2026-09-27: MQTT replacement for the HA REST link designed (`docs/MQTT.md`); HA side
  verified with the simulator `tools/mqtt_sim.py`.
- 2026-09-27: MQTT Phase 1 (later tested on the ESP32-S3-ETH): settings version 5 (v4 files upgraded,
  settings kept), default node ID `fanController_xx` (MQTT off until changed), web page MQTT
  section, read-only sensors. NTP back to `pool.ntp.org`. Build: 582,995 B flash, 27,596 B
  RAM (+16 KB / +0.6 KB over `be88824`, mostly PubSubClient and the MQTT code).
- 2026-09-27: The Claude web handoff (`archive/web_changes.md`) turned out to be already in
  `c23de85`; its open questions are in `docs/TODO.md`.

- 2026-09-27: second board, Waveshare ESP32-S3-ETH (fan_controller_01), build
  `waveshare_s3_eth` (`docs/BOARDS.md`); flashed after a full flash backup. MQTT Phase 2
  (thresholds, override) and Phase 3 (REST removed, network temperature and daily summary over
  MQTT, stored HA token wiped) tested on it. Touch-LCD-2 still untested with any of this.
  Build sizes: Touch-LCD-2 574,839 B, ESP32-S3-ETH 544,455 B.

- 2026-09-27: web page rebuilt: `web/index.html` (layout from esp32-nut, left tabs, themes as
  CSS variables) compiled in, JSON API under `/api`, web login (settings version 6, v4/v5
  files upgraded), OTA with a board-name check. The `client.print` page is gone. Tested on the
  ESP32-S3-ETH, including two OTA installs (607 KB in ~2.5 s, back in ~9 s). Screenshots:
  headless Edge (`msedge --headless=new --screenshot=... --window-size=W,H URL`; its minimum
  width is ~500 px, narrower shots are cropped, not reflowed).

- 2026-09-27: time zone picker (settings version 7: `tzName`, `tzPosix`). The clock stays in
  local time for the rest of the firmware; `getNtpTime()` converts UTC with the POSIX rule
  at each sync, so daylight saving follows within 5 minutes. `tzOffset` is only a fallback.

- 2026-09-27: Ethernet moved from the Arduino Ethernet library (own socket stack) to the
  core's ETH driver (lwIP): `NetworkServer`/`NetworkClient` for web and MQTT, SNTP instead of
  the UDP NTP code (and its first-packet retry). Unique MAC per board. Image +246 KB (network
  stack), still ~28 % of the app slot. Gotchas: our `network.h` shadowed the core's
  `Network.h` on case-insensitive Windows (renamed `fan_network.h`); `ETH.h` includes
  "Network.h" in quotes, so the library finder misses it (`-I${PROJECT_PACKAGES_DIR}/...`
  in build_flags; `${platformio.packages_dir}` there loses its backslashes); SNTP must start
  after the link is up (started earlier, its first lookup fails and it backs off).

- 2026-09-27: WiFi backup + setup hotspot + device name (settings version 8), WiFi tab like
  Wifi_Fan_Knob's. State machine in `networkLoop()` (fan_network.cpp). Image 1.32 MB (WiFi
  stack +466 KB), 42 % of the app slot. Failover tested with the cable pulled and back.
- 2026-09-27: Ethernet DHCP option (settings version 9; new boards start on DHCP, upgraded ones
  keep their static address) and the fan channel table; both tested by the user. HA cleanup
  (Phase 4) done by the user. Docs and success criteria updated (Phase 5).

- 2026-09-29: Third build `waveshare_s3_lcd2_lite` (W5500 Lite, user's pins; GPIO 19/20 are
  USB and must stay free). First-time setup: Setup tab while no login is set (one form, one
  restart, suggested name from the MAC) and an LCD setup screen with QR codes. Flash reads over
  these boards' USB fail with the esptool stub; `--no-stub` works, slowly. esptool's progress bar
  crashes when its output is captured on Windows: set `PYTHONIOENCODING=utf-8`.
- 2026-10-04: Network: WiFi joins at once without a W5500 (was 30 s) and the hotspot waits
  for a WiFi join. **lwIP has one global DNS server:** network switches can leave it wrong and
  SNTP then can't resolve pool.ntp.org; the firmware now sets the network's DNS again and
  restarts SNTP after every switch. **`MDNS.end()` panics** (LoadProhibited in mdns_free) once
  the setup hotspot has been on and off (it follows the deleted AP netif): mDNS is started
  once and renamed with `mdns_hostname_set()`. **TimeLib:** `setSyncInterval(300)` right after
  `setSyncProvider()` overrode the provider's own 5 s retry, so after a power cut the clock
  waited ~5 minutes. "SLOW: <part> took N ms" in main.cpp finds loop() stalls.
- 2026-10-04: LCD: the CST816D reads taps 30-60 px low near the top edge (centre is accurate);
  the gear's touch area is the whole top-right corner. Closing a full-screen page must wipe
  and redraw the whole dashboard (its own redraws leave gaps). LCD standby (HA switch).
- 2026-10-04: Fans: curve input averaged ~30 s, hysteresis, rate limit; per-fan RPM offset by
  an integral trim on averaged RPMs (single tach readings jump 60-120 RPM). Settings v10; an
  upgrade used to reset `ethDhcp` for every older file (now only before v9).
- 2026-10-05: The board froze overnight (no crash, no log rows until the reset button): a 30 s
  task watchdog on loop() now restarts it, and the RTC snapshot keeps the running part of
  loop() and the heap for the BOOT event. Tested with a deliberate hang. **pyserial resets the
  board** on open/close unless DTR and RTS are set low before `open()`.
- 2026-10-05: **The CST816D reports phantom taps** (0.2-0.6 s, mostly one row) in bursts
  16-25 s after the backlight goes off, ~19 in 2 minutes; very rarely with it on. They woke
  the standby screen; now a 1 s press wakes it. Anything touch-triggered that matters needs a
  hold (Restart on the QR page: 2 s).
- 2026-10-05: Override Cancel looked broken: after the override the smoothed curve eased down
  from 100 % at 0.5 %/s (~2.5 min). An override's end now jumps to the curve's speed.
- 2026-10-05: LCD flicker gone: each dashboard part caches what it drew and redraws only on a
  change; bars draw over their old fill instead of clearing to black. `clearScreen()` bumps a
  generation counter that makes everything draw once after a full-screen page.
- 2026-10-05: Web: Network and WiFi tabs merged, System tab reordered, Restart button; LCD
  Override has - / + (held = repeat) instead of the slider.
- 2026-10-06: Overnight: watchdog restart at 03:16 `stage=network` while HA rebooted after its
  backup; 05:57 the board went 73 min without MQTT (HA/Mosquitto logs clean: the board thought
  it had no network). NET/MQTT/CONFIG events and a self-heal restart added. HA history CSVs are
  in UTC (local is UTC-5 in summer).
- 2026-10-06: **Both networks are on one subnet**, so after Ethernet returns lwIP keeps a new
  connection on WiFi while WiFi is up; MQTT now moves once, when the WiFi backup goes off.
  Log the connection's local address, not the preferred network.
- 2026-10-06: Memory: no leak. The WiFi backup's driver takes ~110 KB, which explains the
  143-147 KB lows; a WiFi scan holds ~30 KB until collected (now dropped after 30 s). Target
  ~200 KB on Ethernet, ~120 KB on WiFi.
- 2026-10-06: Ethernet without an address: DHCP restarted after 20 s and every 3 min, then a
  backed-off restart without WiFi. The DHCP client state (`esp_netif_dhcpc_get_status`) and the
  W5500's PHYCFGR (`esp_eth_ioctl(ETH.handle(), ETH_CMD_READ_PHY_REG)`, register 0x2E << 16)
  go into the NET events. Time to address comes from the driver events: loop() can be busy
  when it polls (MQTT's first connect sends ~30 discovery messages, 3 s).
- 2026-10-06: LCD top-bar icons (1-bit, 16 x 16, `drawBitmap()`), no LVGL needed; the design
  note is `docs/LCD_DESIGN.md`.
- 2026-10-07: **The W5500 Lite resets itself / talks garbage.** 18:04 a 2 s link drop left
  17 h without an address: PHYCFGR read `0xBF`, OPSEL clear = the chip's power-on value, its
  whole setup lost. 16:45 a watchdog restart `stage=probe` was really the W5500 driver's
  receive task (priority 15) looping on SPI reads (`w5500_get_rx_received_size` reads until
  two reads agree) and starving loop(). Found with the crash dump the core keeps in flash
  (`coredump` partition, on by default in the Arduino core); `esp_core_dump_get_summary()`
  now puts it in the BOOT event. A lease not renewed for hours lets the router give the
  address away.
- 2026-10-07: **MQTT with Nagle off** (`net.setNoDelay(true)` after connect, `mqtt.cpp`).
  TCP's Nagle algorithm holds back a small message while an earlier one is still waiting for
  its acknowledgement, to bundle it with the next. The broker delays its acknowledgements on
  purpose (tens to hundreds of ms, hoping to piggyback them on a reply), so each side waits
  for the other. After every connect the board sends ~30 small discovery messages in a row,
  each waiting its turn: 8.9 s (21 s once), with loop() stuck in the send, so LCD, touch and
  web stalled (fans not: PWM is hardware). With Nagle off each message goes out at once; the
  cost, a few more small packets, is nothing on a LAN. Discovery 2.6 s on the first boot,
  0.1 s on the two since (COM15, 2026-10-07; timed in the `discovery sent in` MQTT event).

Open items: `docs/TODO.md`.
