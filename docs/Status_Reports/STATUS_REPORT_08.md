# ETH_Touch_PWM - Project Status Report 08 - FINAL
**Date**: October 21, 2026
**Status**: **FINAL - project complete.** This is the closing wrap-up of ETH_Touch_PWM. The
feature set is finished and running; nothing is open in `../TODO.md`. **Any new work on this
system belongs to a V2.0 of the system** - a new project with its own repo/docs, not more
commits here. This repo stays as the reference and the running firmware's source.
**Last commit**: `40c1b1` (touch compiled out on the lite build), everything on `main`.
Bench board at close: Touch-LCD-2 + W5500 Lite (`6U_Rack_Fans`, COM15, Ethernet at .229).
Settings version 11. Image ~1.49 MB (47.5 % of the 3 MB OTA slot), free memory on Ethernet
~253 KB.

---

## What this project is

Two-channel 25 kHz PWM fan controller on ESP32-S3, running in the rack since late September
2026. Started 2026-09-27 as a PlatformIO port of the Arduino IDE sketch
`ESP32_S3_FanController_2inch` (sergeant82d/ESP32S3-Ethernet-Fan-Controller, `c23de85`) and
grew far past it in under four weeks.

## What it delivers

- **Fans**: two 25 kHz PWM channels with RPM tach, fan curve (averaged input, hysteresis,
  rate limit), per-fan RPM trim, manual override from web / HA / (until now) the LCD,
  both-probes-failed failsafe at full speed.
- **Temperature**: local DS18B20 probe plus a network temperature from Home Assistant, blended.
- **Home Assistant**: full MQTT integration with discovery - entities both ways, availability,
  daily summary, firmware version on the device page. No HA token needed.
- **Web page** (`web/index.html`, compiled in): the reference implementation of
  `D:\GitHub\WEB_STYLE.md` - sidebar tabs, themes as CSS variables, login on every change,
  OTA with a board-name check, History with monthly log downloads, System tab with the real
  build version.
- **Network**: W5500 Ethernet (static or DHCP) with a WiFi backup, a setup hotspot, mDNS
  device name, self-healing (NET/MQTT events, DHCP restarts, watchdog with a crash dump in
  the BOOT event).
- **SD logging**: minute logs and `daily.csv` kept for good, all-time record derived, every
  write checked, 16 MHz SPI.
- **LCD** (Touch-LCD-2 boards): dashboard with top-bar icons, follows HA/web on/off. Touch is
  compiled out on the lite build (see below) - the display stays.
- **Three builds from one code base**: `waveshare_s3_lcd2`, `waveshare_s3_lcd2_lite`,
  `waveshare_s3_eth` (`docs/BOARDS.md`). Settings upgrade in place across versions (v11 now).

## How it ended: the touch decision

The CST816D touch panel on the bench board reported phantom presses for two weeks (a sweeping
band at y 105-133, wakes and button presses ~26-56 s after the backlight switched, one that
ran the fans at 100 % for 45 min). Every containment helped - 1 s hold, 150 ms gap rule,
5 min idle, band ignore, Keep-On-only confirm, panel locks - but no rule tells a phantom from
a finger, and the chip's own event and gesture codes don't either. On 2026-10-21 you decided:
touch is compiled out on the `waveshare_s3_lcd2_lite` build (`-DTOUCH_ENABLED=0`, `40c1b1`).
The LCD stays and follows HA/web; the override panel and QR page are gone with the touch.
Replacing the Touch-LCD-2 board is the hardware fix if touch is ever wanted again - a V2.0
topic if ever.

## What was learned (details in `../PROJECT_HISTORY.md`)

- **Network**: lwIP has one global DNS server; both networks on one subnet keep new sockets on
  WiFi; Nagle off for MQTT's discovery burst; the first MQTT connect after boot stalls ~8 s
  for unknown reasons - a 15 s settle wait makes it never happen (0 of 16 boots).
- **W5500 Lite**: a bad module reset itself and starved loop() with SPI reads (module swap
  fixed it, 10-08); PHYCFGR and DHCP state now go into NET events.
- **SD**: the Arduino File layer ignores a failed final flush - writes now use fopen/fwrite/
  fsync and check each result.
- **Clock**: TimeLib vs SNTP after a reset (drifting RTC timer) - re-sync at once on every
  network time.
- **Watchdog + RTC snapshot + core dump** in the BOOT event found the two hard hangs; that
  tooling paid for itself immediately.
- **Windows/PlatformIO**: build from PowerShell, long paths on, `Serial.setTxTimeoutMs(1)`
  on native USB, esptool `--no-stub` for the blocks the stub can't read.
- **Graphify** knowledge graph: good for impact checks before a change; details in
  `D:\GitHub\GRAPHIFY_KNOWLEDGE.md`.

## State at close

- **Open items**: none (`../TODO.md` Open list is empty; finished work is in `../TODO_DONE.md`).
- **Accepted as-is** (documented, not fixed): the ~8 s first-MQTT-connect stall (mitigated by
  the 15 s wait, cause unknown); the 2026-10-05 overnight freeze (cause never found, the 30 s
  watchdog covers it); one MQTT first-connect stall in ~20 boots despite the wait.
- **UNTESTED at close**: the plain `waveshare_s3_lcd2` and `waveshare_s3_eth` builds with the
  final code (compile-checked only; behavior unchanged); `fan_controller_01` (ESP32-S3-ETH)
  has not been reflashed since 2026-09-28, so it runs older firmware.
- **Power**: the board needs external power with LCD + Ethernet + SD running (PC USB locks it
  up) - unchanged since the start.

## What V2.0 means

This repo and its docs freeze. Anything new - a different board, touch again (new hardware),
new sensors, LVGL, extra channels - starts as **V2.0** with its own project folder, CLAUDE.md,
docs and TODO, seeded from this one as a reference. Flash backups of the running boards live
in `../ETH_Touch_PWM_backups` (outside the repo).

## Where everything lives

| What | Where |
|---|---|
| How it works, rules, success criteria | `../../CLAUDE.md` |
| History, gotchas, findings, log | `../PROJECT_HISTORY.md` |
| Boards, pins, OTA, login recovery | `../BOARDS.md` |
| MQTT topics, entities, HA automations | `../MQTT.md` |
| New-board setup | `../SETUP.md` |
| Finished work list | `../TODO_DONE.md` |
| Audit (2026-10-06) | `../AUDIT_2026-10-06.md` |
| Status reports 01-08 | `./` |
| Web standard (this page is its reference) | `D:\GitHub\WEB_STYLE.md` |
