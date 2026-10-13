# ETH_Touch_PWM - Project Status Report 07
**Date**: October 13, 2026 (also covers October 12, which had no report of its own)
**Session**: phantom touches on the LCD, the clock after a flash, the TODO list cleaned up, the
web page at a fixed width, the Downloads list, Graphify
**Status**: Final for the day. Everything is on `main` (last code commit `f1ae7d8`). Bench board:
Touch-LCD-2 + W5500 Lite (`6U_Rack_Fans`, COM15, Ethernet at .229, WiFi backup at .190).
Settings version 11 (unchanged).

For how things work, see `../../CLAUDE.md`, `../PROJECT_HISTORY.md` (updated both days),
`D:\GitHub\WEB_STYLE.md` (new "Content width" rule) and `D:\GitHub\GRAPHIFY_KNOWLEDGE.md`. Open
items: `../TODO.md` (one); everything finished is in `../TODO_DONE.md` (new).

---

## What changed

| Area | Result | Commit |
|---|---|---|
| **LCD return to dark** | A screen woken by a press on the dark LCD goes dark again after 5 min without a touch; turned on from HA or the web page it stays on | `279710f` |
| **LCD override** | An override started on the LCD stops by itself after 60 s unless Keep On, - or + is pressed; every LCD override event logs where the press landed | `6c7e48e` |
| **Touch** | A finger counts as lifted only after 60 ms without a reading (one tap that stopped the override started it again) | `6c7e48e` |
| **Clock** | The time used for the logs is re-read as soon as the network time arrives (at boot and every hour); after a flash it had been up to 12.5 min off for 5 min | `6c7e48e` |
| **Web: width** | Cards keep a fixed 900 px width on large monitors, as on Wifi_Fan_Knob; rule added to `WEB_STYLE.md` | `194abd7` |
| **Web: Downloads** | The current and previous month as rows, every month in one collapsed "All monthly logs" row by year; files sorted newest first; `daily.csv` described as "every day" | `f1ae7d8` |
| **TODO** | Older Done items archived to the new `docs/TODO_DONE.md`; the W5500, overnight-freeze, touch I2C, 03:15 hang and SD event-order items closed with notes; the firmware audit marked finished | `d51ecb0`, `ed35c8b`, `c6a99e6`, `c8cbe9f` |
| **Git** | `graphify-out/` ignored (your line) | `dbecdb6` |

Build: ~1.49 MB, 47.5 % of the 3 MB app slot; RAM 17 %; all three builds compile with no new
warnings. Free memory on Ethernet ~254 KB.

## Checked on the board (COM15)

- **You:**
  - a 1 s hold woke the dark screen and it went dark again 301 s later
  - an LCD override left unconfirmed stopped at exactly 60 s; Keep On kept it; two
    tap-to-stops were clean (one stop each, no restart)
  - the page at 900 px on your large monitors
  - the Downloads card, the "All monthly logs" list and a month download
- **Logs and tests by Claude:**
  - the event log at both session starts: no watchdog restart, no W5500 reset, no SD error,
    midnight rows every night
  - turned on from HA, the screen stayed on 11 min
  - the new clock code runs after a restart (serial: "SNTP: time synchronized" from the main
    loop)
  - the page script (`node --check`) and screenshots at 2400 px and phone width
- **UNTESTED:** the clock fix in the case that showed the error (a flash after a long uptime);
  whether the 60 s stop and the return to dark keep phantoms harmless over many nights; the
  plain Touch-LCD-2 and the ESP32-S3-ETH with this code (both compile).

## Findings

- **Phantom touches are the touch panel's, and they press buttons.** Over 10-10 to 10-13 the
  panel reported whole 1 s presses nobody made: dark-screen wakes at 10-10 22:35, 10-12 03:29,
  10-13 04:40 and 04:53, and on 10-13 08:00:47, 45 s after HA's morning routine turned the screen
  on, a press on Manual Control that ran the fans at 100 % for 45 min until you stopped it.
  Many come 45-56 s after the backlight switches on or off; most sit on one row of the screen.
  No hold rule can tell them from a finger, so the fixes limit what one can do: 5 min of light,
  60 s of full speed.
- **The clock after a reset** comes from a timer that drifts about 1 % of the uptime (12.5 min
  slow after 21 h). The network time corrected the system clock within seconds, but the log's
  time library only re-read it every 5 min. Fixed by re-reading at once.
- **The W5500 and the overnight freeze are closed:** clean since the module swap on 10-08; the
  03:15 hang of 10-06 most likely had the same cause.
- **One MQTT first-connect stall** (8.7 s) at a power-on boot on 10-12, despite the 15 s wait:
  the first in about 20 boots since that fix. Noted only.
- **The board's USB port** disappeared from the PC for a while on 10-13 (COM15 missing, then
  back); the flash waited for it.
- **Graphify** (set up 10-12): useful for impact checks before a change (`affected`, `explain`)
  and as a map (`graph.html`); the code part is rebuilt for free with `graphify update .`.
  Not worth it here: queuing SD event writes (writing at once is what keeps the events before a
  crash), splitting `web_server.cpp`, its project hooks, or paid full rebuilds. Its useful hint:
  the touch driver ignores the chip's own event type and gesture code for each touch.

## Decisions

- A touch wake goes dark again after 5 min; an LCD override needs confirming within 60 s.
- The firmware audit is finished; the 16 MHz SD card trial is closed (3 clean days).
- `docs/TODO_DONE.md` holds finished items; two one-time moves today, then the two-week rule.
- Cards at a fixed 900 px width, for all projects (`WEB_STYLE.md`).
- Downloads: the collapsed month list, without a "whole year" download.
- `graphify-out/` stays out of git.

## Still open (TODO.md)

- **Watching:** phantom touches on the LCD (wake and override lines you didn't make, with their
  position). Not yet tried: logging the touch chip's own event type and gesture code per touch,
  to tell a phantom from a finger.

## Next steps

- At the start of the next session, Claude reads the event log: `wake press` and LCD `OVERRIDE`
  lines you didn't make, `LCD-idle`, watchdog, W5500, SD.
- At the next flash after a long uptime, check the boot event times (the clock fix).
- If you're using the other boards (plain Touch-LCD-2, ESP32-S3-ETH, COM8/COM9), one could be
  updated: flash backup, flash, the same checks.
