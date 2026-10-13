# To do

How this list works (the all-projects format, agreed 2026-10-07; started here 2026-09-28):
- **Your notes** (top): write anything for Claude here. At the start of a session Claude reads
  it, answers questions and summarises the open tasks; when the day's status report is written,
  the notes are turned into items below and this section is emptied.
- **Open**: oldest first. **Done**: newest first, with date and commit, who checked it (user /
  Claude / board) and what is UNTESTED. Done items older than about two weeks move to
  `docs/TODO_DONE.md`.
- Tags (alphabetical; the same set in every project, copy one into an item):
  - `[Board]` needs a board on the bench
  - `[Decide]` needs a decision
  - `[Docs]` documentation
  - `[Eye]` screensaver / standby eye (Wifi_Fan_Knob)
  - `[HA]` Home Assistant / MQTT
  - `[LCD]` Touch-LCD-2 screen
  - `[Net]` Ethernet / WiFi
  - `[Question]` needs an answer
  - `[SD]` SD card
  - `[Sensor]` air, light and presence sensors (Wifi_Fan_Knob)
  - `[Web]` web page

## Your notes



## Open

- [ ] `[Board]` Low priority, parked (user, 2026-10-06): one I2C read error (touch bus) at
      ~4 s after boot; touch works afterwards. Touch read failures now go to the event log
      (TOUCH, the first and then at most one an hour); Claude checks it at the start of each
      session and picks this up only if it shows again (2026-09-29)
- [ ] `[Board]` `[HA]` 2026-10-06 03:15: the watchdog caught a hang in the network step
      (`stage=network`) at the moment HA rebooted after its 03:03 backup (HA history: entities
      unavailable 03:15:50, back 03:16:17). The finer stage names (`net:...`, `mqtt:...`) will
      say which call next time
- [ ] `[LCD]` Watch: the dark LCD woke itself 4 times since the 1 s hold (10-05 22:13, 10-06
      23:54, 10-07 18:45 and 10-09 23:58, the last two exactly 56 s after HA turned it off).
      Since 2026-10-10 each wake logs `DISPLAY wake press: ...` (length, readings, position,
      longest gap, time since off); Claude checks it at session start. If one shows without
      you: the 2-minute no-wake after turning off is the next step (your real 1 s hold stayed
      on one exact point, so position can't tell them apart)
      2026-10-12: two more after the 150 ms rule, so they are one continuous 1 s press, not
      joined taps: 10-10 22:35:27 (`1.03 s, 35 readings, x 236-262 y 124-124, longest gap
      40 ms, 53 s after off`) and 10-12 03:29:49 (`1.00 s, 34 readings, x 277-283 y 126-126,
      8697 s after off`). Both on one row (y 124-126) at the right side; yours was x 168
      y 136. A 2-minute no-wake would have missed the 03:29 one. Decided (user, 2026-10-12):
      a touch wake goes dark again after 5 min without a touch (Done below). Keep watching
      the wake lines: a phantom now shows as a `wake press` followed 5 min later by
      `source=LCD-idle action=OFF`
- [ ] `[SD]` Low priority: on the card's return 2026-10-10 14:03 the event log has
      `state=OK` before the buffered `state=Missing` from 14:01 (the buffer was written after
      the OK line; the 13:58 return had them in order). Only the order; nothing lost

## Done

- [x] Firmware audit `docs/AUDIT_2026-10-06.md` finished: every item done, or closed by your
      decision (3.4 left as is, 3.5 skipped, 1.1 covered by the global display rule); the
      items are in `docs/TODO_DONE.md` (Claude, 2026-10-13, this commit)
- [x] `[LCD]` `[HA]` Override started by a phantom press (your report, 2026-10-13): HA turned the
      LCD on at 08:00:02, a phantom press hit Manual Control at 08:00:47 and the fans ran at
      100 % for 45 min until you stopped it from the web page. Now an override started on the
      LCD stops by itself after 60 s unless Keep On, - or + is pressed; every LCD override
      event logs where the press landed (`at x= y=`). Found while testing: one tap on the
      flashing button stopped the override and started it again in the same second (one
      empty or failed touch reading mid-tap made a second press); a finger now counts as
      lifted only after 2 empty readings (60 ms). COM15 (user): unconfirmed one stopped at
      exactly 60 s, Keep On kept it, two tap-to-stops clean. UNTESTED: the plain Touch-LCD-2
      (compile) (2026-10-13, `6c7e48e`)
- [x] `[Net]` Clock after a flash: the log ran up to 12.5 min off for 5 min after a USB flash
      (TimeLib took the time kept through the reset and re-read only every 5 min; the error
      grows with uptime, ~1 % of it). Now TimeLib re-reads as soon as SNTP syncs (at boot and
      every hour). COM15: the new path runs (serial, restart from HA) (Claude). UNTESTED: a
      flash after a long uptime, the case that showed it; the other two builds (compile)
      (2026-10-13, `6c7e48e`)
- [x] `[SD]` SD card at 16 MHz: trial closed, 3 days without an SD error, three midnight
      rollovers (Claude, event log 2026-10-13) (2026-10-13, `6c7e48e`)
