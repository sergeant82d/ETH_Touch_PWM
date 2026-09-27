#include "display.h"
#include "pins.h"
#include "config.h"
#include "sensors.h"
#include "touch.h"
#include "home_assistant.h"
#include "sd_logger.h"
#include "network.h"
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#include <Ethernet.h>
#include <TimeLib.h> // same clock source as web_server.cpp - see note in updateMainDashboardUI()
#include <math.h>
#include <Fonts/FreeSansBold24pt7b.h> // bundled with Adafruit_GFX - smoother/proportional, used for the big temp number
#include <Fonts/FreeSansBold18pt7b.h> // smaller sibling, same family - used for the clock/F-C/button (a real size step down from the temp number)

#ifndef ST77XX_DARKGRAY
#define ST77XX_DARKGRAY 0x7BEF
#endif

// Panel: 240x320 native (portrait) - rotated to 320x240 landscape below.
static const int PANEL_NATIVE_W = 240;
static const int PANEL_NATIVE_H = 320;
static const int LCD_WIDTH  = 320;  // landscape: width/height swapped after rotation
static const int LCD_HEIGHT = 240;

// Dedicated SPI bus for the onboard LCD, separate from the W5500 SPI bus.
// Note: this bus is physically shared with the SD card (same MOSI/SCLK,
// different CS) - only one device should be selected/active at a time.
static SPIClass LcdSPI(HSPI);
static Adafruit_ST7789 screenMain(&LcdSPI, PIN_LCD_CS, PIN_LCD_DC, PIN_LCD_RST);

// LCD and the SD card physically share this SPI bus (same MOSI/SCLK,
// different CS - see pins.h). sd_logger.cpp needs the same SPIClass
// instance, not a second independent one, to avoid bus contention.
SPIClass& getDisplaySPI() { return LcdSPI; }

// Layout (320x240 landscape): tall bar gauges on the left (fans) and right
// (probes, mirrored), text readouts in the center column.
static const int TITLE_H     = 26;
static const int LEFT_ZONE_X = 4;
static const int LEFT_ZONE_W = 88;
static const int RIGHT_ZONE_W = 88;
static const int RIGHT_ZONE_X = LCD_WIDTH - RIGHT_ZONE_W - 4;
static const int CENTER_X    = LEFT_ZONE_X + LEFT_ZONE_W + 8;
static const int CENTER_W    = RIGHT_ZONE_X - 8 - CENTER_X;

// Row above each bar column showing its current value(s), and a footer row
// below the (now shorter) bars showing plain text - IP under the Fan RPM
// column, date under the Temperature column. Raising the bar floor to make
// room for both was the whole point of this layout pass.
static const int VALUE_ROW_Y = TITLE_H + 14;
static const int FOOTER_Y    = LCD_HEIGHT - 14;

static void drawFanRpmBars(int x0, int y0, int zoneWidth, int zoneHeight);
static void drawTempProbeBars(int x0, int y0, int zoneWidth, int zoneHeight);

// Vertical extent of the bar-gauge zones, shared between updateMainDashboardUI()
// (full redraw, 2s cadence) and refreshBarsOnly() (fast redraw, for flashing).
static const int BARS_TOP = TITLE_H + 26;
static int barsHeight() { return (FOOTER_Y - 6) - BARS_TOP; }

// True while the Manual Control override overlay is showing full-screen.
// See handleTouchInput() for the state machine; updateMainDashboardUI()
// and refreshBarsOnly() no-op while this is true.
static bool overlayOpen = false;

// Gauge scales now live in config (config.fanRpmGaugeMin/Max, config.tempGaugeMinF/MaxF)
// so they're adjustable from the LCD settings menu, web page, and Home Assistant
// rather than fixed at compile time.

// Bar geometry, shared between the gauge-drawing functions and the column
// header labels so the labels center over the actual bars, not the zone.
static const int BAR_WIDTH   = 28;
static const int BAR_SPACING = 20;
static const int BAR_SPAN_W  = BAR_WIDTH * 2 + BAR_SPACING; // total width of the 2-bar cluster

// Settings gear icon (Lucide "settings", 24x24, 1-bit), drawn top-right of the title bar.
static const int ICON_SETTINGS_SIZE = 24;
static const unsigned char PROGMEM icon_settings_24x24[] = {
    0x00, 0x00, 0x00,
    0x00, 0x18, 0x00,
    0x00, 0x18, 0x00,
    0x03, 0x7E, 0xC0,
    0x03, 0xFF, 0xC0,
    0x03, 0xC1, 0xC0,
    0x1F, 0xC0, 0xF8,
    0x1E, 0xE0, 0x78,
    0x0C, 0x60, 0x30,
    0x18, 0x7C, 0x18,
    0x18, 0x7E, 0x18,
    0x78, 0x67, 0xFE,
    0x78, 0x67, 0xFE,
    0x18, 0x7E, 0x18,
    0x18, 0x7C, 0x18,
    0x0C, 0x60, 0x30,
    0x1E, 0xE0, 0x78,
    0x1F, 0xC0, 0xF8,
    0x03, 0xC1, 0xC0,
    0x03, 0xFF, 0xC0,
    0x03, 0x7E, 0xC0,
    0x00, 0x18, 0x00,
    0x00, 0x18, 0x00,
    0x00, 0x00, 0x00,
};

