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

## Done

- [x] `[LCD]` Watch: phantom touches (open since 2026-10-05): the CST816D reports phantom
      presses - a sweeping band at y 105-133 (up to 2926 readings blocked an hour), wakes
      and lit-screen button presses ~26-56 s after the backlight switches (last: 10-19
      00:23 wake y 89-95, 10-21 08:00 QR page y 48). Contained step by step (1 s hold,
      150 ms gap rule, 5 min idle, band ignore, Keep-On-only confirm, panel locks - see
      the 2026-10-15/17 Done entries and PROJECT_HISTORY). Resolved on this board by your
      decision 2026-10-21: touch compiled out on the waveshare_s3_lcd2_lite build
      (`-DTOUCH_ENABLED=0`); the LCD stays and follows HA/web, the LCD override panel and
      QR page are gone (touch-only). Checked by you on COM15 (OTA install, taps in many
      places ignored, screen still follows HA/web) and Claude (BOOT/NET/MQTT clean, heap
      253 KB). UNTESTED: the plain waveshare_s3_lcd2 and waveshare_s3_eth builds (compile
      only, behavior unchanged). Replacing the Touch-LCD-2 board stays the hardware fix
      if touch is ever wanted again (this commit)
- [x] `[LCD]` `[HA]` Phantom presses on the override panel (your report, 2026-10-15): only Keep
      On confirms an LCD override; every press on the panel logged with position and the
      touch chip's report; readings in the phantom band y 105-133 ignored (hourly count);
      Manual Control locked 1 s after any override action; QR page openings logged; Cancel /
      Keep On touch areas down to the bottom of the screen. COM15 (you, 2026-10-15): + no
      longer confirms (stopped at 60 s), Keep On does, the band ignored presses there, three
      Cancels without a restart; 48 h running (Claude). UNTESTED: the taller Cancel / Keep On
      areas (added 2026-10-17); the plain Touch-LCD-2 (compile) (2026-10-17, this commit)
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
