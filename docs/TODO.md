# To do

Open items. Tick them (`- [x]`) with the date and commit when done. Sources: the Claude web
handoff (`archive/web_changes.md`, section 20) and the MQTT work (`docs/MQTT.md`).

## MQTT (replaces the HA REST link)

- [x] HA side designed and verified with `tools/mqtt_sim.py` (2026-09-27, `c19398c`)
- [x] Phase 1: MQTT settings, node ID, read-only sensors (2026-09-27, `64a6771`; tested on the
      ESP32-S3-ETH). Touch-LCD-2: settings upgrade from version 4 untested
- [x] Phase 2: thresholds and manual override from HA (2026-09-27, `47d448c`; tested on the
      ESP32-S3-ETH). LCD override path untested (Touch-LCD-2)
- [ ] Phase 3: network temperature and daily summary over MQTT; REST code, token and fields
      removed. Code done and tested on the ESP32-S3-ETH 2026-09-27; daily summary waits for a
      real day change
- [ ] In HA: network temperature automation for `fanController_01` (docs/MQTT.md), optional
      fault notification automation
- [ ] Phase 4 (in HA): remove the old helpers, rest_command and automations (list in
      `docs/MQTT.md`), rebuild fault notifications on the MQTT problem sensors
- [ ] Phase 5: update docs and success criteria (no longer "same as the Arduino build")

## Needs the board

- [ ] Success criterion 2: flash and compare with the Arduino build
- [ ] **Pin remap** (PWM2 = GPIO 6, TACH1 = GPIO 4, TACH2 = GPIO 16) was never tested. Check the
      wiring matches before the first flash
- [ ] Sensor-blackout failsafe fix (`918439d`): both probes down = both fans full speed
- [ ] Touch coordinate mapping: check small targets, not just the big Manual Control button
- [ ] SD daily rollup over a real day change
- [ ] NTP via `pool.ntp.org` (reverted from the Cloudflare IP, 2026-09-27)

## Decide

- [ ] Every board uses the same MAC (`DE:AD:BE:EF:FE:ED`, `network.cpp`); two controllers on
      one LAN would clash. Option: derive it from the ESP32's own MAC
- [ ] `isSpilloverNearFull()` is unused: wire it to the LCD/web SD indicator or an MQTT
      diagnostic, or remove it
- [ ] Screen flicker: every redraw clears everything. Dirty-checking or an off-screen buffer

## Planned, no spec yet

From the Claude web sessions; get the spec before planning any of these: web UI restyle (tabs,
4 themes, Home tab notes), Wi-Fi/BLE/DHCP default, socket health, OTA, shared network backup,
history viewer, fan-channel checkboxes on the web page.
