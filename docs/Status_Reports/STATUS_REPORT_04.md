# ETH_Touch_PWM - Project Status Report 04
**Date**: October 5, 2026
**Session**: overnight freeze, display standby, Cancel, restart button, web layout, override buttons, LCD flicker
**Status**: Final for the day. Everything is on `main` (last code commit `5eec4c8`). Bench board:
Touch-LCD-2 + W5500 Lite (`6U_Rack_Fans`, COM15, Ethernet at .223). Settings version 10 (unchanged).

For how things work, see `../../CLAUDE.md`, `../PROJECT_HISTORY.md` (updated today),
`../BOARDS.md` (new section: restart, watchdog, boot events) and `../MQTT.md`. Open items:
`../TODO.md`.

---

## What changed

| Area | Result | Commit |
|---|---|---|
| **Overnight freeze** | The board froze at 05:05 with no crash logged. A 30 s watchdog now restarts it, and the BOOT event says which part of the program was stuck and how much memory was free | `b91c006` |
| **LCD standby** | The screen turned itself back on: with the backlight off, the touch panel reports phantom taps. A 1 s press wakes it now | `b3188e5` |
| **Override Cancel** | Cancel did end the override, but the fans eased down from 100 % for ~2.5 min. They now go straight to the curve's speed | `7e098a9` |
| **Restart button** | System tab, just above Factory reset (asks to confirm). LCD: gear (QR page), hold Restart 2 s, the button fills as you hold | `f9a23ce` |
| **Web layout** | Network and WiFi in one tab; System tab in your order; on phones the LCD view is the first Dashboard card | `f5d3bc7` |
| **Override - / +** | LCD: big - and + instead of the slider. Web: - and + at the slider's ends. Tap 1 %, hold to repeat, 5 % steps after 2 s | `80283a7` |
| **LCD flicker** | Each part of the screen redraws only when what it shows changes; bars no longer blink through black | `5eec4c8` |
| **Docs** | PROJECT_HISTORY, BOARDS, MQTT, SETUP, TODO | `9612e92` and with each change |

Build: ~1.47 MB, 47 % of the 3 MB app slot; RAM 17 %; all three builds compile.

## Checked on the board (COM15)

- **You:**
  - wake on a 1 s press (a tap does nothing)
  - Cancel brings the fans down at once
  - Restart from the web and the LCD
  - the new Network and System tabs on PC and phone
  - - / + on the LCD and the web
  - the flicker is gone ("screen looks great")
- **Logs and tests by Claude:**
  - a deliberate hang restarted the board, logged `reason=TASK_WDT stage=web`
  - LCD off over MQTT: 19 phantom taps in 2 minutes, the screen stayed dark
  - override 100 % then off over MQTT: back to 34 % at once
  - `/api/restart` refuses without the login
- **UNTESTED:** the plain Touch-LCD-2 and the ESP32-S3-ETH with today's code (both compile).

## Findings

- **The freeze:** the last log row was 05:05:16; nothing until the reset at 09:52 (logged as a
  cold boot). The cause is unknown. If it happens again, the BOOT event says where.
- **Phantom taps:** after the backlight goes off, the touch panel reports bursts of taps 16-25 s
  later, mostly along one row halfway up the screen. They are very rare with the backlight on.
  This is worth checking once the hardware is wired for good (is a wire running behind the
  screen?).
- **Free memory:** lowest 143 KB after ~4.7 h on WiFi, below the 150 KB target in CLAUDE.md;
  ~250 KB on Ethernet.
- **My mistake:** my serial log reader reset the board twice this evening (19:07, 19:12, logged as
  `reason=USB`). Fixed; noted in PROJECT_HISTORY.
- **"GPIO isr service already installed"** in the boot log: harmless (two parts of the firmware
  install the same interrupt service); closed.
- MQTT took 2 s to connect once at boot: within your 10 s, no action.

## Decisions

- **LVGL cancelled.** It's kept as an archive note with your icon design for the top bar.
- Restart on the LCD's QR page rather than a new swipe page.
- Waking the screen and restarting need a hold, because of the phantom taps.
- Override: the LCD has buttons only; the web keeps the slider with buttons.
- The Ethernet DHCP, 3-minute stall, Override slider and both SD items are closed for now, with
  notes in case they come back.

## Still open (TODO.md)

- **Watching:** the overnight freeze (look for `TASK_WDT` in the event log); lowest free memory
  on WiFi.
- **To do:** LVGL archive note with the icon design and a short discussion of display
  conventions.
- **Low priority:** one touch I2C read error at boot.

## Next steps

- Let the board run overnight. Tomorrow, check the event log for a `TASK_WDT` BOOT event and
  the History tab for gaps.
- Write the LVGL archive note when convenient.
