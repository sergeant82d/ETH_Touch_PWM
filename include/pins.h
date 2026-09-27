#ifndef PINS_H
#define PINS_H

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
#define PWM_FREQ_HZ 25000
#define PWM_RESOLUTION_BITS 8   // adjust if finer duty resolution needed

// ===== Input Capture / Interrupt Pins (fan tachometers) =====
#define TACH1_PIN   4
#define TACH2_PIN  16

// ===== Dallas OneWire (DS18B20) =====
#define ONEWIRE_PIN 8

// ===== Spare (free) pins for future expansion =====
// GPIO7, 15, 17, 18, 21 remain unused - e.g. fan channels 3/4.

// ===== Shared timing constant (display refresh / HA sync interval, ms) =====
#define REFRESH_PERIOD_MS 2000

#endif // PINS_H
