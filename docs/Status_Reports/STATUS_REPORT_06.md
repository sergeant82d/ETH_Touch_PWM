# ETH_Touch_PWM - Project Status Report 06
**Date**: October 10, 2026 (also covers October 8, which had no report of its own)
**Session**: new W5500 module, the firmware audit finished, MQTT first-connect stalls, HA Restart
button, daily history rebuilt, LCD gauge settings
**Status**: Final for the day. Everything is on `main` (last code commit `c675010`). Bench board:
Touch-LCD-2 + W5500 Lite (`6U_Rack_Fans`, COM15, Ethernet at .229 and WiFi backup at .190, both
reserved on the router). Settings version 11 (was 10; older files upgrade in place).

For how things work, see `../../CLAUDE.md`, `../PROJECT_HISTORY.md` (updated both days),
`../BOARDS.md`, `../MQTT.md` (new entities, connect rules) and `../AUDIT_2026-10-06.md`. Open
items: `../TODO.md`.

---

## What changed

| Area | Result | Commit |
|---|---|---|
| **HA Restart button** | "Restart" in the device's configuration section; presses in the first 30 s of a connection are ignored (a stuck retained message can't restart it over and over) | `d2d7292` |
| **Web** | "Setup hotspot" card renamed "HotSpot password"; every card title in Title Case (rule added to `WEB_STYLE.md`, item added to Wifi_Fan_Knob's TODO) | `68a8af5`, `d3cd279` |
| **SD writes (audit 3.1, 3.2)** | Every write checked to the end (the old file layer ignored its final flush); a failed write marks the card missing; the internal buffer is deleted only once all of it is on the card, and a buffer left over a restart is written after boot | `5cff60e` |
| **Daily values (audit 3.3, 3.6)** | The day's highs and lows survive any restart but a power cut; a restart across midnight closes the right day; `daily.csv` is kept for good (was 30 days); the all-time record is worked out from it and swapped in safely | `44b9ffb` |
| **History rebuilt** | `daily.csv` rebuilt from the minute logs, 2026-09-27 to 10-09 (`tools/rebuild_daily.py`), fan 2's two glitches left out; all-time record recalculated | `fa6cb4a`, `415b202` |
| **MQTT (audit 4.1)** | Connects 15 s after the network comes up (the first connection after a boot stalled 1-10 s in 70-90 % of boots); every send and the subscribe checked, a stuck one closes the connection; slow sends logged | `4765c4f` |
| **Network** | No W5500 at boot no longer crashes in a loop (it now runs on WiFi); W5500 Lite back at the normal 20 MHz | `4765c4f` |
| **LCD gauge settings** | Your note: each fan's Max RPM (top of its bar) and the temperature bars' range, on the Fan Control tab and in HA | `415b202` |
| **Web requests (audit 5.1)** | Headers 3 s in all, lines capped at 512 characters, a page or download stops on a failed send or after 60 s | `0a84aaa` |
| **Tidy-ups (audit 5.2, 5.3)** | The SD buffer's size kept in memory (was read from flash ~8 times a second); uptime no longer drops to 0 after 49.7 days | `2346746` |
| **SD card speed** | 16 MHz instead of 4 MHz, on trial | `c675010` |
| **Docs** | Nagle explained in PROJECT_HISTORY; the Wifi_Fan_Knob changes of 10-07 read and applied; TODO kept current with each change | `769e7df`, `8df06fe` and with each change |

Build: ~1.49 MB, 47.5 % of the 3 MB app slot; RAM 17 %; all three builds compile with no new
warnings. Free memory on Ethernet ~253 KB.

## Checked on the board (COM15)

- **You:**
  - replaced the W5500 Lite module (10-08 15:25) and set both DHCP reservations
  - the HA Restart button, the renamed card, the Title Case titles
  - a card pull for the SD write checks (10-08), two today: the `daily.csv` copy and the
    no-card measurement
  - the History tab with the rebuilt days
  - the gauge settings on the web page, the LCD and in HA (a Max RPM save from the web)
  - every tab after the web request limits, and the History tab at 16 MHz
- **Logs and tests by Claude:**
  - ~80 restarts from an MQTT test script to pin down the first-connect stall; with the fix,
    every connection smooth (discovery 0.1 s)
  - the midnight rows for 10-08 and 10-09 written on time; the day's values kept through
    restarts (`hi/lo kept` in the BOOT events)
  - the rebuilt 10-09 row identical to the board's own (proof the script matches the firmware)
  - gauge settings from HA: applied, logged, out-of-range values refused, settings upgraded
    from version 10 with nothing lost
  - web limits: a stalled request held the board 3.0 s, a 100 KB header line answered, a very
    slow reader dropped after 15 s with no restart
  - uptime after a restart from HA: `uptimeAtReset=134s` at 135 s
  - 16 MHz: downloads ~1.5 times faster (month log 1.75 -> 1.21 s), files identical
- **Full flash backups** before the risky flashes: `COM15_Lite_67f63b1_full-flash_2026-10-08.bin`
  and `COM15_Lite_4765c4f_full-flash_2026-10-10.bin` in `ETH_Touch_PWM_backups` (restore
  command in `backup_log.txt`).
- **UNTESTED:** the plain Touch-LCD-2 and the ESP32-S3-ETH with this code (both compile; the
  ESP32-S3-ETH's SD wiring at 16 MHz too); a real dead W5500 (only simulated); a write failing
  partway through the buffer write-back; the 60 s download limit itself; the SD buffer past
  80 % full.

## Findings

- **The W5500 module was the problem.** The old one reset itself 5 times in ~34 h and twice
  froze the board (crash dumps: its driver task stuck reading garbage from the chip). Since the
  new one went in: no resets, no watchdog restarts, 42 h untouched without a fault.
- **The MQTT first-connect stall** was not Nagle, not the W5500, the SPI speed, the broker or
  Ethernet (WiFi stalled the same way). TCP resent a message after 3, 6 s ... Cause still
  unknown; waiting 15 s after the network comes up avoids it every time (0 of 16 stalled).
- **The no-W5500 boot loop** was found by accident during a test that took the board down
  ~15 min (the LCD went blank; you noticed). Fixed; lesson: watch a test build's first boot.
- **No-card retries (audit 3.4)** hold the board 0.5-1.0 s every 15 s while the card is out:
  LCD and touch pause, fans, MQTT and the web carry on.
- **The SD card was the download bottleneck** at 4 MHz (~200 KB/s, now ~315 KB/s).
- **Full flash reads** fail on a few blocks with esptool's fast loader; read with `--no-stub`
  for those (not always the same blocks).
- **After the 5-minute flash read** the board's first boot logged events ~7 min fast until the
  network time arrived. Harmless.
- **Event order:** once, the SD "card OK" line was logged before the buffered "card missing"
  line (only the order; low-priority item).
- **10-09 23:58:** the LCD came back on 56 s after HA turned it off (`source=LCD`); asked,
  no answer yet. Today's LCD override on/off at 14:48 was a stray touch (you).

## Decisions

- `daily.csv` kept for good (~30 KB a year); rebuild from 09-27, fan 2's glitches left out.
- DMA LCD drawing: not for this project; the default for future ones where it fits (global
  `CLAUDE.md`).
- Restart button in HA with the 30 s guard; card titles in Title Case on all projects.
- Max RPM entered per fan (there is no fan calibration in this project).
- Audit 3.4 left as it is; 3.5 skipped; 1.1 covered by the global display rule; SD at 16 MHz on
  trial. **The firmware audit is finished.**

## Still open (TODO.md)

- **Close on 10-12 if the log is still clean:** the Ethernet/W5500 item and the overnight
  freeze item.
- **Trial:** SD at 16 MHz (close after 2 quiet days, or back to 4 MHz).
- **Watching:** the 03:15 network-stage hang, touch I2C (parked).
- **Low priority:** the event-order quirk after a card returns.

## Next steps

- At the start of the next session (10-12), Claude reads the event log: `TASK_WDT`, W5500
  resets, SD errors, `MQTT slow send`, the midnight rows. If clean, close the W5500, freeze and
  16 MHz items.
- Question for you then: was the LCD turning on at 10-09 23:58 your touch?
