#include "touch.h"
#include "pins.h"
#include <Wire.h>

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

bool getTouchPoint(int &x, int &y) {
    Wire.beginTransmission(CST816_ADDR);
    Wire.write(REG_GESTURE_ID);
    if (Wire.endTransmission(false) != 0) return false; // controller not responding

    const uint8_t bytesToRead = 6;
    if (Wire.requestFrom((int)CST816_ADDR, (int)bytesToRead) != bytesToRead) return false;

    uint8_t gesture   = Wire.read();
    uint8_t fingerNum = Wire.read();
    uint8_t xh        = Wire.read();
    uint8_t xl        = Wire.read();
    uint8_t yh        = Wire.read();
    uint8_t yl        = Wire.read();
    (void)gesture;

    if (fingerNum == 0) return false;

    int rawX = ((xh & 0x0F) << 8) | xl;
    int rawY = ((yh & 0x0F) << 8) | yl;

    // Transform native portrait touch coords into the 320x240 landscape
    // space used by display.cpp (matches screenMain.setRotation(1)).
    // BEST GUESS - see the warning in touch.h. A rotation(1) on most
    // ST7789 modules maps landscape (dx,dy) <- portrait (native_h - rawY, rawX).
    x = TOUCH_NATIVE_H - rawY;
    y = rawX;

    return true;
}
