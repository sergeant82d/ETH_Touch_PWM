#ifndef PINS_H
#define PINS_H

// Changing a pin or adding a build: docs/BOARDS.md, "Changing pins or adding a board"
// (update its pin table too).

// Two boards, chosen by the PlatformIO environment:
//   waveshare_s3_lcd2 (default): Waveshare ESP32-S3-Touch-LCD-2 + external W5500
//   waveshare_s3_lcd2_lite (-DBOARD_LCD2_LITE): the same board + a W5500 Lite module, with
//     the user's pin choices (2026-09-29): Ethernet straight out of the right header,
//     fans and probe out of the left one. Only the Ethernet, fan and probe pins differ.
//   waveshare_s3_eth (-DBOARD_S3_ETH): Waveshare ESP32-S3-ETH (onboard W5500, no LCD),
//     the fan_controller_01 hardware; pins from its Arduino sketch
//     ESP32_S3_ETH_PWM_Fans_VER_1_0_1_WORKING_NO_LCD.ino (Arduino repo, 7411e66).

#if defined(BOARD_S3_ETH)

#define BOARD_NAME "ESP32-S3-ETH"
#define HAS_LCD 0                // no LCD, touch or IMU; display.cpp/touch.cpp not built
#define DEFAULT_IP_LAST_OCTET 53 // 192.168.10.53, as the Arduino build

// ===== W5500 Ethernet - onboard, hard-wired =====
#define W5500_MOSI  11
#define W5500_MISO  12
#define W5500_SCK   13
#define W5500_CS    14
#define W5500_INT   10
#define W5500_RST   9

// ===== MicroSD - onboard slot, own SPI bus =====
#define PIN_SD_CS       4
#define PIN_SD_MISO     5
#define PIN_SD_MOSI     6
#define PIN_SD_SCLK     7

// ===== Fans (PWM 25 kHz / tachometer) =====
#define PWM1_PIN    1
#define TACH1_PIN   2
#define PWM2_PIN    18
#define TACH2_PIN   40

// ===== Dallas OneWire (DS18B20) =====
#define ONEWIRE_PIN 21

#else // Waveshare ESP32-S3-Touch-LCD-2 (both builds)

#if defined(BOARD_LCD2_LITE)
#define BOARD_NAME "ESP32-S3-Touch-LCD-2 W5500 Lite" // own name: OTA refuses the other LCD build
#define DEFAULT_IP_LAST_OCTET 55 // 192.168.10.55
#else
#define BOARD_NAME "ESP32-S3-Touch-LCD-2"
#define DEFAULT_IP_LAST_OCTET 54 // 192.168.10.54
#endif
#define HAS_LCD 1

// ===== Board: Waveshare ESP32-S3-Touch-LCD-2 (240x320, ST7789T3 + CST816D) =====
// Pin map verified against the board's schematic (ESP32-S3-Touch-LCD-2-SchDoc).
// This board also breaks out a camera header; since this project has no
// camera attached, all camera-interface GPIOs below are free for reuse.

// ---- Reserved: do not reassign ----
#define PIN_UART0_TX    43
#define PIN_UART0_RX    44
#define PIN_USB_DM      19   // native USB D- (leave alone if using USB-CDC serial)
#define PIN_USB_DP      20   // native USB D+

// ---- LCD (ST7789T3, shares SPI bus with SD card) ----
#define PIN_LCD_RST     0    // also the BOOT strap pin - fine, released after boot
#define PIN_LCD_BL      1
#define PIN_LCD_MOSI    38   // shared with SD_MOSI
#define PIN_LCD_SCLK    39   // shared with SD_SCLK
#define PIN_LCD_DC      42
#define PIN_LCD_CS      45

// ---- MicroSD (shares SPI bus with LCD; separate CS) ----
#define PIN_SD_MOSI     PIN_LCD_MOSI
#define PIN_SD_SCLK     PIN_LCD_SCLK
#define PIN_SD_MISO     40
#define PIN_SD_CS       41

// ---- Touch panel (CST816D) + onboard IMU (QMI8658C), shared I2C bus ----
#define PIN_TP_INT      46
#define PIN_I2C_SCL     47   // TP_SCL / IMU_SCL
#define PIN_I2C_SDA     48   // TP_SDA / IMU_SDA
#define PIN_IMU_INT1    3

// ---- Other onboard functions ----
#define PIN_BAT_ADC     5    // battery voltage divider

#if defined(BOARD_LCD2_LITE)

// ===== W5500 Lite (SPI) - right header, board pins 7-12 in a row (GND pin 13, 3V3 pin 1) =====
#define W5500_MOSI  9    // board pin 12
#define W5500_SCK   14   // board pin 11
#define W5500_CS    12   // board pin 10
#define W5500_INT   11   // board pin 9
#define W5500_RST   13   // board pin 8
#define W5500_MISO  15   // board pin 7

// ===== Fans (PWM 25 kHz / tachometer) and DS18B20 - left header =====
#define PWM1_PIN    6
#define TACH1_PIN   16
#define PWM2_PIN    2
#define TACH2_PIN   4
#define ONEWIRE_PIN 17

// ===== Spare (free) pins =====
// GPIO 7, 8, 10, 18, 21 (left header) - e.g. fan channels 3/4. GPIO 19/20 on the same
// header are the USB port: leave them alone. 47/48 (right header) are the touch I2C.

#else // Touch-LCD-2 + original W5500 module

// ===== W5500 Ethernet (SPI) - external module, using free camera-header pins =====
#define W5500_SCK   12
#define W5500_MOSI  13
#define W5500_MISO  14
#define W5500_CS    11
#define W5500_INT   10
#define W5500_RST   9

// ===== PWM Outputs (25 kHz, use LEDC) =====
#define PWM1_PIN    2
#define PWM2_PIN    6

// ===== Input Capture / Interrupt Pins (fan tachometers) =====
#define TACH1_PIN   4
#define TACH2_PIN  16

// ===== Dallas OneWire (DS18B20) =====
#define ONEWIRE_PIN 8

// ===== Spare (free) pins for future expansion =====
// GPIO7, 15, 17, 18, 21 remain unused - e.g. fan channels 3/4.

#endif // W5500 module
#endif // board

// ===== Both boards =====
#define PWM_FREQ_HZ 25000
#define PWM_RESOLUTION_BITS 8   // adjust if finer duty resolution needed

// ===== Shared timing constant (display refresh / HA sync interval, ms) =====
#define REFRESH_PERIOD_MS 2000

#endif // PINS_H