// Flash timing for near-limit gauge bars (independent of the 2s dashboard
// refresh - see refreshBarsOnly()).
static const unsigned long BLINK_PERIOD_MS = 500; // ~1Hz flash
static bool blinkPhaseOn() { return (millis() % BLINK_PERIOD_MS) < (BLINK_PERIOD_MS / 2); }

// Bar alert thresholds, as a fraction of the gauge's configured range.
// Firmware-defined, not user-configurable.
static const float BAR_FLASH_THRESHOLD = 0.90; // starts flashing in its normal (identity) color
static const float BAR_RED_THRESHOLD   = 0.95; // flashes red instead

// Decides the fill color and whether this bar should be flashing right now,
// given its identity color (e.g. cyan for fans, orange/magenta for probes).
// Returns true if the fill should be drawn this instant (false = blank/off
// phase of a flash, so the caller skips drawing to create the blink).
static bool resolveBarFill(float value, float minV, float maxV, uint16_t identityColor, uint16_t &outColor) {
    if (maxV <= minV) { outColor = identityColor; return true; }

    float pct = (value - minV) / (maxV - minV);
    if (pct < 0.0) pct = 0.0;

    if (pct >= BAR_RED_THRESHOLD) {
        outColor = ST77XX_RED;
        return blinkPhaseOn();
    }
    if (pct >= BAR_FLASH_THRESHOLD) {
        outColor = identityColor;
        return blinkPhaseOn();
    }
    outColor = identityColor;
    return true;
}

// Small inset between the outline and the fill (keeps fill from touching
// the outline stroke). Outer bar footprint is unchanged either way.
static const int BAR_OUTLINE_INSET = 2;
static const int BAR_CORNER_RADIUS = 10; // rounded pill-style outline

// Draws one gauge bar: thin rounded outline (identityColor), black interior,
// and - if hasValue is true - a single-color fill (identityColor normally;
// flashes per resolveBarFill() near/at the configured max). Fill is drawn
// with a small rounded radius of its own (clamped to fit) so it never has
// square corners poking past the bar's own rounded outline.
static void drawGaugeBar(int x, int y, int w, int h, uint16_t identityColor, bool hasValue, float value, float minV, float maxV) {
    screenMain.fillRoundRect(x, y, w, h, BAR_CORNER_RADIUS, ST77XX_BLACK);

    int innerX = x + BAR_OUTLINE_INSET;
    int innerY = y + BAR_OUTLINE_INSET;
    int innerW = w - 2 * BAR_OUTLINE_INSET;
    int innerH = h - 2 * BAR_OUTLINE_INSET;

    if (hasValue) {
        uint16_t fillColor;
        bool drawFillNow = resolveBarFill(value, minV, maxV, identityColor, fillColor);
        if (drawFillNow) { // false = blink "off" phase - leave interior black
            float clampedVal = constrain(value, minV, maxV);
            int fillH = (maxV > minV) ? (int)map((long)(clampedVal * 100), (long)(minV * 100), (long)(maxV * 100), 0, innerH) : 0;
            if (fillH > 0) {
                // Rounded, not square, corners on the fill itself - a plain
                // fillRect has sharp corners that poke past the bar's
                // rounded outline right at the edges (redrawing the outline
                // on top only masks a 1px stroke, not the whole notch).
                // Radius is clamped to half the fill's own height so this
                // stays safe even when the fill is very short.
                int fillRadius = min(6, fillH / 2);
                screenMain.fillRoundRect(innerX, innerY + (innerH - fillH), innerW, fillH, fillRadius, fillColor);
            }
        }
    }

    // Outline drawn LAST, on top of the fill - its rounded corners then
    // correctly mask the fill's square corners underneath (the fill is a
    // plain rectangle even though the bar is rounded), instead of the
    // fill's corners visually poking past the curve like they did when
    // the outline was drawn first and the fill painted over it.
    screenMain.drawRoundRect(x, y, w, h, BAR_CORNER_RADIUS, identityColor);
}

// Prints text centered within [x0, x0+w) at the given baseline y, at the given text size.
static void printCentered(int x0, int w, int y, const String &text, uint8_t size = 1) {
    int16_t bx, by;
    uint16_t bw, bh;
    screenMain.setTextSize(size);
    screenMain.getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
    screenMain.setCursor(x0 + (w - (int)bw) / 2, y);
    screenMain.print(text);
}

