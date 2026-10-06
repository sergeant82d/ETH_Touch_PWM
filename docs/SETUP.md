# Setting up a new board

Updated 2026-09-29 for the Setup page (first written the same day, when setup meant visiting
five tabs). Flashing: `docs/BOARDS.md`. Home Assistant details: `docs/MQTT.md`.

## What a new (or erased) board starts with

| Setting | Factory value | Effect |
|---|---|---|
| Web login | none | The **Setup page** opens; every other change is refused until setup is saved |
| Ethernet | DHCP | The router picks the address |
| Node ID (controller name) | `fanController_xx` | The Setup page suggests `fanController_<last 4 MAC digits>` |
| MQTT (Home Assistant) | off | Ticked on the Setup page if wanted |
| Fans | 1 | |
| Time zone | America/Chicago | The Setup page suggests the browser's time zone |
| WiFi backup | none | |
| Setup hotspot | `FanController-<last 4 MAC digits>`, password `12345678` | Starts after 60 s with no Ethernet or WiFi |

The factory values for the broker (192.168.10.85) and the static-address fields come from
the original Arduino sketch and match the author's network; an erase can't remove them.
A board flashed over an older version of this firmware keeps its settings (they upgrade in
place); only a new or erased board starts from this table.

## Steps

1. **Connect and power.** Plug in the Ethernet cable before powering up. Use external power:
   the Touch-LCD-2 locks up on PC USB power with the LCD, Ethernet and SD running.
2. **Follow the LCD.** While no login is set, the LCD shows a setup screen instead of the
   dashboard (a tap shows the dashboard; the screen comes back after 2 minutes):
   - *No network yet:* a countdown to the setup hotspot, with its name and password.
   - *Hotspot on:* QR 1 joins the hotspot, QR 2 opens `http://192.168.4.1`.
   - *On Ethernet or WiFi:* a QR code of the page's address, plus the IP and `.local` name.

   The ESP32-S3-ETH has no LCD: find it in the router, or at `http://fancontroller-xx.local`.
3. **Fill in the Setup tab** (it opens by itself) and press **Save and restart**:
   1. *Name and login:* the controller name (shown on the LCD and the page, gives the
      `.local` address, names it in Home Assistant) and the web login.
   2. *Network and time:* Ethernet DHCP or static, an optional backup WiFi (Scan networks),
      time zone, 12/24 h.
   3. *Fans:* how many are connected, °F or °C.
   4. *Home Assistant (optional):* tick it for the MQTT broker and login.

   The controller restarts once. The page says where to find it and moves there after 20 s
   (by IP for static, by `.local` name for DHCP). On the hotspot it says to rejoin the normal
   network first. Log in there with the new login.
4. **Afterwards, on the tabs:** the fan curve (Fan Control), the WiFi address and hotspot
   password (Network), device name, time zone and theme (System). Each panel has its own Save.
5. **Home Assistant (in HA):** the device appears by itself once MQTT is connected. Add the
   network-temperature automation from `docs/MQTT.md` with the controller name in the topic
   (`<name>/network_temp/set`; the Setup page shows it). Until it runs, the page shows "No
   value from HA", which is normal for a minute or so after a restart.
6. **Check:** the Dashboard shows temperatures and fan RPM; the sidebar shows Ethernet, MQTT
   connected and the SD card; the LCD dots are green.

## Still rough (TODO.md)

- On the normal tabs, each Save reloads every form, which wipes other panels' unsaved fields,
  and nothing warns about unsaved fields.
- A node ID change on the Home Assistant tab changes the `.local` name only from the next
  restart or a Save in the Device name panel (System tab).
- The hotspot turns itself off 30 s after a network works, which drops a phone mid-step.
- No factory reset on the web page yet (only the USB erase, `docs/BOARDS.md`).
