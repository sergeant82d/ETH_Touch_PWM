# To do

How this list works (agreed 2026-09-28):
- **Your notes** (top): write anything for Claude here. At the start of a session Claude reads
  it, answers questions and summarises the open tasks; when the day's status report is written,
  the notes are turned into items below and this list is condensed.
- **Open**: oldest first. **Done**: newest first, with date and commit. Done items older than
  about two weeks move to `docs/TODO_DONE.md`.
- Tags: `[Board]` needs a board on the bench, `[LCD]` Touch-LCD-2 screen, `[Web]` web page,
  `[HA]` Home Assistant / MQTT, `[Net]` Ethernet / WiFi, `[SD]` SD card, `[Docs]`,
  `[Decide]` needs a decision, `[Question]` needs an answer.

## Your notes

(empty)

## Open

- [ ] `[Board]` **Pin remap** on the Touch-LCD-2 (PWM2 = GPIO 6, TACH1 = GPIO 4, TACH2 = GPIO 16)
      was never tested: check the wiring matches before its first flash (2026-09-27)
- [ ] `[Board]` Touch-LCD-2: first flash of the current firmware and the checks in CLAUDE.md's
      success criteria, incl. the settings upgrade from version 4, the LCD override path and
      NTP (works on the ESP32-S3-ETH) (2026-09-27)
- [ ] `[Board]` Sensor-blackout failsafe fix (`918439d`): both probes down = both fans full
      speed (2026-09-27)
- [ ] `[LCD]` Touch coordinate mapping: check small targets, not just the big Manual Control
      button (2026-09-27)
- [ ] `[LCD]` Screen flicker: every redraw clears everything; dirty-checking or an off-screen
      buffer. Needs the board (moved from "Decide" 2026-09-28) (2026-09-27)
- [ ] `[Net]` Setup hotspot and a static WiFi address: not yet tried on a board (2026-09-27)
- [ ] `[Web]` From the Claude web plans: Home tab notes, history viewer. The daily summary now
      reaches HA, so history tracking can start. Still without a spec: socket health.
      ("Shared network backup" dropped by the user, 2026-09-28.) History tab step 1 (Day chart,
      Downloads) and duty/override log columns done 2026-09-28; step 2: 30 days and Events views
      (2026-09-27)
- [ ] `[Web]` LCD view on the web page. Decided 2026-09-28: option (b), a card that redraws the
      LCD layout from the live data (works on both boards; the Touch-LCD-2's LCD can't be read
      back). Source: user's two Google Docs (TFT_eSPI / LovyanGFX mirroring; the LovyanGFX one
      suits the Wifi_Fan_Knob):
      https://docs.google.com/document/d/1eiV-0-nFfHzA8a_0D2_Bfcq3fkiPuLYAS52BnAqrvKo/edit?usp=drivesdk
      https://docs.google.com/document/d/1QRiNYOYCpe3n6oVMIhkXRHPVwEsFdX8NhLnUuT_J_Yo/edit?usp=drivesdk
      (2026-09-28)
- [ ] `[SD]` SD health on every display. Decided 2026-09-28: one SD state for everything:
      OK = green; getting full (>= 90 % used), write errors or the internal buffer over 80 % =
      orange, slow flash; card missing = red, fast flash. LCD dot and web sidebar dot; HA
      "SD card" sensor (state, free space) and "Fault SD card"; every change logged to the SD
      event log. Replaces the unused `isSpilloverNearFull()` (2026-09-28)
- [ ] `[LCD]` QR code page: tap the gear icon to show a QR code of the board's address (IP, as
      Android often can't open .local) and, while the hotspot is on, one to join it. Needs a
      small QR library; testable on the Touch-LCD-2 only (2026-09-28)

## Done

- [x] `[Web]` Notes box on the Dashboard: scrollable, emoji row, byte counter (4000 max),
      "last saved ... by ...", unsaved-changes warning; stored on the board (/notes.json),
      saving needs the login. User checked on the ESP32-S3-ETH (2026-09-28)