// Attempts to render `text` centered within [x0,x0+w) x [y0,y0+h) using the
// bundled smoother font (FreeSansBold24pt7b @ scale 1) - measured for real
// via getTextBounds() rather than assumed, since exact glyph metrics are
// only known once compiled against the real font data. Draws and returns
// true if it fits; leaves the screen untouched and returns false (caller
// should fall back to the classic font) if it doesn't. Always reverts to
// the classic font before returning either way.
static bool tryDrawWithBigFont(const String &text, int x0, int w, int y0, int h, uint16_t color, uint16_t bgColor = ST77XX_BLACK, const GFXfont *font = &FreeSansBold18pt7b) {
    screenMain.setFont(font);
    screenMain.setTextSize(1);
    int16_t bx, by;
    uint16_t bw, bh;
    screenMain.getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);

    if (bw > (uint16_t)(w - 4) || bh > (uint16_t)(h - 2)) {
        screenMain.setFont(NULL);
        return false;
    }

    screenMain.setTextColor(color, bgColor);
    int textX = x0 + (w - (int)bw) / 2 - bx;
    int textY = y0 + (h - (int)bh) / 2 - by;
    screenMain.setCursor(textX, textY);
    screenMain.print(text);
    screenMain.setFont(NULL);
    return true;
}

// Manual Control button: top edge stays fixed where it's always been; the
// bottom now extends down to near the screen edge (a small margin kept,
// matching the margin used elsewhere on this screen) rather than being a
// short strip - per request, tall enough for a two-line, larger label.
static const int MANUAL_BTN_Y = 150;
static const int MANUAL_BTN_BOTTOM_MARGIN = 6;
static const int MANUAL_BTN_H = LCD_HEIGHT - MANUAL_BTN_BOTTOM_MARGIN - MANUAL_BTN_Y;

// Draws the Manual Control button: red outline when idle, flashing solid
// red/black when override is active. Now three rows: the temperature unit
// indicator on top (moved in here from its own row above, freeing that
// space back to the big temp number), then "Manual" and "Control" below.
static void drawManualControlButton(int x, int y, int w, int h) {
    const int radius = 4;
    uint16_t fg, bg;
    if (!manualOverrideActive) {
        bg = ST77XX_BLACK;
        fg = ST77XX_RED;
    } else {
        bool on = blinkPhaseOn();
        bg = on ? ST77XX_RED : ST77XX_BLACK;
        fg = on ? ST77XX_BLACK : ST77XX_RED;
    }
    screenMain.fillRoundRect(x, y, w, h, radius, bg);
    screenMain.drawRoundRect(x, y, w, h, radius, ST77XX_RED);

    // Unit indicator gets its own generous, larger row (24pt font) rather
    // than a rigid equal third - Manual/Control share whatever's left.
    // Kept modest (not larger) so as not to eat into Manual/Control's
    // already-confirmed-good sizing below.
    int unitRowH = 38;

    // --- Row 1: temperature unit indicator ---
    // Colored the same as the big temp number (red above tMax-5, green
    // otherwise) - since that can itself be red, this row keeps its own
    // black background band regardless of the button's flash state, so
    // the letter never disappears against a red flashing button.
    uint16_t tempColor = (blendedAverageC > (config.tMax - 5.0)) ? ST77XX_RED : ST77XX_GREEN;
    screenMain.fillRect(x + 2, y + 2, w - 4, unitRowH - 2, ST77XX_BLACK);
    String unitLetter = config.isFahrenheit ? "F" : "C";

    screenMain.setFont(&FreeSansBold24pt7b);
    screenMain.setTextSize(1);
    int16_t bx, by;
    uint16_t bw, bh;
    screenMain.getTextBounds(unitLetter, 0, 0, &bx, &by, &bw, &bh);
    bool bigFits = (bw <= (uint16_t)(w - 20)) && (bh <= (uint16_t)(unitRowH - 6));

    int circleR = bigFits ? 5 : 3;
    int gap = 3;
    int letterW = bigFits ? (int)bw : 10;
    int totalW = circleR * 2 + gap + letterW;
    int startX = x + (w - totalW) / 2;
    int circleX = startX + circleR;
    int circleY = y + circleR + 3;
    screenMain.drawCircle(circleX, circleY, circleR, tempColor);

    int letterX = circleX + circleR + gap;
    if (bigFits) {
        screenMain.setTextColor(tempColor, ST77XX_BLACK);
        int letterY = y + (unitRowH - (int)bh) / 2 - by;
        screenMain.setCursor(letterX - bx, letterY);
        screenMain.print(unitLetter);
        screenMain.setFont(NULL);
    } else {
        // 24pt didn't fit in this row - fall back to the smaller 18pt
        // sibling rather than the classic font. Bug fix: font must stay
        // set to 18pt through the actual print call - it was being reset
        // to NULL right before printing, which silently rendered the tiny
        // classic font instead of the intended 18pt fallback.
        screenMain.setFont(&FreeSansBold18pt7b);
        screenMain.getTextBounds(unitLetter, 0, 0, &bx, &by, &bw, &bh);
        screenMain.setTextColor(tempColor, ST77XX_BLACK);
        int letterY = y + (unitRowH - (int)bh) / 2 - by;
        screenMain.setCursor(letterX - bx, letterY);
        screenMain.print(unitLetter);
        screenMain.setFont(NULL);
    }

    // --- Rows 2-3: Manual / Control ---
    // Try the bundled smoother font for both; if either doesn't fit, fall
    // back to the classic font for both (never mix - consistent either way).
    int labelTop = y + unitRowH;
    int labelH = h - unitRowH;
    int lineZoneH = labelH / 2;
    bool topOk = tryDrawWithBigFont("Manual", x, w, labelTop, lineZoneH, fg, bg);
    bool botOk = topOk && tryDrawWithBigFont("Control", x, w, labelTop + lineZoneH, lineZoneH, fg, bg);

    if (!topOk || !botOk) {
        screenMain.fillRect(x + 2, labelTop, w - 4, labelH - 2, bg); // wipe any partial big-font attempt
        screenMain.setTextColor(fg, bg);
        const int lineH = 16, lineGap = 4;
        int blockTop = labelTop + (labelH - (lineH * 2 + lineGap)) / 2;
        printCentered(x, w, blockTop, "Manual", 2);
        printCentered(x, w, blockTop + lineH + lineGap, "Control", 2);
    }
}

