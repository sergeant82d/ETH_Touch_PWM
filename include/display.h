#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <SPI.h>

// Returns the SPI bus shared between the LCD and the SD card (same
// MOSI/SCLK, different CS - see pins.h). Any other module talking to
// hardware on this bus (currently just sd_logger.cpp) must use this
// same instance, not open a second independent one.
SPIClass& getDisplaySPI();

// Brings up the onboard LCD and draws the static frame (title bar).
void displayInit();

// Redraws the dynamic parts of the dashboard: clock, blended average temp,
// per-fan RPM text, and the fan-RPM / probe-temperature bar gauges that
// used to live on the separate left/right panel displays.
void updateMainDashboardUI();

// Redraws just the bar gauges (not the rest of the dashboard). Call this
// on a fast, independent timer (e.g. every 150-250ms) - separate from
// updateMainDashboardUI()'s 2s cadence - so the near-limit flash actually
// reads as a flash instead of a slow color change.
void refreshBarsOnly();

// Polls the touch panel and handles all on-screen touch interaction
// (Manual Control button, the override overlay's slider/buttons). Call
// every loop() iteration - internally rate-limited, cheap to call often.
// When the override overlay is open, this owns redrawing it; the normal
// dashboard/bar redraws are automatically skipped while it's open (no
// framebuffer yet, so a full-screen takeover avoids fighting the other
// redraw timers over the same pixels).
void handleTouchInput();

// True while the override overlay is on screen - updateMainDashboardUI()
// and refreshBarsOnly() no-op while this is true, so the caller (main.ino)
// doesn't need its own awareness of overlay state.
bool isOverlayOpen();

#endif // DISPLAY_H
