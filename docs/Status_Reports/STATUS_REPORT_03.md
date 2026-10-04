# ETH_Touch_PWM - Project Status Report 03
**Date**: October 4, 2026 (covers September 29 and October 4)
**Session**: W5500 Lite build, first-time setup, network fixes, LCD work, steadier fans, per-fan offset
**Status**: Final for the day. Everything is on `main` (last code commit `a1d61d4`). Bench board:
Touch-LCD-2 + W5500 Lite (`6U_Rack_Fans`, COM15). Settings version 10.

For how things work, see `../../CLAUDE.md`, `../PROJECT_HISTORY.md` (updated today),
`../SETUP.md`, `../MQTT.md` and `../BOARDS.md`. Open items: `../TODO.md`.

---

## What changed

### September 29

| Area | Result | Commit |
|---|---|---|
| **Third build** | `waveshare_s3_lcd2_lite`: Touch-LCD-2 with a W5500 Lite on your pins (Ethernet on the right header, fans and probe on the left) | `59d3b9b` |
| **First-time setup** | Setup tab while no login is set: name and login, network and time, fans, Home Assistant (optional); one Save, one restart; name suggested from the MAC. LCD setup screen with QR codes; hotspot shown on the LCD | `1567e95` |
| **Docs** | `SETUP.md` (setting up a new board), how to change pins or add a build (`BOARDS.md`) | `2265448`, `35a04dd` |

### October 4

| Area | Result | Commit |
|---|---|---|
| **WiFi without Ethernet** | Joins the saved WiFi at once (was after 30 s); the hotspot no longer starts while WiFi is still joining | `b730576` |
| **NTP after network switches** | The DNS server is set again and the time sync restarted after every switch and hotspot on/off; synced within 2-4 s in all four switch tests | `d4b8b9f` |
| **Crash** | Saving WiFi after the hotspot had been on and off crashed the board (mDNS); fixed | `d4b8b9f` |
| **Clock after a power cut** | Set ~12 s after a cold start (took ~5 minutes) | `a1d61d4` |
| **LCD layout** | Status dots network, MQTT, SD; network bar left, local right; clock and big temperature repositioned; alert colour from the fan curve (75 %) | `b730576` |
| **LCD standby** | Backlight off/on from HA ("LCD display"), the web page, or a tap | `b730576` |
| **Touch** | The mapping is right; the gear missed because taps near the top edge read 30-60 px low. Its touch area is now the whole top-right corner | `5cf0e9a` |
| **Override panel** | No leftovers after closing; the border is no longer cut by the slider label | `ba75cd0` |
| **Web** | Factory reset (System tab); node name in Title Case | `64cb852` |
| **Loop pauses** | MQTT reconnects, idle browser connections and probe reads no longer hold the board up (was 0.6-2.8 s each) | `64cb852` |
| **Steadier fans** | Curve on a ~30 s average, hysteresis at the start, gradual speed changes | `ea3557e` |
| **Per-fan offset** | Fan 2 held at fan 1's RPM ±N (10 RPM steps, ±500), with Reset; settings version 10 | `a1d61d4` |

Build: ~1.48 MB, 47 % of the 3 MB app slot; all three builds compile. Free memory ~248-256 KB
on Ethernet.

## Checked on the boards

- **You:**
  - setup on a never-flashed board; Lite pins (fans, tach, probe, W5500)
  - both-probes-failed failsafe; plain Touch-LCD-2 build
  - hotspot and static WiFi; LCD layout (approved)
  - gear touch ("feels good"); override panel artifacts gone
  - clock after a power cut
- **Logs, with you doing the unplugging and tapping:**
  - four network switches with the time syncing each time
  - the crash sequence repeated without a crash
  - every touch target
  - 4 minutes with no loop pauses
  - fan offsets +200 (held +191 to +214) and -200 (-173 to -227)
  - settings v9 → v10 kept everything
- **UNTESTED:**
  - the factory reset itself (only its refusal without a login)
  - the HA-restart re-announce
  - fan smoothing over a whole day (watch the History tab)
  - the plain LCD build and the ESP32-S3-ETH with today's code (both compile)

## Problems found and fixed

- **Stale DNS:** after a network switch, the time sync could lose its DNS server.
- **mDNS crash:** saving WiFi after the hotspot had been used crashed the board.
- **Slow clock after a power cut:** a 300 s retry interval overrode the 5 s one.
- **Settings upgrades:** any upgrade switched a DHCP board to static. Caught before version 10 shipped.
- **Override panel:** closing it left artifacts.
- **Gear touch area:** too small for how this panel reads taps near its top edge.

## Decisions

- **You'll edit the Lite pins yourself** from here (`BOARDS.md` has the steps).
- **Fans:** steadier fans by smoothing and hysteresis rather than PID. PI only if the History tab still shows hunting.
- **Per-fan offset in RPM,** not duty; settings version 10 approved.
- **Cancelled:** HA plotting YAML, the move to native ESP-IDF.
- **Moved down:** SD card items (lower priority); LVGL to the bottom (after your design images).
- **Flicker waits;** not worried about memory.

## Still open (TODO.md)

- **Watching** (need it to happen again):
  - Ethernet getting no address after a drop (your power theory)
  - the one 3-minute stall with the cable out
  - Override slider (low priority)
- **Can do:**
  - LCD flicker (waiting)
  - the boot-log message "GPIO isr service already installed"
  - an LCD page for the fan offset
- **Lower down:** two SD card items (a failing card stalls the page; some cards drop out after
  the buffered data is written).
- **Bottom of the list:** LVGL.

## Next steps

- Watch the fan behaviour in the History tab over a day or two.
- Try the factory reset when a board needs setting up again anyway.
- When the hardware is wired for good: re-check the Ethernet reconnect and the slider.