// Converts a snake_case node ID (e.g. "fan_controller_01") into Title Case
// with spaces (e.g. "Fan Controller 01") for display in the title bar.
static String titleCaseFromNodeId(const char* nodeId) {
    String s(nodeId);
    s.replace('_', ' ');
    bool startOfWord = true;
    for (size_t i = 0; i < s.length(); i++) {
        if (s[i] == ' ') { startOfWord = true; continue; }
        s[i] = startOfWord ? toupper(s[i]) : tolower(s[i]);
        startOfWord = false;
    }
    return s;
}

// Redraws the blue title bar with the current Home Assistant node name
// and the settings gear icon (top-right).
static void drawTitleBar() {
    screenMain.fillRect(0, 0, LCD_WIDTH, TITLE_H, ST77XX_BLUE);
    screenMain.setTextColor(ST77XX_WHITE);
    printCentered(0, LCD_WIDTH, 5, titleCaseFromNodeId(config.nodeID), 2);

    // SD card status - small dot, top-left (mirrors the settings gear on
    // the top-right). Green = card present; red = absent, logging is
    // spilling to internal flash until it returns.
    uint16_t sdColor = isSdCardPresent() ? ST77XX_GREEN : ST77XX_RED;
    screenMain.fillCircle(10, TITLE_H / 2, 4, sdColor);

    // Network status - second dot, right next to the SD one. Green if any
    // network path is up (currently just Ethernet - see isNetworkConnected()
    // for why), red if none are.
    uint16_t netColor = isNetworkConnected() ? ST77XX_GREEN : ST77XX_RED;
    screenMain.fillCircle(22, TITLE_H / 2, 4, netColor);

    int iconX = LCD_WIDTH - ICON_SETTINGS_SIZE - 6;
    int iconY = (TITLE_H - ICON_SETTINGS_SIZE) / 2;
    screenMain.drawBitmap(iconX, iconY, icon_settings_24x24, ICON_SETTINGS_SIZE, ICON_SETTINGS_SIZE, ST77XX_WHITE);
}

// Static labels for the bar gauge zones, centered over the actual bar span
// (not the full zone) so they line up with the bars drawn below. Called
// both at init and every dashboard redraw - the latter matters because the
// override overlay does a full-screen wipe, and this makes the dashboard
// self-healing on close rather than needing special-case redraw logic there.
static const int LABEL_X_NUDGE = 6; // one letter-width at size 1 - fixes a slight left-offset look

static void drawColumnHeaders() {
    screenMain.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    printCentered(LEFT_ZONE_X + LABEL_X_NUDGE, BAR_SPAN_W, TITLE_H + 4, "Fan RPM");
    printCentered(RIGHT_ZONE_X + LABEL_X_NUDGE, BAR_SPAN_W, TITLE_H + 4, "Temperature");
}

void displayInit() {
    pinMode(PIN_LCD_BL, OUTPUT);
    digitalWrite(PIN_LCD_BL, HIGH);

    // MISO (PIN_SD_MISO) is wired even though the LCD itself never reads
    // data back - the SD card shares this bus and needs it. Omitting it
    // here would leave the bus's MISO line unconfigured, and SD reads
    // would silently fail even though writes might appear to work.
    LcdSPI.begin(PIN_LCD_SCLK, PIN_SD_MISO, PIN_LCD_MOSI, -1);
    delay(20);

    screenMain.init(PANEL_NATIVE_W, PANEL_NATIVE_H);
    screenMain.setRotation(1); // landscape: 320 wide x 240 tall
    screenMain.fillScreen(ST77XX_BLACK);

    drawTitleBar();
    drawColumnHeaders();
}

