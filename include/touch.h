#ifndef TOUCH_H
#define TOUCH_H

#include <Arduino.h>

// Initializes I2C for the CST816D touch controller (shares the bus with
// the onboard IMU - see pins.h PIN_I2C_SDA/PIN_I2C_SCL).
void touchInit();

// Returns true if a finger is currently on the panel, with its coordinates
// already transformed into display space (matching the 320x240 landscape
// orientation used everywhere else in display.cpp). Level-triggered (true
// every call while held, not just on initial touch-down) - callers wanting
// tap/edge detection should track the previous call's result themselves.
//
// NOTE: the raw-to-display coordinate transform below is a best guess
// based on the panel's native portrait orientation and this project's
// setRotation(1) landscape rotation - it has not been verified against the
// physical panel yet. If touches land offset, mirrored, or swapped X/Y
// once tested, this is the function to adjust.
bool getTouchPoint(int &x, int &y);

#endif // TOUCH_H
