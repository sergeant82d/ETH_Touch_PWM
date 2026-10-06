#include "touch.h"
#include "pins.h"
#include <Wire.h>
#include "sd_logger.h"

// CST816D register map (standard CST8xx family layout - not chip-specific
// to this exact part number, but consistent across the family).
static const uint8_t CST816_ADDR       = 0x15;
static const uint8_t REG_GESTURE_ID    = 0x01;
static const uint8_t REG_FINGER_NUM    = 0x02;
static const uint8_t REG_XPOS_H        = 0x03; // top nibble unused here (event flag lives in upper 2 bits, ignored)
static const uint8_t REG_XPOS_L        = 0x04;
static const uint8_t REG_YPOS_H        = 0x05;
static const uint8_t REG_YPOS_L        = 0x06;

// Panel's native resolution as the touch controller reports it (portrait,
// pre-rotation) - matches the ST7789's PANEL_NATIVE_W/H in display.cpp.
static const int TOUCH_NATIVE_W = 240;
static const int TOUCH_NATIVE_H = 320;

void touchInit() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    pinMode(PIN_TP_INT, INPUT); // not used as an interrupt yet - polled instead
}

// DIAGNOSTIC (2026-10-04, touch "sporadic"): every finger down/up is logged
// with raw and mapped coordinates, and failed I2C reads are counted.
static bool logWasDown = false;
static unsigned long i2cFails = 0, lastFailReportMs = 0;

// Also to the event log (the boot-time failure only ever showed on the serial
// port, 2026-10-06): the first one, then at most one summary an hour
static void countI2cFail() {
    i2cFails++;
    if (millis() - lastFailReportMs >= 10000) {
        lastFailReportMs = millis();
        Serial.print("TOUCH: I2C read failures so far: "); Serial.println(i2cFails);
    }
    static unsigned long lastEventMs = 0;
    static unsigned long loggedFails = 0;
    if (loggedFails == 0 || millis() - lastEventMs >= 3600000UL) {
        lastEventMs = millis();
        sdLogEvent("TOUCH", "I2C read failed (" + String(i2cFails) + " since boot, uptime " + String(millis() / 1000) + " s)");
        loggedFails = i2cFails;
    }
}

bool getTouchPoint(int &x, int &y) {
    Wire.beginTransmission(CST816_ADDR);
    Wire.write(REG_GESTURE_ID);
    if (Wire.endTransmission(false) != 0) { countI2cFail(); return false; } // controller not responding

    const uint8_t bytesToRead = 6;
    if (Wire.requestFrom((int)CST816_ADDR, (int)bytesToRead) != bytesToRead) { countI2cFail(); return false; }

    uint8_t gesture   = Wire.read();
    uint8_t fingerNum = Wire.read();
    uint8_t xh        = Wire.read();
    uint8_t xl        = Wire.read();
    uint8_t yh        = Wire.read();
    uint8_t yl        = Wire.read();
    (void)gesture;

    if (fingerNum == 0) {
        if (logWasDown) { logWasDown = false; Serial.println("TOUCH: up"); }
        return false;
    }

    int rawX = ((xh & 0x0F) << 8) | xl;
    int rawY = ((yh & 0x0F) << 8) | yl;

    // Transform native portrait touch coords into the 320x240 landscape
    // space used by display.cpp (matches screenMain.setRotation(1)).
    // BEST GUESS - see the warning in touch.h. A rotation(1) on most
    // ST7789 modules maps landscape (dx,dy) <- portrait (native_h - rawY, rawX).
    x = TOUCH_NATIVE_H - rawY;
    y = rawX;

    if (!logWasDown) {
        logWasDown = true;
        Serial.printf("TOUCH: down raw=(%d,%d) -> screen (%d,%d) gesture=0x%02X\n", rawX, rawY, x, y, gesture);
    }
    return true;
}
