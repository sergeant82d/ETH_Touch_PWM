#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include <Arduino.h>

// Mounts the SD card (shares the LCD's SPI bus - see display.h's
// getDisplaySPI()), logs the boot reason + last-known system state
// (survives everything except a true cold boot - see the RTC_NOINIT_ATTR
// snapshot in sd_logger.cpp), and prepares the log file structure.
// Safe to call even if no card is inserted - logging degrades gracefully
// to internal-flash spillover rather than blocking startup.
void sdLoggerInit();

// Call every loop() iteration. Internally rate-limited:
//   - per-minute time-series row -> /logs/YYYY-MM.csv (no purge, kept
//     indefinitely - see the storage-cost bookmark math)
//   - daily hi/lo finalized and appended to /rollups/daily.csv at the
//     day rollover (TimeLib-driven), all-time record updated in place,
//     30-day-old daily rows purged
//   - periodic retry + drain-to-SD if the card was absent and comes back
void sdLoggerLoop();

// Call periodically (e.g. every 1s, alongside the existing thermal sample
// cycle) to keep the RTC_NOINIT_ATTR snapshot current, so a reset that
// happens between calls still has a recent, useful "last known state" to
// report on the next boot.
void sdLoggerUpdateSnapshot();

// Generic append-only event logger - "timestamp,category,description" to
// /events.csv. This is the hook point for the manual-override and
// config-change history logging described in the SD-logging spec; wire
// calls to this in wherever those events actually occur (display.cpp's
// touch handler, web_server.cpp's form handler, home_assistant.cpp's
// override/threshold sync) as a fast follow-up - the logging mechanism
// itself (including SD-absent spillover) is already handled here.
void sdLogEvent(const String &category, const String &description);

// Current SD card presence, for status display on the LCD/web dashboard.
// True once mounted successfully; false if never detected or lost partway
// through (data is spilling to internal flash in that case - see
// sd_logger.cpp's spillover mechanism).
bool isSdCardPresent();

// True if the internal-flash spillover buffer (used only while the SD
// card is absent) is over ~80% of its cap - a warning sign the card
// should be reinserted soon before further log rows start being dropped.
bool isSpilloverNearFull();

#endif // SD_LOGGER_H
