# LCD design notes (LVGL archive)

Written 2026-10-06. Ideas for the Touch-LCD-2 screen kept for later. The top-bar icons were
approved the same day (`images/icon_preview.png`) and are being built.

## Status

- **LVGL cancelled** (you, 2026-10-05). On 2026-10-04 we had agreed it was high effort and low
  impact, but pretty: worth it only after your design images, and then dashboard first,
  behind a build switch.
- The main practical reason for it went away on 2026-10-05: the flicker is fixed on the
  current library (Adafruit_GFX). Each part of the screen now redraws only when it changes
  (`5eec4c8`).
- The top-bar icons below don't need LVGL. They are 1-bit bitmaps, drawn the same way as
  today's gear icon (`drawBitmap()`), so they can be added on their own.

## Today's screen (for reference)

320 x 240 landscape, ST7789T3; layout in `src/display.cpp`, mirrored by the web page's LCD
view.

- **Title bar** (blue, 26 px): three status dots on the left (network, MQTT, SD card), the
  controller name in the centre, the gear (QR page) on the right.
- **Below it:**
  - fan RPM bars on the left, network and local temperature bars on the right
  - clock, big blended temperature and the Manual Control button in the middle
  - IP address and date in the footer
- `images/LCD-Error-Labels.jpg`: the web LCD view with both probes failed ("CRIT!",
  "--" over red bars).

## Your mock-ups (`images/`)

Six AI-generated images in three styles. They show what you like, not exact layouts.

| Style | Images | What it shows | On this board |
|---|---|---|---|
| Glossy sci-fi | `Gemini_..._1e57k51e57k51e57.jpg` (fan pictures), `..._78kzgd78kzgd78kz.jpg` (chevron bars), `..._ggndepggndepggnd.jpg` (fans and thermometers) | Cyan frames, glow; PWR / NET / CLD icons with labels; date and time in one framed box; "[ STATUS ]" footer; thermometers with scales | The glow, gradients and fan pictures need images in flash and LVGL-style blending. The heaviest option |
| Flat | `..._g4ug9ng4ug9ng4ug.jpg` | Today's layout, cleaned up: rounded panels, PWR / NET / CLD dots with labels, bar gauges with a value beside them, IP and date in the footer | Closest to what we have; possible on the current library |
| Steampunk | `..._umjh51umjh51umjh.jpg`, `..._vez6e9vez6e9vez6.jpg` | Brass panel, dial gauges with needles, nixie-tube clock, odometer RPM counters, toggle switch | Almost all pictures. A full-screen 320 x 240 picture is ~150 KB of flash (RGB565), and the needles and digits would be drawn on top |

## Top bar: your design

- **Left of the name:**
  - Ethernet, WiFi and hotspot icons, replacing the status dots
  - the one in use is coloured, the others greyed out
  - the hotspot flashes while it is on, perhaps on an orange background
- **Right of the name:** MQTT and SD card icons, same rule (grey when inactive or off,
  coloured when working).
- **Gear:** stays at the far right.

Proposed states (to agree):

| Icon | Grey | Coloured | Flashing |
|---|---|---|---|
| Ethernet | no link | green: in use | (none) |
| WiFi | off (backup not needed) | green: in use | slow: joining |
| Hotspot | off | (none) | orange background: on |
| MQTT | off (not set up) | green: connected | orange: set up but not connected |
| SD card | (none) | green: OK | slow orange: getting full; fast red: missing (as today) |

Fits and limits:

- **Space:**
  - 16 x 16 icons fit the 26 px bar with 5 px above and below.
  - Three on the left take ~60 px, two on the right plus the gear ~70 px, which leaves
    ~180 px for the name.
  - At today's text size (12 px per letter) that's about 14 letters: "6U Rack Fans" fits,
    "FanController 02" (16) runs into the icons (see the preview). Longer names would need the small font or a
    shortened name.
- **Icons:** approved 2026-10-06, `images/icon_preview.png` (the five icons enlarged, and the
  real 320 x 26 bar in four states with the LCD's own font and gear). Drawn by Claude:
  Ethernet = RJ45 port, WiFi = three arcs and a dot, hotspot = mast with rings, MQTT = house
  (Home Assistant) with a node, SD card = card with cut corner. The bitmaps are in
  `src/display.cpp`. Earlier AI-generated ideas, kept for reference:
  - `images/network_icons_1` has two sets of 16 x 16 grids: WiFi, Ethernet, hotspot,
    Bluetooth, SD card.
  - Some of its C arrays don't match their own grids (e.g. the second WiFi set: its 4th and 5th rows), so
    make the arrays from the grids, not from those arrays.
- **Taps:** the panel reads taps near the top edge 30-60 px low (2026-10-04), so the icons
  can't be buttons; the gear's touch area already covers the whole top-right corner.

## Display conventions (short discussion)

- **Indicator colours:**
  - The common standard for indicator lights (IEC 60073) is:
    - red = fault or danger
    - yellow/amber = abnormal or warning
    - green = normal
    - blue = action needed
    - white = neutral
  - Our dots and the SD card states already follow it. Keep red for real faults (probe
    failed, SD missing, fan stopped) so it keeps its meaning.
- **Industrial screens** (ISA-101, "high-performance HMI"):
  - Grey for everything normal, colour only for what's wrong, so a problem stands out at a
    glance.
  - Home and network gear (routers, switches) do the opposite: lit = working, dark = off.
  - Your design (coloured = in use, grey = not) is the network-gear convention. It suits a
    status bar people glance at, and it matches the web page's sidebar lights.
- **Flashing:**
  - Use it for things that want attention: a fault, or a temporary mode such as the
    hotspot.
  - Slow = warning, fast = fault, as the SD dot does today.
  - Don't flash normal states. A flashing hotspot fits: it's a setup mode, and it should
    end.
- **Never colour alone:**
  - About 1 in 12 men can't tell red from green reliably.
  - Icons help here: the shape says which link it is, and grey vs coloured says its state.
    That is an advantage over the plain dots.
  - Keep a second cue for faults (flashing, or a text label such as "CRIT!").
- **Standard shapes:**
  - WiFi arcs, an Ethernet plug or port, and a hotspot with radiating rings are widely
    recognised.
  - The words in the mock-ups (PWR, NET, CLD) help too, but at 26 px they'd only fit as tiny
    text below the icons.

## If this is picked up again

1. **Top-bar icons on the current library (small job):**
   - Make the bitmaps from the grids.
   - Draw them where the dots are, with the same "redraw only on change" caching.
   - Mirror them in the web page's LCD view.
2. **The flat mock-up (medium):**
   - Rounded panels, values beside the bars, clock and date together.
   - Possible without LVGL.
3. **Glossy or steampunk (large):** LVGL or full-screen images, dashboard first, behind a
   build switch (the 2026-10-04 plan). Check flash use: the app is ~1.47 MB of 3 MB today.
