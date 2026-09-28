# ETH_Touch_PWM - Project Status Report 02
**Date**: September 28, 2026
**Session**: Second session day: TODO layout, themes, notes, History tab, SD health, QR page, LCD view, MQTT dot
**Status**: Final for the day. Everything is on `main` and running on the ESP32-S3-ETH
(fan_controller_01). The Touch-LCD-2 (fan_controller_02) still hasn't run any of this firmware.

For how things work, see `../../CLAUDE.md`, `../PROJECT_HISTORY.md`, `../MQTT.md` and
`../BOARDS.md`. Open items: `../TODO.md`.

---

## What changed today

| Area | Result | Commit |
|---|---|---|
| **TODO and docs** | `TODO.md` in the tagged layout (your notes on top, Open oldest first, Done newest first); project `CLAUDE.md` trimmed to what the global one doesn't cover; BLE question answered | `e543ab5` |
| **Themes** | The Wifi_Fan_Knob gold/navy is its own preset "Navy & gold"; "Classic dark" is now a neutral dark. Five presets | `5760360` |
| **Notes box** | Dashboard card: scrollable, emoji row, 4000-byte counter, "last saved ... by ...", unsaved warning; stored on the board, saving needs the login | `2e0e286` |
| **History tab** | Day chart (temperatures with the fan curve lines, fans with RPM, duty and override), last 30 days, all-time records, events with filters, downloads. The per-minute log gained duty and override; CSV files have column names | `7bf5979`, `c743c41` |
| **Sidebar** | IP address under the node name; WiFi or hotspot name while on WiFi | `7bf5979` |
| **SD health** | One state everywhere (LCD dot, web dot and System row, HA sensors and fault, event log): OK green, 90 % or more used orange slow flash, missing red fast flash. A card pulled while running is now noticed | `6bb7e19` |
| **QR code page (LCD)** | Tap the gear: QR code of `http://<IP>/`, plus one to join the hotspot while it is on. Any tap or 60 s closes it | `4026820` |
| **LCD view (web)** | Dashboard card that redraws the Touch-LCD-2 screen from live data every 200 ms; fonts approximate | `6d41e3c` |
| **MQTT light** | Third title-bar dot, on the LCD and in the web LCD view: green connected, orange not | `d5b909f` |
| **Wifi_Fan_Knob** | Reviewed which of today's features fit the Knob; wrote `docs/HANDOFF_FROM_ETH_Touch_PWM.md` there. The Knob session has since done items 1-4 | (Knob repo) |

Build sizes after the last change: Touch-LCD-2 1,427,558 B (45.4 % of the 3 MB slot),
ESP32-S3-ETH 1,389,746 B (44.2 %). No warnings.

## Checked today (ESP32-S3-ETH, by you or on the board)

- Themes (both changed presets), notes save/reload, notes refused without the login.
- History tab: day view on real data, downloads with column names, events and filters.
- SD card pulled and put back twice: missing detected, remounted, events logged, HA entities present.
- SD daily rollup over a real day change; the first "Summary of the day" arrived in HA.
- Time zone picker; the new web page on PC and phone.
- LCD view card, including the override flashing and the MQTT dot (green).

## Problems found and fixed today

- **SD card pulled while running was never noticed** (writes were only buffered). Now checked every 10 s, remount tried every 15 s.
- **Events logged while the card was out** were written into the month log instead of `events.csv`.
- A failed probe logged its last value; it now logs an empty field.

## Not yet checked

- **Touch-LCD-2:** everything, now also the QR page, the SD dot and the MQTT dot on the real LCD.
- SD card 90 % full; Celsius and both-probes-failed in the LCD view.
- 30-day history and records on real data (need more daily rollups on the new card).
- Column names in files the board creates itself (next rollup, next month file).
- Setup hotspot, static WiFi address, the WiFi name line on a real WiFi connection.

## Decisions made

- TODO.md layout and routine (notes turned into items with each status report).
- "Shared network backup" dropped from the web plans.
- LCD view as a redraw from live data (not a screen copy); fonts don't need to match.
- The QR page went to `main` before testing, at your choice.
- A short "No value from HA" for the network temperature after a restart is normal.

## Waiting on you

- Touch-LCD-2 (or the new W5500 Lite build): check the fan pin wiring, then flash it.
- Your eight notes from this evening are now Open items in `TODO.md`; the two questions,
  the HA plotting request and the LVGL discussion wait for you to ask for them.

## Next steps

- Answer the open questions (PID control, ESP-IDF) and the LVGL and HA-plotting discussions.
- Web: node ID in Title Case in the page corner; per-fan speed offset.
- New board build for the W5500 Lite and your pin choices.
