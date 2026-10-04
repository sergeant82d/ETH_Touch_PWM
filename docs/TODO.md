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
 
- [ ] `[Board]` Touch-LCD-2: first flash of the current firmware and the checks in CLAUDE.md's
      success criteria, incl. the settings upgrade from version 4, the LCD override path and
      NTP (works on the ESP32-S3-ETH) (2026-09-27)
- [ ] `[Board]` Sensor-blackout failsafe fix (`918439d`): both probes down = both fans full
      speed (2026-09-27)
- [ ] `[LCD]` Screen flicker: every redraw clears everything; dirty-checking or an off-screen
      buffer. Needs the board (moved from "Decide" 2026-09-28) (2026-09-27)

- [ ] `[Web]` Per-fan speed offset: fan 1 is the reference, each other fan +/- in 10 RPM steps,
      in the Fan Channels table on the Fan Control tab; an LCD page for it later (your note,
      2026-09-28)
- [ ] `[HA]` HA configuration.yaml and Lovelace card YAML to break out the daily summary CSV for
      plotting; discuss better alternatives (your note, 2026-09-28)
- [ ] `[LCD]` `[Decide]` Discuss moving the LCD to LVGL (nicer gauges and fonts): high effort, low
      impact, but pretty. Pending, near term; you're collecting images (`docs/images/`) (your
      note, 2026-09-28)
- [ ] `[Question]` Can a PID controller hold a steadier temperature instead of the fans speeding
      up and slowing down? (your note, 2026-09-28)
- [ ] `[Question]` What is the impact of moving to native ESP-IDF, still in VS Code and
      PlatformIO? (your note, 2026-09-28)

- [ ] `[Board]` W5500 Lite build: fans (PWM 6 / tach 16, PWM 2 / tach 4) UNTESTED, nothing
      connected yet; DS18B20 on GPIO 17 reads (new board F924, 2026-09-29) (2026-09-29)
- [ ] `[Board]` Boot log on the Touch-LCD-2 shows "GPIO isr service already installed" and one
      I2C read error (touch bus) at ~4 s; touch works. Look into it. Seen on both Lite boards, so it's the firmware (2026-09-29)

- [ ] `[Net]` After an Ethernet link drop and return (cable bumped), the board got no new DHCP
      address and fell back to the hotspot. Happened twice on the F924 board (COM9) the same
      evening; the second time with no known bump, MQTT "Host is unreachable" until reset.
      User's working theory: not enough power on the bench set-up (W5500 Lite ~130 mA on the
      3V3 rail). Revisit if it continues once the hardware is wired for good; then: log the
      Ethernet/DHCP state, and restart DHCP (then the W5500) if the link is up with no
      address. Test: unplug a few seconds, plug back (2026-09-29)
- [ ] `[LCD]` Setup screen: "no network" countdown and hotspot views UNTESTED; the hotspot
      turns off 30 s after a network works and drops a phone mid-setup (2026-09-29)

- [ ] `[Web]` Low priority: Manual Override slider sometimes doesn't change the speed (your
      note, 2026-10-02). Has worked and not worked; can't be confirmed while the bench
      hardware has network/connection problems. Re-check once wired for good (2026-10-04)

- [ ] `[Net]` loop() stalled ~3 min with the Ethernet cable out (first unplug, 2026-10-04):
      no WiFi join, no hotspot until the cable was back. Not reproduced in three later
      unplugs; main.cpp now prints "SLOW: <part> took N ms" for any part over 500 ms, so the
      log names it if it happens again (2026-10-04)
- [ ] `[Board]` Lowest free heap 153 KB while WiFi and Ethernet were both up (floor ~150 KB,
      CLAUDE.md success criteria) (2026-10-04)
- [ ] `[SD]` (lower priority, user 2026-10-04) A failing card (knock-off) makes each SD retry block loop() up to ~0.5 s; the web
      page stalls ("Failed to fetch") during a run of them (2026-09-29)
- [ ] `[SD]` Some cards are recognised until the buffered (internal flash) data has been
      written to them, then the card is lost and the dot flashes red again; only certain
      cards (your note). Likely the same as the failing-card item above (2026-10-04)

