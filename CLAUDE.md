# ETH_Touch_PWM

Two-channel 25 kHz PWM fan controller with Ethernet, touch LCD, DS18B20 probe, SD logging,
web config page and Home Assistant sync. PlatformIO port of the Arduino IDE sketch
`ESP32_S3_FanController_2inch` from
https://github.com/sergeant82d/ESP32S3-Ethernet-Fan-Controller (commit `c23de85`; `main.cpp`
is the touch-enabled version of the `.ino`). That repo stays as the Arduino reference.

## Working principles

- Don't assume. Don't hide confusion. Surface tradeoffs.
- Minimum code that solves the problem. Nothing speculative.
- Touch only what you must. Clean up only your own mess.
- Define success criteria. Loop until verified.

## Hardware

- Waveshare ESP32-S3-Touch-LCD-2: ST7789T3 240x320 LCD, CST816D touch, microSD,
  16 MB flash, OPI PSRAM, native USB (USB CDC on boot).
- External W5500 Ethernet on GPIO 9-14. All pins are in `src/pins.h`; keep them there.
- LCD and SD share one SPI bus. W5500 has its own.
- Power: the board locks up on PC USB power with LCD + Ethernet + SD running. Use external power.

## Build

- `~/.platformio/penv/Scripts/pio.exe run` (add `-t upload` to flash). Run it from PowerShell:
  pioarduino's tool installer refuses Git Bash ("MSys/Mingw is not supported").
- Platform is pioarduino 55.03.311 = Arduino-ESP32 core 3.3.11, matching the Arduino IDE build.
  Needs Windows long paths enabled.
- Partitions `app3M_fat9M_16MB.csv`: LittleFS lives on the `ffat` partition (see `config.cpp`).
  Changing the partition table wipes saved settings.
- Library versions in `platformio.ini` are pinned to the Arduino IDE ones. Change deliberately.

## Rules

- Secrets (HA tokens, passwords) never go in the source. The HA token is entered on the web
  page; anything else goes in git-ignored `secrets.h`.
- Mark anything not tested on the board as untested, in commit messages and docs.
- Commit and push when the user confirms a change works on the board.

## Conversion success criteria

1. Builds in PlatformIO.
2. Flashed, it behaves like the Arduino build: display, touch, Ethernet/web page, fan PWM
   and RPM, temperature, HA sync, SD logging.
3. Flash and RAM use are close to the Arduino build (Arduino IDE, 2026-09-27: sketch 549,279
   bytes of 3,145,728; globals 26,956 bytes).