void updateMainDashboardUI() {
    if (overlayOpen) return; // overlay owns the screen while open - see handleTouchInput()

    int cy = TITLE_H + 6;

    drawTitleBar();
    drawColumnHeaders();

    // --- Time line, enlarged - tries the bundled smoother font first ---
    char tStr[16];
    bool haveTime = (timeStatus() != timeNotSet);
    if (haveTime) {
        if (config.is24Hour) {
            snprintf(tStr, sizeof(tStr), "%02d:%02d", hour(), minute());
        } else {
            snprintf(tStr, sizeof(tStr), "%d:%02d %s", hourFormat12(), minute(), isAM() ? "AM" : "PM");
        }
    } else {
        strncpy(tStr, "syncing...", sizeof(tStr));
    }

    // Fixed zone height (rather than sizing the zone to whichever font/size
    // ends up rendering) keeps everything below it at a consistent position
    // regardless of which fallback path this or the unit indicator take.
    const int TIME_ZONE_H = 40;
    screenMain.fillRect(CENTER_X, cy, CENTER_W, TIME_ZONE_H, ST77XX_BLACK);

    if (!tryDrawWithBigFont(tStr, CENTER_X, CENTER_W, cy, TIME_ZONE_H, ST77XX_YELLOW)) {
        // Bundled font didn't fit - classic font cascade (size 3, then 2),
        // same as before this change.
        uint8_t timeSize = 3;
        int16_t tbx, tby;
        uint16_t tbw, tbh;
        screenMain.setTextSize(timeSize);
        screenMain.getTextBounds(tStr, 0, 0, &tbx, &tby, &tbw, &tbh);
        if (tbw > (uint16_t)(CENTER_W - 4)) {
            timeSize = 2;
            screenMain.setTextSize(timeSize);
            screenMain.getTextBounds(tStr, 0, 0, &tbx, &tby, &tbw, &tbh);
        }
        screenMain.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
        int centeredY = cy + (TIME_ZONE_H - (int)tbh) / 2;
        printCentered(CENTER_X, CENTER_W, centeredY, tStr, timeSize);
    }
    cy += TIME_ZONE_H;

    // --- Big blended-average temperature ---
    // Vertically centered in the space between the rows above and the
    // Manual Control button below. Tries the bundled smoother font at 2x
    // scale first, then 1x, then falls back to the classic font at
    // decreasing sizes - each attempt measured for real via
    // getTextBounds() and only used if it genuinely fits, so this
    // maximizes size without risking overflow regardless of exactly how
    // large the real compiled glyphs turn out to be.
    int tempAreaTop = cy;
    int tempAreaHeight = MANUAL_BTN_Y - tempAreaTop;
    screenMain.fillRect(CENTER_X, tempAreaTop, CENTER_W, tempAreaHeight, ST77XX_BLACK);

    if (!localSensorHealthy && !networkSensorHealthy) {
        screenMain.setTextColor(ST77XX_RED, ST77XX_BLACK);
        int critY = tempAreaTop + (tempAreaHeight - 24) / 2; // ~24px tall at size 3
        printCentered(CENTER_X, CENTER_W, critY, "CRIT!", 3);
    } else {
        float dispAvg = config.isFahrenheit ? ((blendedAverageC * 9.0 / 5.0) + 32.0) : blendedAverageC;
        uint16_t tempColor = (blendedAverageC > (config.tMax - 5.0)) ? ST77XX_RED : ST77XX_GREEN;
        String numStr = String((int)round(dispAvg)); // integer only, no decimal

        int16_t bx, by;
        uint16_t bw, bh;
        bool placed = false;

        // Attempt 1: bundled font at 2x scale - the largest option.
        screenMain.setFont(&FreeSansBold24pt7b);
        screenMain.setTextColor(tempColor, ST77XX_BLACK);
        for (uint8_t scale = 2; scale >= 1 && !placed; scale--) {
            screenMain.setTextSize(scale);
            screenMain.getTextBounds(numStr, 0, 0, &bx, &by, &bw, &bh);
            if (bw <= (uint16_t)(CENTER_W - 8) && bh <= (uint16_t)(tempAreaHeight - 4)) {
                int textX = CENTER_X + (CENTER_W - (int)bw) / 2 - bx;
                int textY = tempAreaTop + (tempAreaHeight - (int)bh) / 2 - by;
                screenMain.setCursor(textX, textY);
                screenMain.print(numStr);
                placed = true;
            }
        }

        if (!placed) {
            // Neither custom-font scale fit - fall back to the classic
            // font, trying the largest size known to be safe first.
            screenMain.setFont(NULL);
            uint8_t classicSize = 8;
            screenMain.setTextSize(classicSize);
            screenMain.getTextBounds(numStr, 0, 0, &bx, &by, &bw, &bh);
            if (bw > (uint16_t)(CENTER_W - 8) || bh > (uint16_t)(tempAreaHeight - 4)) {
                classicSize = 6;
            }
            int classicY = tempAreaTop + (tempAreaHeight - 8 * classicSize) / 2;
            printCentered(CENTER_X, CENTER_W, classicY, numStr, classicSize);
        }
        screenMain.setFont(NULL); // always revert - everything else uses the classic font
    }

    // --- Manual Control button ---
    // The Fan 1/Fan 2/Local/Net/Average label-value rows that used to live
    // here are gone - that data's already shown via the bar-top value
    // labels and the big average number above, so repeating it as text
    // rows was redundant. Freed space means the button gets a roomier,
    // better-centered position instead of hugging the old row stack.
    drawManualControlButton(CENTER_X, MANUAL_BTN_Y, CENTER_W, MANUAL_BTN_H);

    // --- Tall bar gauges: fans on the left, probes on the right (mirrored) ---
    drawFanRpmBars(LEFT_ZONE_X, BARS_TOP, LEFT_ZONE_W, barsHeight());
    drawTempProbeBars(RIGHT_ZONE_X, BARS_TOP, RIGHT_ZONE_W, barsHeight());

    // --- Footer text: IP under Fan RPM column, date under Temperature column ---
    // Plain text, no labels, per spec.
    screenMain.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
    screenMain.fillRect(LEFT_ZONE_X, FOOTER_Y, LEFT_ZONE_W, 10, ST77XX_BLACK);
    printCentered(LEFT_ZONE_X, LEFT_ZONE_W, FOOTER_Y, Ethernet.localIP().toString(), 1);

    screenMain.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
    screenMain.fillRect(RIGHT_ZONE_X, FOOTER_Y, RIGHT_ZONE_W, 10, ST77XX_BLACK);
    if (haveTime) {
        char dStr[12];
        snprintf(dStr, sizeof(dStr), "%04d/%02d/%02d", year(), month(), day());
        printCentered(RIGHT_ZONE_X, RIGHT_ZONE_W, FOOTER_Y, dStr, 1);
    } else {
        printCentered(RIGHT_ZONE_X, RIGHT_ZONE_W, FOOTER_Y, "----/--/--", 1);
    }
}