- [x] `[Web]` Themes: the Wifi_Fan_Knob gold/navy is its own preset "Navy & gold"; "Classic
      dark" is a new neutral dark (charcoal, blue accent). Five presets now. User checked on
      the ESP32-S3-ETH (2026-09-28)
- [x] `[Docs]` TODO.md in the tagged Open/Done layout; open tasks summarised at the start of each
      session; list condensed with each status report (user decisions, 2026-09-28)
- [x] `[Docs]` Project CLAUDE.md trimmed: what the global `~/.claude/CLAUDE.md` now covers was
      removed (2026-09-28)
- [x] `[Question]` What would BLE bring? Answered 2026-09-28: BLE can't serve the web page, so
      pairing wouldn't open it; it could only pass the address or set WiFi (Improv). Real gain
      only if the board read BLE thermometers itself. Cost ~300-500 KB flash, RAM, shared radio.
      Recommendation: skip BLE unless reading BLE sensors directly is wanted
- [x] `[SD]` SD daily rollup over a real day change works on the ESP32-S3-ETH (user, 2026-09-28)
- [x] `[Web]` Time zone picker (IANA zones + POSIX rules, daylight saving automatic; settings
      version 7): works (user, 2026-09-28; `f6c51b4`)
- [x] `[Web]` New web page (left tabs, themes, web login, OTA with board check): works well,
      theme selector on PC and mobile, temporary login replaced (user, 2026-09-28; `4be8091`)
- [x] `[HA]` Phase 3: network temperature and daily summary over MQTT, REST removed; first
      "Summary of the day" arrived in HA with clear data (user, 2026-09-28; `a28141b`)
- [x] `[Docs]` Status report 01 (2026-09-27, `998f7cd`)
- [x] `[HA]` Merge `mqtt` into `main` (2026-09-27, fast-forward to `f832628`; branch deleted)
- [x] `[Docs]` Phase 5: docs and success criteria for the MQTT-era firmware (2026-09-27, `f832628`)
- [x] `[HA]` Phase 4 (in HA): old helpers, rest_command and automations removed, fault
      notifications rebuilt on the MQTT problem sensors (user, 2026-09-27)
- [x] `[HA]` HA automations: network temperature for `fanController_01`, fault notifications
      (user, 2026-09-27)
- [x] `[Net]` Ethernet DHCP or static on the Network tab (settings version 9; new boards start on
      DHCP, upgraded ones keep their static address). User tested DHCP (2026-09-27, `5dd3214`)
- [x] `[Web]` Fan channels as a checkbox table: fan 1 fixed, next wired channel addable, last
      removable, channels 3-4 "not wired", factory default 1 fan. User tested: removing and
      re-adding fan 2 updated HA quickly (2026-09-27, `5dd3214`)
- [x] `[Net]` WiFi backup and back: cable pulled, joined WiFi by DHCP, MQTT and .local over
      WiFi, no restart; cable back, WiFi dropped ~60 s later (2026-09-27, `55d72a9`)
- [x] `[Net]` Ethernet on the core's ETH driver (lwIP), SNTP via pool.ntp.org; each board uses
      its chip's own MAC (was DE:AD:BE:EF:FE:ED on every board) (2026-09-27, `a43d531`)
- [x] `[Web]` Active network and logged-in user in the page frame; OTA drop zone
      (2026-09-27, `240e986`)
- [x] `[HA]` Phase 2: fan curve start/top and manual override from HA (2026-09-27, `47d448c`)
- [x] `[Board]` USB serial no longer stalls loop() (2026-09-27, `ff254bd`)
- [x] `[HA]` HA entities grouped on the device page by name (2026-09-27, `f73f58b`)
- [x] `[Board]` Second board: ESP32-S3-ETH build `waveshare_s3_eth` (2026-09-27, `18ea0e8`)
- [x] `[HA]` Phase 1: MQTT settings, node ID, read-only sensors (2026-09-27, `64a6771`)
- [x] `[HA]` MQTT design verified in HA with `tools/mqtt_sim.py` (2026-09-27, `c19398c`)
- [x] `[Board]` 1 fan setting was reset to 2 at boot; now 1-2 allowed (2026-09-27)