## Done

- [x] `[Board]` Loop pauses: MQTT connect limited to 1 s and discovery resent only when the node
      or fan count changed or HA sends its birth message (was ~30 messages per reconnect,
      2-2.8 s); web connections are parked until the request arrives (browser spare
      connections held loop() 1 s); the DS18B20 conversion is read on the next sample
      instead of waited for (0.6 s). COM15: no SLOW lines in 4 min with page/status traffic;
      probe, MQTT fine. HA-restart re-announce UNTESTED (2026-10-04, this commit)
- [x] `[Web]` Factory reset on the System tab (login + type RESET): deletes /settings.cfg and
      restarts into the Setup page; theme, notes, SD kept. Refused without login (tested);
      the reset itself UNTESTED (2026-10-04, this commit)
- [x] `[Web]` Node ID in Title Case in the sidebar, same rule as the LCD title bar ("6U Rack
      Fans") (your note, 2026-10-04, this commit)
- [x] `[LCD]` Override panel artifacts (user): closing it (Stay On / Cancel) now wipes and
      redraws the whole dashboard, like the QR page; the slider label's wipe no longer cuts
      the panel border. Checked by the user on COM15 (2026-10-04, this commit)
- [x] `[LCD]` Touch: every tap logged (raw and screen coordinates) on COM15. The coordinate
      mapping is right (Manual Control, slider, Stay On, tap-to-cancel all hit first time, no
      I2C read failures). The "sporadic" part was the gear: near the top edge taps read
      30-60 px low (aimed at y ~13, arrived at y 31-79, x 233-316), outside its 60x36 area.
      The gear area is now the whole top-right corner (x 225+, y 0-90); user: "feels good"
      (2026-10-04, this commit)
- [x] `[Net]` NTP after network switches (your note): the DNS server of the network in use is
      set again and SNTP restarted after every switch and hotspot on/off; log line per switch
      and in the minute DIAGNOSTIC (net, DNS, time sync). Tested on COM15: time synced within
      2-4 s through boot, Ethernet->WiFi, WiFi->Ethernet and Ethernet->hotspot->Ethernet
      (user + Claude, 2026-10-04, this commit)
- [x] `[Net]` Crash (LoadProhibited in mDNS) when saving WiFi after the setup hotspot had been
      on and off: MDNS.end() followed the deleted hotspot interface. mDNS is now started once
      and renamed in place. Same sequence repeated without a crash (COM15, 2026-10-04, this
      commit)
- [x] `[LCD]` `[HA]` `[Web]` LCD standby (your note): backlight off/on from HA (switch
      "LCD display"), the web page (button under the LCD view; the view dims) or a tap on the
      dark screen (wakes it, nothing else). On after every boot. HA path tested via MQTT on
      the COM14 board (Claude); web button and tap UNTESTED (2026-10-04, this commit)
- [x] `[LCD]` `[Web]` Layout batch (your notes): status dots network, MQTT, SD (LCD and web
      LCD view); temperature bars network left, local right; clock top level with the column
      labels and the big temperature centred between clock and button; alert colour red from
      75 % of fan curve start -> top (was top - 5 C). Network dot/MQTT now need an address,
      not just a cable link (one cause of the LCD/web light mismatch). LCD layout confirmed
      and approved by the user (2026-10-04, `b730576`)
- [x] `[Net]` WiFi without Ethernet (your note: restart started the hotspot instead of the
      saved WiFi): no W5500 = join WiFi at once (was 30 s); W5500 but no address at boot =
      wait 10 s; the hotspot waits while a WiFi join is under way (up to 45 s). Boot log on
      COM14: joined ~1 s after boot, no hotspot (Claude, 2026-10-04, this commit)
- [x] `[Web]` Normal tabs' rough spots: closed by the user, the Setup page covers first-time
      setup (`1567e95`). Unchanged on the normal tabs (reopen if it bothers): each Save
      reloads every form and wipes other panels' unsaved fields; a node ID change reaches
      the `.local` name only at the next restart or WiFi-tab save (2026-09-29)
- [x] `[Net]` Setup hotspot and a static WiFi address: tested, working (user, 2026-09-29)
- [x] `[Web]` "Socket health" from the Claude web plans: it meant SD card health, done with
      the SD health state (`6bb7e19`) (user, 2026-09-29)
- [x] `[LCD]` QR code page (gear icon): the QR codes scan on the W5500 Lite boards (user,
      2026-09-29, `4026820`). Opening it by touch is unreliable: in the touch item
- [x] `[Board]` Pin remap for the original Touch-LCD-2 build (PWM2 6, TACH1 4, TACH2 16):
      closed by the user, replaced by the W5500 Lite build `59d3b9b` (2026-09-29)
- [x] `[Web]` LCD view: the °F/°C label on the Manual Control button sat above the button's
      border (your note). Fixed itself; archived, cause unknown (2026-09-29)
- [x] `[Web]` First-time setup page: Setup tab while no login is set (name and login,
      network and time, fans, Home Assistant optional), one Save, one restart, then it says
      where the board is; unique suggested name from the MAC; login button hidden until a
      login exists. LCD setup screen with QR codes; hotspot shown on the LCD (orange dot,
      "Hotspot on"). Used by the user on both Lite boards (2026-09-29, this commit)
- [x] `[Docs]` `docs/SETUP.md`: setting up a new board, step by step as the firmware needs it
      today, with the rough spots (your note, 2026-09-29, this commit)
- [x] `[Board]` Third build `waveshare_s3_lcd2_lite`: Touch-LCD-2 + W5500 Lite on your pins.
      Ethernet, MQTT, SD, web page and touch work on the new board (COM8) (user, 2026-09-29,
      `59d3b9b`)
- [x] `[Docs]` Status report 02; your notes moved into Open (2026-09-28, this commit)
- [x] `[Docs]` Wifi_Fan_Knob handoff: which of today's features fit it, written to that repo's
      `docs/HANDOFF_FROM_ETH_Touch_PWM.md` (2026-09-28)
- [x] `[LCD]` MQTT status dot on the LCD title bar and the web LCD view. Web view checked on
      the ESP32-S3-ETH; the real LCD UNTESTED (2026-09-28, `d5b909f`)
- [x] `[Web]` LCD view card on the Dashboard: the Touch-LCD-2 screen redrawn from live data
      (option b). Works on the ESP32-S3-ETH (user, 2026-09-28, `6d41e3c`); fonts approximate.
- [x] `[SD]` SD health on every display: OK green; >= 90 % used orange, slow flash; missing red,
      fast flash. Web dot + text and System row, HA "SD card" / "SD card used" / "Fault SD card",
      event log; replaces the unused `isSpilloverNearFull()` as a state of its own. Found and
      fixed on the way: a card pulled while running was never noticed (now a sector read every
      10 s, remount every 15 s), and events logged while the card was out were written back
      into the month log instead of events.csv. User tested pull/reinsert on the ESP32-S3-ETH
      (2026-09-28). Not yet: the LCD dot (Touch-LCD-2), a >= 90 % full card
- [x] `[Web]` History tab: Day chart, Last 30 days (click a day to open it), all-time records,
      Events with filters, Downloads; per-minute log gained duty and override; CSV column names.
      User: "looks amazing" (2026-09-28; `7bf5979` + this commit). 30-day view on real data
      waits for the first rollup on the new card
- [x] `[Web]` Notes box on the Dashboard: scrollable, emoji row, byte counter (4000 max),
      "last saved ... by ...", unsaved-changes warning; stored on the board (/notes.json),
      saving needs the login. User checked on the ESP32-S3-ETH (2026-09-28)
- [x] `[Web]` Themes: the Wifi_Fan_Knob gold/navy is its own preset "Navy & gold"; "Classic
      dark" is a new neutral dark (charcoal, blue accent). Five presets now. User checked on
      the ESP32-S3-ETH (2026-09-28)
- [x] `[Docs]` TODO.md in the tagged Open/Done layout (your decisions); project CLAUDE.md
      trimmed to what the global one doesn't cover (2026-09-28, `e543ab5`)
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