// Fast, bars-only redraw - call on an independent ~150-250ms timer so the
// near-limit flash (see resolveBarFill()) and the Manual Control button's
// active-state flash actually read as a flash rather than crawling along
// at the main dashboard's 2s refresh rate.
void refreshBarsOnly() {
    if (overlayOpen) return; // overlay owns the screen while open

    drawFanRpmBars(LEFT_ZONE_X, BARS_TOP, LEFT_ZONE_W, barsHeight());
    drawTempProbeBars(RIGHT_ZONE_X, BARS_TOP, RIGHT_ZONE_W, barsHeight());
    if (manualOverrideActive) {
        drawManualControlButton(CENTER_X, MANUAL_BTN_Y, CENTER_W, MANUAL_BTN_H);
    }
}

static void drawFanRpmBars(int x0, int y0, int zoneWidth, int zoneHeight) {
    int barsStartX = x0 + (zoneWidth - BAR_SPAN_W) / 2;
    long gaugeMin = config.fanRpmGaugeMin;
    long gaugeMax = config.fanRpmGaugeMax;

    screenMain.fillRect(x0, y0, zoneWidth, zoneHeight, ST77XX_BLACK);

    for (int i = 0; i < 2; i++) {
        int cx = barsStartX + i * (BAR_WIDTH + BAR_SPACING);
        drawGaugeBar(cx, y0, BAR_WIDTH, zoneHeight, ST77XX_CYAN, true,
                     (float)currentRPMs[i], (float)gaugeMin, (float)gaugeMax);
    }

    // Value labels above each bar - just the number, no "RPM" suffix, to
    // fit within the narrow bar width.
    screenMain.fillRect(x0, VALUE_ROW_Y, zoneWidth, 10, ST77XX_BLACK);
    screenMain.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
    for (int i = 0; i < 2; i++) {
        int cx = barsStartX + i * (BAR_WIDTH + BAR_SPACING);
        printCentered(cx, BAR_WIDTH, VALUE_ROW_Y, String(currentRPMs[i]), 1);
    }
}

