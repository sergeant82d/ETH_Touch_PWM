# Setting up a new board

Written 2026-09-29 from the code as it is now (firmware `59d3b9b`). It describes what the
firmware requires today, not how it should be; the rough spots are listed at the end.
Flashing: `docs/BOARDS.md`. Home Assistant details: `docs/MQTT.md`.

## What a new (or erased) board starts with

| Setting | Factory value | Effect |
|---|---|---|
| Web login | none | The page can be viewed, but **every change is refused** until a login is set |
| Ethernet | DHCP | The router picks the address; nobody knows it yet |
| Node ID | `fanController_xx` | **MQTT stays off** while it is this |
| Device name | from the node ID: `fancontroller-xx.local` | Follows the node ID (from the next restart or WiFi-tab save) |
| Fans | 1 | Fan 2 has to be ticked |
| Time zone | America/Chicago | |
| WiFi backup | none | |
| Setup hotspot | `FanController-XXXX`, password `12345678` | Starts after 60 s with no Ethernet or WiFi |

A board flashed over an older version of this firmware keeps its settings instead (they
upgrade in place); only a new or erased board starts from this table.

## Steps, in the order that causes the fewest reconnects

The Network tab is last because it is the only save that restarts the board and moves the page.
**Fill in one panel and press its Save before you type into the next one:** every Save reloads
all the forms from the board, which wipes anything typed but not yet saved in other panels.

1. **Connect and power.** Plug in the Ethernet cable before powering up. Use external power:
   the Touch-LCD-2 locks up on PC USB power with the LCD, Ethernet and SD running.
2. **Find the address.**
   - Touch-LCD-2: the IP is in the bottom-left corner of the LCD. Tap the gear icon for a QR
     code of the page.
   - Or open `http://fancontroller-xx.local` (most PCs and iPhones; many Android phones can't
     open .local names).
   - No Ethernet: after 60 s the board starts the hotspot. Join `FanController-XXXX`
     (password `12345678`) and open `http://192.168.4.1`. See the note below before using it.
3. **System tab > Web login:** set a user and password (8+ characters). You are logged in at
   once. Nothing else can be saved before this.
4. **Home Assistant tab > MQTT:** node ID (e.g. `fanController_02`), broker, port, MQTT
   user and password > Save. MQTT starts without a restart, and the Status panel should show
   "Connected". The device name will follow the new node ID (e.g. `fancontroller-02.local`),
   but only from the next restart or WiFi-tab save; until then the old name still works.
5. **Fan Control tab:** tick the fans that are connected, pick °F/°C, set the fan curve
   start/top > Save (one Save for the whole tab).
6. **WiFi tab** (optional):
   - Backup WiFi network (scan, pick, password) > Save.
   - WiFi address (DHCP or static) > Save.
   - Device name, if you don't want the one made from the node ID > Save.
   - Setup hotspot: change the `12345678` password > Save.

   Each panel has its own Save; nothing here restarts the board.
7. **Network tab:**
   - Time: time zone and 12/24 h > Save. No restart.
   - Ethernet address: DHCP or static IP > Save. **Only if it changes, the board restarts**
     and the page reopens itself at the new address after 10 s (by IP for static, by
     `.local` name for DHCP).
8. **Home Assistant (in HA):**
   - The device and its entities appear by themselves once MQTT is connected.
   - Add the network-temperature automation from `docs/MQTT.md`, with this board's node ID in
     the topic (`<nodeID>/network_temp/set`), or add a second `mqtt.publish` action to the
     existing automation.
   - Until that runs, the page shows "No value from HA" and HA reports "Fault network probe".
     Right after a restart that is normal for a minute or so.
9. **Check:** the Dashboard shows temperatures and fan RPM; the sidebar shows Ethernet, MQTT
   connected and the SD card; the LCD dots are green.

**Setting up over the hotspot:** it turns itself off 30 s after Ethernet or WiFi starts
working. The phone then loses the page, often in the middle of a step. Over the hotspot, set
only what gets the board onto a network (the login, then WiFi or the Ethernet address). Do the
rest from a PC on the normal network.

## Rough spots (why it feels disjointed)

Found while writing this down; candidates for `TODO.md`, nothing changed yet.

1. **Login first, on the last tab.** A new board refuses every Save until a login is set on
   the System tab, and only the login dialog says so, after the first refused Save. The page
   could open the login panel by itself while no login is set.
2. **Settings are spread over five tabs with ten separate Save buttons, and each Save wipes
   the others' unsaved fields** (after a save the page reloads every form from the board).
   Nothing warns about unsaved fields (the Notes box has a warning; the forms don't).
3. **The node ID quietly renames the device later.** The `.local` name follows the node ID,
   but only from the next restart or WiFi-tab save, so the name changes at an unexpected
   moment (for example when the Ethernet save restarts the board).
4. **The Ethernet change restarts the board and moves the page.** With DHCP the new address
   is unknown until the LCD shows it or `.local` resolves.
5. **The hotspot drops the phone** 30 s after a network works (see above), with no message
   on the page first.
6. **The Home Assistant side is manual:** the automation's topic has to be edited per board.

One way to fix most of it: a **setup page** shown while the board is new, with one form
(login, node ID and MQTT, fans, network) and one Save. The board would restart once at the
end, and the page would say where to find it next.
