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
- [x] Phase 4 (in HA): remove the old helpers, rest_command and automations (list in
      `docs/MQTT.md`), rebuild fault notifications on the MQTT problem sensors (done by the
      user 2026-09-27)
- [ ] Phase 5: update docs and success criteria (no longer "same as the Arduino build")

## Web page

- [ ] New web page (`web/index.html` + `/api`, left tabs, themes NUT / classic dark / classic
      light / custom, web login, OTA with board check). Code done 2026-09-27, tested on the
      ESP32-S3-ETH (API, login, validation, override, theme, OTA install and refusals,
      screenshots). User check in a browser pending; replace the temporary login.
- [ ] Classic dark theme uses the Wifi_Fan_Knob gold/navy colours: confirm or change
- [ ] Later from the Claude web plans: Home tab notes, history viewer
- [ ] Time zone picker (Network tab, IANA zones + POSIX rules from Wifi_Fan_Knob, daylight
      saving automatic; settings version 7). Done 2026-09-27; clock checked against the PC.
      Changing the zone on the page not yet tried (needs the user's login)
- [x] Ethernet on the core's ETH driver (lwIP), SNTP (2026-09-27; tested on the ESP32-S3-ETH)
- [ ] WiFi as a backup when Ethernet is down, plus the setup hotspot; WiFi tab like
      Wifi_Fan_Knob's (user decisions 2026-09-27). Code done 2026-09-27; tested: settings v7->v8,
      scan, device name `fancontroller-01.local` over Ethernet. Failover tested 2026-09-27 (cable
      pulled): joined Lost-Link2 by DHCP (.199, -50 dBm), MQTT and .local over WiFi, no
      restart; cable back: Ethernet in use at once, WiFi dropped ~60 s later. NOT yet tested:
      hotspot, static WiFi address
- [x] Network: Add block to set Static IP Address (it existed; now with the DHCP choice below)
- [x] Web page - Fan Control - turn the fan-channels into a table, selectable check boxes, not radio buttons. Defaults to only one, the first one listed in the firmware, and user can select additional fans at run time on the web page, but only in order; i.e., #2 is available at first start up, but #3 is not available until #2 has been selected. Grey-out unavailable fans.
      (2026-09-27: table with pins and live RPM; fan 1 fixed, next wired channel addable, last
      removable; channels 3-4 "not wired" on both boards; factory default 1 fan. User tested
      2026-09-27: removing and re-adding fan 2 updated HA quickly)
- [x] On the Network page, there needs to be the option for DHCP or Static IP, like there is on the WiFi page
      (2026-09-27, settings version 9: new/reset boards start on DHCP, boards upgraded from an
      older version keep their static address. User tested DHCP on the ESP32-S3-ETH 2026-09-27: works)

## Needs the board

- [ ] Success criterion 2: flash and compare with the Arduino build
- [ ] **Pin remap** (PWM2 = GPIO 6, TACH1 = GPIO 4, TACH2 = GPIO 16) was never tested. Check the
      wiring matches before the first flash
- [ ] Sensor-blackout failsafe fix (`918439d`): both probes down = both fans full speed
- [ ] Touch coordinate mapping: check small targets, not just the big Manual Control button
- [ ] SD daily rollup over a real day change
- [ ] NTP via `pool.ntp.org` (reverted from the Cloudflare IP, 2026-09-27)

## Decide

- [x] Every board used the same MAC (`DE:AD:BE:EF:FE:ED`): fixed by the ETH driver switch,
      each board has its chip's own MAC (2026-09-27)
- [ ] `isSpilloverNearFull()` is unused: wire it to the LCD/web SD indicator or an MQTT
      diagnostic, or remove it
- [ ] Screen flicker: every redraw clears everything. Dirty-checking or an off-screen buffer
- [x] 1 fan setting reset to 2 at boot (`loadSettings` allowed 2-4): now 1-2 (2026-09-27)

## Use ESP32's MAC
	- BDH
- [x] Done with the ETH driver switch (2026-09-27, `a43d531`): Ethernet and WiFi use the chip's
      own MACs (ESP32-S3-ETH: Ethernet 2E:84:85:53:86:65, WiFi 28:84:85:53:86:64; both on the
      WiFi tab)


## Planned, no spec yet

From the Claude web sessions; get the spec before planning any of these: web UI restyle (tabs,
4 themes, Home tab notes), Wi-Fi/BLE/DHCP default, socket health, OTA, shared network backup,
history viewer, fan-channel checkboxes on the web page.
