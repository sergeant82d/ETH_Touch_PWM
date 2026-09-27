# Project history

How this project came to be, and what was learned setting it up (2026-09-27). Read this before
starting work; it saves re-discovering things.

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
  (including the HA token) on the next flash. The built table matches the board's: app0/app1
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
  `Serial.setTxTimeoutMs(0)` in `setup()` fixes it (output is dropped when nobody reads).
  The Arduino builds still have it. Also: a test that waits for `<node>/status = online`
  right after a flash sees the retained message of the previous session; wait for a small
  `uptime` instead.
- `HTTPClientError` installing a library (PubSubClient, 2026-09-27) while curl downloads the
  same URL fine: download the archive, check its sha256 against the registry API, `pio pkg
  install --no-save -l file://<archive>`, then set its `.piopm` spec to the registry owner/id
  (e.g. `{"owner": "knolleary", "id": 89, "name": "PubSubClient", ...}`), or `pio run` tries
  to download it again.
- The board profile header reads "8 MB, No PSRAM"; that is the generic devkit description.
  The `platformio.ini` overrides apply.

## Size baseline (success criterion 3)

| | Arduino IDE | PlatformIO (`be88824`) |
|---|---|---|
| Program | 549,279 B | ~566,700 B (+3 %) |
| Global variables | 26,956 B | 27,028 B |

The +17 KB is unexplained (likely build option differences); accepted as close.

## Secrets

- Two Home Assistant tokens were once committed to the Arduino repo (a `HA_Token-6.txt` and
  one hard-coded in an old `.ino`). Both, and all other old tokens, were revoked 2026-09-27.
- The HA token is entered on the web page and stored on the board. Anything else secret goes
  in git-ignored `secrets.h`. Never commit tokens.

## Status

- Done: project created, builds (criterion 1), size close (criterion 3). Commit `be88824`.
- Open: criterion 2, flash the board and check display, touch, Ethernet/web page, fan PWM and
  RPM, temperature, HA sync, SD logging against the Arduino build. Needs the board's COM port
  (and IP, to check the web page). Flashing keeps saved settings (same partition table);
  reflash from Arduino IDE to go back.
- 2026-09-27: fixed the sensor-blackout failsafe (**untested on the board**). With both probes
  down, `evaluateSensorFailsafes()` set full duty but `calculateFanCurve(0)` ran right after
  and set duty 0 (0 °C < tMin), so fans stopped instead of running flat out. The check now
  lives in `calculateFanCurve()`. First deliberate change from the Arduino source; the Arduino
  repo still has the bug.
- 2026-09-27: MQTT replacement for the HA REST link designed (`docs/MQTT.md`); HA side
  verified with the simulator `tools/mqtt_sim.py`.
- 2026-09-27: MQTT Phase 1 (**untested on the board**): settings version 5 (v4 files upgraded,
  settings kept), default node ID `fanController_xx` (MQTT off until changed), web page MQTT
  section, read-only sensors. NTP back to `pool.ntp.org`. Build: 582,995 B flash, 27,596 B
  RAM (+16 KB / +0.6 KB over `be88824`, mostly PubSubClient and the MQTT code).
- 2026-09-27: The Claude web handoff (`archive/web_changes.md`) turned out to be already in
  `c23de85`; its open questions are in `docs/TODO.md`.

Open items: `docs/TODO.md`.
