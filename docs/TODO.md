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

- [ ] `[LCD]` Watch: phantom touches. The dark LCD woke itself 4 times since the 1 s hold (10-05 22:13, 10-06
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
      a touch wake goes dark again after 5 min without a touch (`279710f`). Keep watching
      the wake lines: a phantom now shows as a `wake press` followed 5 min later by
      `source=LCD-idle action=OFF`
      2026-10-13: phantoms also press buttons on the lit screen. 08:00:47, 45 s after HA's
      morning routine turned the LCD on, one hit Manual Control and the fans ran at 100 % for
      45 min (you stopped it from the web page). Overnight two more wakes: 04:40:08 (`x 35-130
      y 83-92, 21842 s after off`; stayed lit 12.5 min, more phantoms kept resetting the
      5 min) and 04:53:09 (`x 202-263 y 130-130, 33 s after off`). Many come ~45-56 s after
      the backlight switches on or off. Since `6c7e48e`: an LCD override stops after 60 s
      unless confirmed, every LCD override event logs `at x= y=`, and a finger counts as
      lifted only after 60 ms. Your real presses on Manual Control landed at x 133-166
      y 192-217. Claude checks at session start for wake and override lines you didn't make.
      Not yet tried: what the touch chip itself reports with each touch (event type and
      gesture code, which the driver ignores), to tell a phantom from a finger

## Done

- [x] `[Web]` `[HA]` Firmware version shown (your request, 2026-10-14: the System tab said
      "Oct 10" for days, `__DATE__` only changed when web_server.cpp was recompiled).
      `tools/build_info.py` writes the build time and git commit on every build, "+ changes"
      when the code differs from that commit; shown on the System tab (Build), in every BOOT
      event (`firmware ...`) and on HA's device page (Firmware). Checked by you on COM15
      (web, HA); BOOT line read by Claude. Also the first real test of the clock fix: after
      28.7 h up, only the BOOT line (written before the network time) was ~3 min off, the
      next events right. UNTESTED: the other two builds (compile) (2026-10-14, this commit)
- [x] `[Docs]` Status report 07 (covers 2026-10-12 and 10-13); no notes to turn into items
      (2026-10-13, this commit)
- [x] `[Web]` History tab, Downloads (your question, 2026-10-13): the month logs grow by one a
      month, so the card lists the current and previous month, and every month sits in one
      collapsed row "All monthly logs (/logs, N files)", by year, newest first (hover a month
      for its size). Month files sorted newest first (the board sends them in card order);
      `daily.csv` described as "every day" (was "30 days"); sizes over 1 MB in MB. Page only,
      no firmware web-code change. Checked by you on COM15 (2026-10-13, `f1ae7d8`)
- [x] `[Web]` Cards keep a fixed width (900 px) on large monitors, as on Wifi_Fan_Knob (your
      request, 2026-10-13): `.main > * { max-width: 900px; }`; rule added to `WEB_STYLE.md`
      ("Content width"). Checked by you on COM15; History charts and the Fan Control table fit,
      phones unchanged. UNTESTED: the other two builds (compile) (2026-10-13, `194abd7`)
