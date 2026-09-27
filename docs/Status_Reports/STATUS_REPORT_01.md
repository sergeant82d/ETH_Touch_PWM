# ETH_Touch_PWM - Project Status Report 01
**Date**: September 27, 2026
**Session**: First session day: PlatformIO port, MQTT for Home Assistant, second board, new web page, WiFi backup
**Status**: Final for the day. Everything is on `main` (last commit `ec8026c`) and running on the
ESP32-S3-ETH (fan_controller_01). The Touch-LCD-2 (fan_controller_02) has not run any of it yet.

For how things work, see `../../CLAUDE.md`, `../PROJECT_HISTORY.md`, `../MQTT.md` and
`../BOARDS.md`. Open items: `../TODO.md`.

---

## What changed today

| Area | Result |
|---|---|
| **Port** | The Arduino IDE sketch builds in PlatformIO (same core 3.3.11, partitions, PSRAM, USB settings) |
| **Home Assistant over MQTT** | HA finds the controller by itself (discovery): temperatures, fan speed/duty/faults, fan curve start/top, manual override, daily summary, IP/uptime. The REST link, HA token, helpers, `rest_command` and their automations are gone; HA sends the network temperature by one automation |
| **Second board** | The Waveshare ESP32-S3-ETH is a second build (`-e waveshare_s3_eth`): its own pins, SD bus, no LCD. It now runs as fan_controller_01 |
| **Web page** | Rebuilt as a real page (`web/index.html`) with tabs on the left (Dashboard, Fan Control, Home Assistant, Network, WiFi, System), four themes (NUT, classic dark, classic light, custom), a web login for every change, OTA updates that refuse the other board's firmware, and a drop zone for the firmware file |
| **Fan channels** | Table with pins and live speed; fans are ticked in order, fan 1 always on; channels without pins show "not wired" |
| **Time** | Time zone picker (world list, daylight saving automatic); time from SNTP |
| **Network** | Ethernet on the ESP32's own driver (unique MAC per board), static or DHCP; WiFi backup when the cable is out; setup hotspot when there is no network; device name `http://fancontroller-01.local` |
| **Settings** | Stored settings upgrade in place on every firmware update (versions 4 to 9 so far); the old HA token is wiped |
| **Tools and docs** | MQTT simulator (`tools/mqtt_sim.py`), `MQTT.md`, `BOARDS.md`, `TODO.md`, new success criteria in `CLAUDE.md` |

## Checked today (ESP32-S3-ETH)

- Fans (PWM and RPM), probe, network temperature from HA and blending.
- Home Assistant: entities, controls both ways, fan add/remove, availability. (The fault notification automation is set up but hasn't had a fault to report.)
- Web page: all tabs, login, OTA installs and refusals, themes.
- Network: Ethernet static and DHCP, WiFi backup with the cable pulled and back, device name.
- Settings upgrades from version 5 through 9, keeping IP, node ID, login and thresholds.

## Problems found and fixed today

- **Failsafe:** with both probes failed, the fans stopped instead of running at full speed (the Arduino build still has this).
- **USB serial:** with the USB cable in a PC that wasn't reading, every serial print waited ~2 s; MQTT kept dropping. Fixed.
- **Choosing 1 fan** was reset to 2 at every boot.
- **Every board had the same MAC address.** Now each uses its chip's own.
- Build problems along the way (library download failures, a file name clash with the core's `Network.h`, SNTP starting too early); all written up in `PROJECT_HISTORY.md`.

## Not yet checked

- **Touch-LCD-2:** everything, including LCD, touch and the settings upgrade from the old Arduino build. Check the fan pin wiring first (pin remap in `TODO.md`).
- **ESP32-S3-ETH:** setup hotspot, static WiFi address, and the first "Summary of the day" at midnight.
- Changing the time zone on the page.

## Decisions made

- Home Assistant talks MQTT only; the node ID names the device (MQTT stays off while it is `fanController_xx`).
- WiFi is a backup only (joins after 30 s without Ethernet, leaves 60 s after it is back); the hotspot is for setting up.
- New or reset boards start on DHCP with fan 1 only; boards that are already set up keep their settings.
- Classic dark theme uses the Wifi_Fan_Knob gold on navy (still to confirm).
- `mqtt` merged into `main` and the branch deleted.

## Waiting on you

- Confirm or change the classic dark colours.
- Flash the Touch-LCD-2 when it is available (over USB, or OTA once it runs this firmware).

## Next steps

- First "Summary of the day" in HA tonight.
- Touch-LCD-2 first flash and checks.
- Later, from the Claude web plans: Home tab notes, history viewer.