static void drawTempProbeBars(int x0, int y0, int zoneWidth, int zoneHeight) {
    int barsStartX = x0 + (zoneWidth - BAR_SPAN_W) / 2;

    // Gauge scale is stored in F; convert to the active display unit.
    float minDispF = config.isFahrenheit ? config.tempGaugeMinF : (config.tempGaugeMinF - 32.0) * 5.0 / 9.0;
    float maxDispF = config.isFahrenheit ? config.tempGaugeMaxF : (config.tempGaugeMaxF - 32.0) * 5.0 / 9.0;

    screenMain.fillRect(x0, y0, zoneWidth, zoneHeight, ST77XX_BLACK);

    float dispLocalF = config.isFahrenheit ? ((localTempC * 9.0 / 5.0) + 32.0) : localTempC;
    drawGaugeBar(barsStartX, y0, BAR_WIDTH, zoneHeight,
                 localSensorHealthy ? ST77XX_ORANGE : ST77XX_RED,
                 localSensorHealthy, dispLocalF, minDispF, maxDispF);

    int x1 = barsStartX + BAR_WIDTH + BAR_SPACING;
    float dispNetF = config.isFahrenheit ? ((networkTempC * 9.0 / 5.0) + 32.0) : networkTempC;
    drawGaugeBar(x1, y0, BAR_WIDTH, zoneHeight,
                 networkSensorHealthy ? ST77XX_MAGENTA : ST77XX_RED,
                 networkSensorHealthy, dispNetF, minDispF, maxDispF);

    // Value labels above each bar - bare rounded integer, no unit letter.
    // Repeating the unit on every value was redundant once the big average
    // number (which does show it) is the dashboard's one clear reference,
    // and dropping it gives back real width in this narrow 28px column.
    screenMain.fillRect(x0, VALUE_ROW_Y, zoneWidth, 10, ST77XX_BLACK);
    String localLabel = localSensorHealthy ? String((int)round(dispLocalF)) : "--";
    screenMain.setTextColor(ST77XX_ORANGE, ST77XX_BLACK);
    printCentered(barsStartX, BAR_WIDTH, VALUE_ROW_Y, localLabel, 1);

    String netLabel = networkSensorHealthy ? String((int)round(dispNetF)) : "--";
    screenMain.setTextColor(ST77XX_MAGENTA, ST77XX_BLACK);
    printCentered(x1, BAR_WIDTH, VALUE_ROW_Y, netLabel, 1);
}

// ============================================================
// Manual Control override overlay - full-screen touch UI
// ============================================================
// A full-screen takeover rather than a true overlay-on-top-of-dashboard:
// with no framebuffer yet (see the bookmarked screen-flicker item), a
// partial overlay would fight the 2s dashboard refresh and 200ms bar
// refresh timers over the same pixels. Full takeover sidesteps that
// entirely - both those redraw functions no-op while overlayOpen is true.

static const int OVERLAY_BOX_X = 30;
static const int OVERLAY_BOX_Y = 40;
static const int OVERLAY_BOX_W = 260;
static const int OVERLAY_BOX_H = 160;

static const int OVERLAY_SLIDER_X = OVERLAY_BOX_X + 10;
static const int OVERLAY_SLIDER_Y = OVERLAY_BOX_Y + 60;
static const int OVERLAY_SLIDER_W = OVERLAY_BOX_W - 20;
static const int OVERLAY_SLIDER_H = 16;

static const int OVERLAY_BTN_W = (OVERLAY_BOX_W - 30) / 2;
static const int OVERLAY_BTN_H = 26;
static const int OVERLAY_BTN_Y = OVERLAY_BOX_Y + OVERLAY_BOX_H - 36;
static const int OVERLAY_CANCEL_X = OVERLAY_BOX_X + 10;
static const int OVERLAY_KEEPON_X = OVERLAY_CANCEL_X + OVERLAY_BTN_W + 10;

// Redraws just the percentage readout + slider fill - called on every
// touch-drag update, so it's kept cheap (no box/button redraw needed).
static void drawOverlaySliderOnly() {
    int pct = (manualOverrideDutyCycle * 100) / 255;
    String pctStr = "Fan Speed: " + String(pct) + "%";
    screenMain.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    screenMain.fillRect(OVERLAY_BOX_X, OVERLAY_BOX_Y + 28, OVERLAY_BOX_W, 12, ST77XX_BLACK);
    printCentered(OVERLAY_BOX_X, OVERLAY_BOX_W, OVERLAY_BOX_Y + 28, pctStr, 1);

    screenMain.fillRect(OVERLAY_SLIDER_X + 1, OVERLAY_SLIDER_Y + 1, OVERLAY_SLIDER_W - 2, OVERLAY_SLIDER_H - 2, ST77XX_BLACK);
    int fillW = (OVERLAY_SLIDER_W - 2) * manualOverrideDutyCycle / 255;
    screenMain.fillRect(OVERLAY_SLIDER_X + 1, OVERLAY_SLIDER_Y + 1, fillW, OVERLAY_SLIDER_H - 2, ST77XX_CYAN);
}

