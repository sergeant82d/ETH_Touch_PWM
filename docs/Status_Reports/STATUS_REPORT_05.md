# ETH_Touch_PWM - Project Status Report 05
**Date**: October 6, 2026
**Session**: overnight failures, event logging, LCD icons, memory, firmware audit and first fixes
**Status**: Final for the day. Everything is on `main` (last code commit `aaebfb7`). Bench board:
Touch-LCD-2 + W5500 Lite (`6U_Rack_Fans`, COM15, Ethernet at .223, WiFi backup at .222).
Settings version 10 (unchanged).

For how things work, see `../../CLAUDE.md`, `../PROJECT_HISTORY.md` (updated today),
`../BOARDS.md` (self-heal, Ethernet details, slow steps), `../LCD_DESIGN.md` (new) and
`../AUDIT_2026-10-06.md` (new). Open items: `../TODO.md`.

---

## What changed

| Area | Result | Commit |
|---|---|---|
| **Event log** | Network and MQTT events (link, address, WiFi, hotspot, connect, drop and why), every settings change field by field, finer watchdog stages | `c3f1121` |
| **Self-heal** | MQTT down 15 min with a working network = restart, waiting longer each time (15-240 min); none without a network | `c3f1121`, `dbcb402` |
| **Web** | Phones: Manual override right below the LCD view; History tab next-to-last; a settings save reconnects MQTT only when an MQTT setting changed | `66e1804` |
| **LCD icons** | Your top-bar design: Ethernet, WiFi, hotspot left of the name, MQTT and SD right; grey / green / flashing; the web LCD view the same | `f861f37` |
| **Web LCD view** | "No connection" overlay when status updates stop (in place of "Display off") | `47c66da` |
| **Memory** | Lowest free memory tracked with the part of the program running then; a WiFi scan nobody collected kept ~30 KB, now dropped after 30 s | `4ca5a3b` |
| **Memory target** | CLAUDE.md: ~200 KB on Ethernet, ~120 KB while on WiFi | `b13c655` |
| **Audit, first batch** | MQTT moves once when Ethernet returns (was twice); Ethernet without an address: DHCP retried, then a backed-off restart; W5500 Lite at 10 MHz (test) | `dbcb402` |
| **Diagnostics** | Ethernet speed, time to address, DHCP client state and the W5500's own link register in the event log; any loop step over 5 s logged as "Slow" | `aaebfb7` |
| **Docs** | LCD design note (LVGL archive, icons, display conventions); audit items in TODO in the agreed order; BOARDS, CLAUDE.md | `3aa575e` and with each change |

Build: ~1.48 MB, 47 % of the 3 MB app slot; RAM 17 %; all three builds compile with no
warnings.

## Checked on the board (COM15)

- **You:**
  - settings changes show in the log on the board and the web page
  - the phone layout and History tab position
  - the LCD icons, normal and with the cable out
  - the "No connection" overlay (cable out and back)
  - three cable pulls for the failover and the new Ethernet lines
- **Logs and tests by Claude:**
  - boot events clean after each flash
  - cable out and back: one 1 s MQTT reconnect, when WiFi goes off (it was two)
  - Ethernet lines: `link up, 100 Mbps full`, `address ... after 4.5 s`, link down with
    `DHCP client not started; W5500 says link down`
  - memory over the day: no leak; ~252 KB free on Ethernet now
- **UNTESTED:** the Ethernet "no address" retry, both self-heal restarts and the "Slow" event
  (they need the faults); the plain Touch-LCD-2 and the ESP32-S3-ETH with today's code (both
  compile).

## Findings

- **03:16 watchdog restart** (`stage=network`) while HA rebooted after its 03:03 backup. The
  cause is still open; the finer stage names will say which call next time.
- **05:57-07:10, 73 min off MQTT.** HA and Mosquitto logs were clean, so the board decided it had
  no network. Most likely Ethernet lost its address and never got it back, as on COM9 on
  2026-09-29. Now retried, and the log will show which of three causes it is.
- **The failover reconnect** the audit predicted was in this morning's log and is fixed.
- **Memory:** the 143-147 KB lows were the WiFi backup being joined (its driver takes ~110 KB),
  not a leak; hence the new target.
- **Audit** (your commit `be0d257`): sound overall. Corrections agreed: its MQTT publish fix would
  reconnect in a loop on an oversized message; the no-card retry cost should be measured
  first; the SD buffer drain can stay a single pass with checked writes.
- **A 21 s pause** at one boot (10:24), MQTT's first connect after your HA restart; not seen
  again. The new "Slow" event would name it.
- **Touch I2C** read error at boot (10:25:03), now in the event log: the known parked one, touch
  works afterwards.
- **The SD card** runs on plain SPI at 4 MHz on both boards (your question); no need to change.

## Decisions

- Free memory target ~200 KB on Ethernet, ~120 KB on WiFi.
- Touch I2C stays parked; Claude scans the event log at each session start (TOUCH, TASK_WDT,
  self-heal, NET/MQTT drops).
- The audit's items go in TODO in the agreed order; the first batch is done.
- DHCP reservation or a static address for the board: decide once the new logs show the cause.
- `docs/HA_Data/` (HA exports) is ignored by git.

## Still open (TODO.md)

- **Watching:** the overnight freeze, the 03:15 network-stage hang, Ethernet without an
  address (with the new diagnostics); touch I2C (parked).
- **Audit, next:** checked SD writes and the buffer drain; the day's highs and lows kept through
  a restart; safe rewrites of the rollup files; MQTT waits; measure the no-card retry; web
  request limits; tidy-ups.
- **Your note:** rename the "Setup hotspot" card on the Network tab to "HotSpot password".

## Next steps

- Let the board run overnight through HA's 03:03 backup. At the start of the next session,
  Claude reads the event log for TASK_WDT, self-heal, LOOP, TOUCH and NET/MQTT drops.
- Then the next audit batch (SD writes and daily values), or the hotspot card rename.