// Full overlay draw - called once when it opens.
static void drawOverrideOverlay() {
    screenMain.fillScreen(ST77XX_BLACK);
    screenMain.drawRoundRect(OVERLAY_BOX_X, OVERLAY_BOX_Y, OVERLAY_BOX_W, OVERLAY_BOX_H, 8, ST77XX_CYAN);

    screenMain.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
    printCentered(OVERLAY_BOX_X, OVERLAY_BOX_W, OVERLAY_BOX_Y + 10, "MANUAL OVERRIDE", 1);

    screenMain.drawRoundRect(OVERLAY_SLIDER_X, OVERLAY_SLIDER_Y, OVERLAY_SLIDER_W, OVERLAY_SLIDER_H, 6, ST77XX_WHITE);
    drawOverlaySliderOnly();

    screenMain.drawRoundRect(OVERLAY_CANCEL_X, OVERLAY_BTN_Y, OVERLAY_BTN_W, OVERLAY_BTN_H, 4, ST77XX_RED);
    screenMain.setTextColor(ST77XX_RED, ST77XX_BLACK);
    printCentered(OVERLAY_CANCEL_X, OVERLAY_BTN_W, OVERLAY_BTN_Y + 8, "CANCEL", 1);

    screenMain.drawRoundRect(OVERLAY_KEEPON_X, OVERLAY_BTN_Y, OVERLAY_BTN_W, OVERLAY_BTN_H, 4, ST77XX_GREEN);
    screenMain.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
    printCentered(OVERLAY_KEEPON_X, OVERLAY_BTN_W, OVERLAY_BTN_Y + 8, "KEEP ON", 1);
}

static bool pointInRect(int px, int py, int rx, int ry, int rw, int rh) {
    return px >= rx && px <= rx + rw && py >= ry && py <= ry + rh;
}

bool isOverlayOpen() { return overlayOpen; }

void handleTouchInput() {
    static bool wasPressed = false;

    int tx, ty;
    bool pressed = getTouchPoint(tx, ty);

    if (overlayOpen) {
        if (pressed) {
            // Generous vertical tolerance around the slider track makes it
            // easier to grab with a fingertip than the visual track height alone.
            bool inSliderZone = pointInRect(tx, ty - 10, OVERLAY_SLIDER_X, OVERLAY_SLIDER_Y, OVERLAY_SLIDER_W, OVERLAY_SLIDER_H + 20);

            if (inSliderZone) {
                int rel = constrain(tx - OVERLAY_SLIDER_X, 0, OVERLAY_SLIDER_W);
                manualOverrideDutyCycle = map(rel, 0, OVERLAY_SLIDER_W, 0, 255);
                drawOverlaySliderOnly();
            } else if (!wasPressed) {
                // Buttons only fire on the initial press edge, not every
                // poll while held, so a lingering finger doesn't double-fire.
                if (pointInRect(tx, ty, OVERLAY_CANCEL_X, OVERLAY_BTN_Y, OVERLAY_BTN_W, OVERLAY_BTN_H)) {
                    manualOverrideActive = false; // revert to auto
                    overlayOpen = false;
                    pushOverrideSwitchToHA(); // switch-only - see home_assistant.h for why
                    Serial.println("Override DEACTIVATED via LCD (Cancel)");
                    sdLogEvent("OVERRIDE", "source=LCD action=OFF (cancel)");
                } else if (pointInRect(tx, ty, OVERLAY_KEEPON_X, OVERLAY_BTN_Y, OVERLAY_BTN_W, OVERLAY_BTN_H)) {
                    overlayOpen = false; // stays active; button keeps flashing (see refreshBarsOnly())
                    pushOverrideSpeedToHA(); // speed-only - switch state hasn't changed here
                    int pct = (manualOverrideDutyCycle * 100) / 255;
                    Serial.print("Override kept ON via LCD - speed="); Serial.print(pct); Serial.println("%");
                    sdLogEvent("OVERRIDE", "source=LCD action=SPEED (keep-on) speed=" + String(pct) + "%");
                }
            }
        }
    } else {
        // Dashboard idle: only the Manual Control button is touch-active,
        // and only on the initial press edge.
        if (pressed && !wasPressed) {
            if (pointInRect(tx, ty, CENTER_X, MANUAL_BTN_Y, CENTER_W, MANUAL_BTN_H)) {
                if (!manualOverrideActive) {
                    manualOverrideActive = true;
                    manualOverrideDutyCycle = 255; // default full speed per spec
                    overlayOpen = true;
                    drawOverrideOverlay();
                    pushOverrideSwitchToHA(); // switch-only - HA's own automation resets its speed helper to 255
                    Serial.println("Override ACTIVATED via LCD - speed=100%");
                    sdLogEvent("OVERRIDE", "source=LCD action=ON speed=100%");
                } else {
                    manualOverrideActive = false; // tap while flashing turns override off directly
                    pushOverrideSwitchToHA(); // switch-only
                    Serial.println("Override DEACTIVATED via LCD (tap while flashing)");
                    sdLogEvent("OVERRIDE", "source=LCD action=OFF (tap-while-flashing)");
                }
            }
        }
    }

    wasPressed = pressed;
}
