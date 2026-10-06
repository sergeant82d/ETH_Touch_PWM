#include "sd_logger.h"
#include "pins.h"
#include "config.h"
#include "sensors.h"
#if HAS_LCD
#include "display.h"
#endif
#include "mqtt.h"
#include <SD.h>
#include <LittleFS.h>
#include <TimeLib.h>
#include <esp_system.h>

// ============================================================
// RTC_NOINIT_ATTR last-known-state snapshot
// ============================================================
// Survives software resets, watchdog resets, panics, and brownouts - but
// NOT a true cold boot (that power domain genuinely loses power). The
// magic number lets us tell "valid leftover data from before a reset"
// apart from "garbage, because this really was a cold boot."
struct SystemSnapshot {
    uint32_t magic;
    float localTempC;
    float networkTempC;
    float blendedAverageC;
    unsigned long fan1RPM;
    unsigned long fan2RPM;
    bool manualOverrideActive;
    int manualOverrideDutyCycle;
    bool localSensorHealthy;
    bool networkSensorHealthy;
    unsigned long uptimeMs;
    char stage[12];          // part of loop() running (sdLoggerMarkStage)
    uint32_t freeHeap;       // at the last snapshot
    uint32_t minFreeHeap;    // lowest since boot
};

// Changed with the layout (2026-10-05: stage and heap added), so a snapshot
// left by older firmware isn't read with the new layout.
static const uint32_t SNAPSHOT_MAGIC = 0xFA57C0DF;
RTC_NOINIT_ATTR SystemSnapshot rtcSnapshot;

void sdLoggerUpdateSnapshot() {
    rtcSnapshot.magic = SNAPSHOT_MAGIC;
    rtcSnapshot.localTempC = localTempC;
    rtcSnapshot.networkTempC = networkTempC;
    rtcSnapshot.blendedAverageC = blendedAverageC;
    rtcSnapshot.fan1RPM = currentRPMs[0];
    rtcSnapshot.fan2RPM = currentRPMs[1];
    rtcSnapshot.manualOverrideActive = manualOverrideActive;
    rtcSnapshot.manualOverrideDutyCycle = manualOverrideDutyCycle;
    rtcSnapshot.localSensorHealthy = localSensorHealthy;
    rtcSnapshot.networkSensorHealthy = networkSensorHealthy;
    rtcSnapshot.uptimeMs = millis();
    rtcSnapshot.freeHeap = ESP.getFreeHeap();
    rtcSnapshot.minFreeHeap = ESP.getMinFreeHeap();
}

void sdLoggerMarkStage(const char* name) {
    strncpy(rtcSnapshot.stage, name, sizeof(rtcSnapshot.stage) - 1);
    rtcSnapshot.stage[sizeof(rtcSnapshot.stage) - 1] = '\0';
}

static String resetReasonString() {
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON:   return "POWERON (cold boot)";
        case ESP_RST_EXT:       return "EXT (external reset pin)";
        case ESP_RST_SW:        return "SW (software restart)";
        case ESP_RST_PANIC:     return "PANIC (firmware crash)";
        case ESP_RST_INT_WDT:   return "INT_WDT (interrupt watchdog)";
        case ESP_RST_TASK_WDT:  return "TASK_WDT (task watchdog)";
        case ESP_RST_WDT:       return "WDT (other watchdog)";
        case ESP_RST_DEEPSLEEP: return "DEEPSLEEP (woke from sleep)";
        case ESP_RST_BROWNOUT:  return "BROWNOUT (voltage sag)";
        case ESP_RST_SDIO:      return "SDIO";
        case ESP_RST_USB:       return "USB";
        case ESP_RST_JTAG:      return "JTAG";
        default:                return "UNKNOWN";
    }
}

// ============================================================
// Storage state + SD-absent spillover to internal LittleFS
// ============================================================
static bool sdPresent = false;
static const uint64_t MIN_FREE_BYTES = 5UL * 1024 * 1024; // 5MB safety margin

static const char* SPILLOVER_PATH = "/sd_pending.csv";
static const size_t SPILLOVER_MAX_BYTES = 200 * 1024; // ~200KB cap
static bool spilloverCapWarned = false;

static String timestampNow() {
    if (timeStatus() == timeNotSet) return "notime";
    char buf[24];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             year(), month(), day(), hour(), minute(), second());
    return String(buf);
}

static int usedPercent = -1;           // last measured, for sdState() (measuring can be slow)

static bool sdHasFreeSpace() {
    if (!sdPresent) return false;
    uint64_t total = SD.totalBytes();
    uint64_t used = SD.usedBytes();
    if (total == 0) return false; // couldn't query - treat as unsafe
    usedPercent = (int)(used * 100 / total);
    return (total - used) > MIN_FREE_BYTES;
}

static bool littleFsUnavailableWarned = false;

// Each buffered line is "<sd path>\t<line>", so it goes back to the right
// file (events vs. month log) when the card returns (since 2026-09-28).
static void appendToSpillover(const String &sdPath, const String &text) {
    String line = sdPath + "\t" + text;
    if (!isLittleFsMounted()) {
        // LittleFS never mounted successfully - do NOT touch it. Calling
        // filesystem operations against an unmounted LittleFS is a known
        // way for ESP32's VFS layer to hang indefinitely rather than fail
        // cleanly, which is exactly what this guard exists to prevent.
        if (!littleFsUnavailableWarned) {
            Serial.println("WARNING: LittleFS not mounted - SD-absent spillover is unavailable. This log row is being dropped.");
            littleFsUnavailableWarned = true;
        }
        return;
    }

    File f = LittleFS.open(SPILLOVER_PATH, FILE_APPEND);
    if (!f) {
        f = LittleFS.open(SPILLOVER_PATH, FILE_WRITE);
        if (!f) return;
    }
    if (f.size() + line.length() > SPILLOVER_MAX_BYTES) {
        if (!spilloverCapWarned) {
            Serial.println("WARNING: SD spillover buffer full - further log rows are being dropped until the card returns.");
            spilloverCapWarned = true;
        }
        f.close();
        return;
    }
    f.print(line);
    f.close();
}

// ---- Column names (first line of each file, since 2026-09-28) ----
static const char* DAILY_COLS = "local_min_c,local_max_c,network_min_c,network_max_c,blended_min_c,"
                                "blended_max_c,fan1_min_rpm,fan1_max_rpm,fan2_min_rpm,fan2_max_rpm";

String csvHeaderFor(const String &path) {
    if (path.startsWith("/logs/")) return "timestamp,local_c,network_c,blended_c,fan1_rpm,fan2_rpm,duty_pct,override";
    if (path == "/rollups/daily.csv") return String("date,") + DAILY_COLS;
    if (path == "/rollups/alltime.csv") return String("record,") + DAILY_COLS;
    if (path == "/events.csv") return "timestamp,category,description";
    return "";
}

// Opens an SD file for appending; a new file starts with its column names
static File openForAppend(const String &path) {
    bool isNew = !SD.exists(path);
    File f = SD.open(path, FILE_APPEND);
    if (f && isNew) {
        String header = csvHeaderFor(path);
        if (header.length()) { f.print(header); f.print('\n'); }
    }
    return f;
}

// Appends one line to an SD file, creating parent behavior isn't needed
// (SD.open with FILE_APPEND creates the file if missing, but not parent
// dirs - callers must mkdir once at init). Falls back to spillover if the
// card is absent or low on space.
static void appendLine(const char* sdPath, const String &line) {
    if (sdHasFreeSpace()) {
        File f = openForAppend(sdPath);
        if (f) {
            f.print(line);
            f.close();
            return;
        }
        // Open failed even though we thought the card was present: pulled
        // out or failing. Mark it missing (the 15 s retry in sdLoggerLoop()
        // mounts it again when it's back) and spill this line.
        Serial.println("WARNING: SD write failed - card marked missing, spilling to internal flash.");
        SD.end();
        sdPresent = false;
        usedPercent = -1;
    }
    appendToSpillover(sdPath, line);
}

void sdLogEvent(const String &category, const String &description) {
    String line = timestampNow() + "," + category + "," + description + "\n";
    appendLine("/events.csv", line);
}

bool isSdCardPresent() { return sdPresent; }

SdState sdState() {
    if (!sdPresent) return SD_STATE_MISSING;
    if (usedPercent >= 90 || isSpilloverNearFull()) return SD_STATE_GETTING_FULL;
    return SD_STATE_OK;
}

const char* sdStateText() {
    switch (sdState()) {
        case SD_STATE_GETTING_FULL: return "Getting full";
        case SD_STATE_MISSING:      return "Missing";
        default:                    return "OK";
    }
}

int sdUsedPercent() { return sdPresent ? usedPercent : -1; }

bool isSpilloverNearFull() {
    if (!isLittleFsMounted()) return false; // nothing to check if it's not even mounted
    if (!LittleFS.exists(SPILLOVER_PATH)) return false;
    File f = LittleFS.open(SPILLOVER_PATH, FILE_READ);
    if (!f) return false;
    size_t sz = f.size();
    f.close();
    return sz > (SPILLOVER_MAX_BYTES * 4 / 5); // >80% of cap
}

static void drainSpilloverToSD() {
    if (!isLittleFsMounted()) return; // nothing to drain from an unmounted filesystem
    if (!LittleFS.exists(SPILLOVER_PATH)) return;
    File f = LittleFS.open(SPILLOVER_PATH, FILE_READ);
    if (!f) return;

    // Each line names its file ("<path>\t<line>"); lines from older firmware
    // without a path go to the current month's log, as they used to.
    String monthPath = "/logs/" + String(year()) + "-" + (month() < 10 ? "0" : "") + String(month()) + ".csv";
    String openPath;
    File out;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        if (line.length() == 0) continue;
        String path = monthPath;
        int tab = line.indexOf('\t');
        if (tab > 0) {
            path = line.substring(0, tab);
            line = line.substring(tab + 1);
        }
        if (path != openPath) {          // lines of one file come in runs: reopen only on a change
            if (out) out.close();
            out = openForAppend(path);
            openPath = path;
        }
        if (out) { out.print(line); out.print('\n'); }
    }
    if (out) out.close();
    f.close();
    LittleFS.remove(SPILLOVER_PATH);
    spilloverCapWarned = false;
    Serial.println("SD card back: drained buffered log data from internal flash.");
}

// ============================================================
// Daily hi/lo rollup (in-RAM provisional, finalized+appended at midnight)
// ============================================================
struct DailyExtremes {
    float localMin, localMax;
    float netMin, netMax;
    float blendMin, blendMax;
    long fan1Min, fan1Max;
    long fan2Min, fan2Max;
    bool anySample;
};

static DailyExtremes today;
static int lastLoggedDay = -1;

static void resetDailyExtremes() {
    today.localMin = today.netMin = today.blendMin = 1e6;
    today.localMax = today.netMax = today.blendMax = -1e6;
    today.fan1Min = today.fan2Min = 999999;
    today.fan1Max = today.fan2Max = -1;
    today.anySample = false;
}

static void updateDailyExtremes() {
    if (localSensorHealthy) {
        today.localMin = min(today.localMin, localTempC);
        today.localMax = max(today.localMax, localTempC);
    }
    if (networkSensorHealthy) {
        today.netMin = min(today.netMin, networkTempC);
        today.netMax = max(today.netMax, networkTempC);
    }
    if (localSensorHealthy || networkSensorHealthy) {
        today.blendMin = min(today.blendMin, blendedAverageC);
        today.blendMax = max(today.blendMax, blendedAverageC);
    }
    today.fan1Min = min(today.fan1Min, (long)currentRPMs[0]);
    today.fan1Max = max(today.fan1Max, (long)currentRPMs[0]);
    today.fan2Min = min(today.fan2Min, (long)currentRPMs[1]);
    today.fan2Max = max(today.fan2Max, (long)currentRPMs[1]);
    today.anySample = true;
}

static String extremesToCsvFields(const DailyExtremes &e) {
    String s;
    s += String(e.localMin, 1) + "," + String(e.localMax, 1) + ",";
    s += String(e.netMin, 1) + "," + String(e.netMax, 1) + ",";
    s += String(e.blendMin, 1) + "," + String(e.blendMax, 1) + ",";
    s += String(e.fan1Min) + "," + String(e.fan1Max) + ",";
    s += String(e.fan2Min) + "," + String(e.fan2Max);
    return s;
}

// Updates the never-purged all-time record in place by comparing today's
// finalized extremes against whatever's currently stored.
static void updateAllTimeRecord(const DailyExtremes &finalizedDay) {
    DailyExtremes allTime = finalizedDay; // fallback if no file exists yet

    if (sdPresent && SD.exists("/rollups/alltime.csv")) {
        File f = SD.open("/rollups/alltime.csv", FILE_READ);
        if (f) {
            String line;
            while (f.available()) {              // skip the column-name line
                line = f.readStringUntil('\n');
                if (line.startsWith("ALL,")) break;
            }
            f.close();
            // Format: ALL,localMin,localMax,netMin,netMax,blendMin,blendMax,f1Min,f1Max,f2Min,f2Max
            int idx = line.indexOf(',');
            if (idx != -1) {
                String rest = line.substring(idx + 1);
                float vals[6]; long ivals[4];
                int pos = 0, field = 0;
                // Simple CSV split - this file is always our own known format.
                String work = rest;
                float* floatTargets[6] = {&allTime.localMin, &allTime.localMax, &allTime.netMin, &allTime.netMax, &allTime.blendMin, &allTime.blendMax};
                for (field = 0; field < 6; field++) {
                    int c = work.indexOf(',');
                    String tok = (c == -1) ? work : work.substring(0, c);
                    *floatTargets[field] = tok.toFloat();
                    if (c == -1) break;
                    work = work.substring(c + 1);
                }
                long* longTargets[4] = {&allTime.fan1Min, &allTime.fan1Max, &allTime.fan2Min, &allTime.fan2Max};
                for (field = 0; field < 4; field++) {
                    int c = work.indexOf(',');
                    String tok = (c == -1) ? work : work.substring(0, c);
                    *longTargets[field] = tok.toInt();
                    if (c == -1) break;
                    work = work.substring(c + 1);
                }
                (void)pos; (void)vals; (void)ivals;
            }

            allTime.localMin = min(allTime.localMin, finalizedDay.localMin);
            allTime.localMax = max(allTime.localMax, finalizedDay.localMax);
            allTime.netMin = min(allTime.netMin, finalizedDay.netMin);
            allTime.netMax = max(allTime.netMax, finalizedDay.netMax);
            allTime.blendMin = min(allTime.blendMin, finalizedDay.blendMin);
            allTime.blendMax = max(allTime.blendMax, finalizedDay.blendMax);
            allTime.fan1Min = min(allTime.fan1Min, finalizedDay.fan1Min);
            allTime.fan1Max = max(allTime.fan1Max, finalizedDay.fan1Max);
            allTime.fan2Min = min(allTime.fan2Min, finalizedDay.fan2Min);
            allTime.fan2Max = max(allTime.fan2Max, finalizedDay.fan2Max);
        }
    }

    if (sdHasFreeSpace()) {
        // FILE_WRITE appends on ESP32's SD library, it does not truncate -
        // must remove first for a true overwrite, or this file would just
        // grow with a duplicate "ALL,..." row every time instead of
        // replacing the previous record.
        SD.remove("/rollups/alltime.csv");
        File out = SD.open("/rollups/alltime.csv", FILE_WRITE);
        if (out) {
            out.print(csvHeaderFor("/rollups/alltime.csv") + "\n" + "ALL," + extremesToCsvFields(allTime) + "\n");
            out.close();
        }
    }
}

// Removes daily.csv rows older than 30 days. ISO date strings ("YYYY-MM-DD")
// sort/compare correctly as plain strings, so no date-math library needed.
static void purgeOldDailyRows() {
    if (!sdHasFreeSpace() || !SD.exists("/rollups/daily.csv")) return;

    time_t cutoffTime = now() - 30UL * 24 * 3600;
    char cutoffBuf[12];
    snprintf(cutoffBuf, sizeof(cutoffBuf), "%04d-%02d-%02d", year(cutoffTime), month(cutoffTime), day(cutoffTime));
    String cutoff(cutoffBuf);

    File in = SD.open("/rollups/daily.csv", FILE_READ);
    if (!in) return;

    String kept;
    int purgedCount = 0;
    while (in.available()) {
        String line = in.readStringUntil('\n');
        if (line.length() < 10) continue;
        String lineDate = line.substring(0, 10);
        if (lineDate >= cutoff) {
            kept += line + "\n";
        } else {
            purgedCount++;
        }
    }
    in.close();

    if (purgedCount > 0) {
        // Same FILE_WRITE-appends-not-truncates gotcha as the all-time
        // record above - remove first, or the "kept" rows would get
        // appended after the old, unpurged content instead of replacing it.
        SD.remove("/rollups/daily.csv");
        File out = SD.open("/rollups/daily.csv", FILE_WRITE);
        if (out) {
            out.print(kept);
            out.close();
            Serial.print("Daily rollup purge: removed "); Serial.print(purgedCount);
            Serial.println(" row(s) older than 30 days.");
        }
    }
}

static void finalizeDailyRollup() {
    if (!today.anySample) return; // nothing recorded yet (e.g. first-ever boot)

    // This runs right after the day has already rolled over (see
    // sdLoggerLoop()), so dateNow() would incorrectly label yesterday's
    // accumulated data with today's date. Back up ~12h from "now" to land
    // safely in yesterday regardless of exactly when this fires relative
    // to midnight.
    time_t yesterdayTime = now() - 12UL * 3600;
    char buf[12];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year(yesterdayTime), month(yesterdayTime), day(yesterdayTime));
    String rollupDate(buf);

    String row = rollupDate + "," + extremesToCsvFields(today) + "\n";
    appendLine("/rollups/daily.csv", row);
    updateAllTimeRecord(today);
    purgeOldDailyRows();

    // Once-a-day hi/lo summary to HA over MQTT - NOT the full raw log (that
    // stays local to the SD card per the original spec).
    mqttPublishDailySummary(rollupDate,
                            today.localMin, today.localMax,
                            today.netMin, today.netMax,
                            today.blendMin, today.blendMax,
                            today.fan1Min, today.fan1Max,
                            today.fan2Min, today.fan2Max);
}

// SD bus: shares the LCD's SPI bus on the Touch-LCD-2 (begun, with MISO, in
// displayInit()); has its own SPI bus on the ESP32-S3-ETH.
#if HAS_LCD
static SPIClass& sdSPI() { return getDisplaySPI(); }
#else
static SPIClass& sdSPI() {
    static SPIClass sdBus(HSPI); // FSPI (the default SPI) is the W5500's
    static bool begun = false;
    if (!begun) {
        sdBus.begin(PIN_SD_SCLK, PIN_SD_MISO, PIN_SD_MOSI, -1);
        begun = true;
    }
    return sdBus;
}
#endif

// ============================================================
// Public API
// ============================================================

void sdLoggerInit() {
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH); // deselect SD before the LCD uses the shared bus

    sdPresent = SD.begin(PIN_SD_CS, sdSPI(), 4000000);
    if (sdPresent) {
        SD.mkdir("/logs");
        SD.mkdir("/rollups");
        sdHasFreeSpace(); // measure the card for sdState()
        Serial.print("SD card mounted, "); Serial.print(usedPercent); Serial.println("% used.");
    } else {
        Serial.println("SD card not detected at boot - logging will spill to internal flash until it's inserted.");
    }

    resetDailyExtremes();

    // Boot reason + last-known-state event, using whatever survived in RTC
    // memory. A true cold boot (ESP_RST_POWERON) has nothing meaningful in
    // rtcSnapshot - that memory domain genuinely lost power too.
    String reason = resetReasonString();
    String desc = "reason=" + reason;

    if (esp_reset_reason() != ESP_RST_POWERON && rtcSnapshot.magic == SNAPSHOT_MAGIC) {
        desc += " | lastState: blend=" + String(rtcSnapshot.blendedAverageC, 1) + "C";
        desc += " fan1=" + String(rtcSnapshot.fan1RPM) + "rpm fan2=" + String(rtcSnapshot.fan2RPM) + "rpm";
        desc += " override=" + String(rtcSnapshot.manualOverrideActive ? "ON" : "off");
        desc += " localOK=" + String(rtcSnapshot.localSensorHealthy ? "y" : "n");
        desc += " netOK=" + String(rtcSnapshot.networkSensorHealthy ? "y" : "n");
        desc += " uptimeAtReset=" + String(rtcSnapshot.uptimeMs / 1000) + "s";
        rtcSnapshot.stage[sizeof(rtcSnapshot.stage) - 1] = '\0';
        desc += " stage=" + String(rtcSnapshot.stage);
        desc += " heap=" + String(rtcSnapshot.freeHeap) + " minHeap=" + String(rtcSnapshot.minFreeHeap);
    } else {
        desc += " | no prior state available (cold boot or RTC memory invalid)";
    }

    Serial.print("Boot event: "); Serial.println(desc);
    sdLogEvent("BOOT", desc);

    sdLoggerMarkStage("setup");
    sdLoggerUpdateSnapshot(); // establish a valid snapshot immediately, don't wait for the first 1s tick
}

// A card pulled out while running: file writes don't notice (FatFs buffers
// them and the flush error is lost), so read sector 0 straight from the card
// every 10 s. Two failures in a row = missing (found 2026-09-28).
static void checkCardStillThere() {
    static unsigned long lastCheck = 0;
    static int failures = 0;
    static uint8_t sector[512];
    if (!sdPresent || millis() - lastCheck < 10000) return;
    lastCheck = millis();
    if (SD.readRAW(sector, 0)) { failures = 0; return; }
    if (++failures < 2) return;
    failures = 0;
    Serial.println("WARNING: SD card not answering - marked missing, logging to internal flash.");
    SD.end();
    sdPresent = false;
    usedPercent = -1;
}

void sdLoggerLoop() {
    checkCardStillThere();

    // --- SD health changes go to the event log (the buffer if the card is out) ---
    static int lastState = -1;
    static unsigned long lastStateCheck = 0;
    if (millis() - lastStateCheck >= 1000) {
        lastStateCheck = millis();
        int st = sdState();
        if (lastState != -1 && st != lastState) {
            String desc = String("state=") + sdStateText();
            if (sdUsedPercent() >= 0) desc += " used=" + String(sdUsedPercent()) + "%";
            Serial.print("SD card: "); Serial.println(desc);
            sdLogEvent("SD", desc);
        }
        lastState = st;
    }

    if (timeStatus() == timeNotSet) return; // can't build valid timestamps/filenames yet

    // --- Per-minute time-series row ---
    static unsigned long lastMinuteLogMs = 0;
    const unsigned long MINUTE_LOG_PERIOD_MS = 60000;
    if (millis() - lastMinuteLogMs >= MINUTE_LOG_PERIOD_MS) {
        lastMinuteLogMs = millis();

        String monthPath = "/logs/" + String(year()) + "-" + (month() < 10 ? "0" : "") + String(month()) + ".csv";
        // timestamp, local, network, blended (C; empty = probe failed, not a
        // stale value), fan 1 RPM, fan 2 RPM, fan duty % (both fans share the
        // curve), manual override (1/0). Duty and override since 2026-09-28;
        // older rows have 6 fields. Read by the web page's History tab.
        auto temp = [](bool ok, float c) { return ok ? String(c, 1) : String(""); };
        String row = timestampNow() + "," +
                     temp(localSensorHealthy, localTempC) + "," +
                     temp(networkSensorHealthy, networkTempC) + "," +
                     temp(localSensorHealthy || networkSensorHealthy, blendedAverageC) + "," +
                     String(currentRPMs[0]) + "," +
                     String(currentRPMs[1]) + "," +
                     String((currentDutyCycles[0] * 100 + 127) / 255) + "," +
                     (manualOverrideActive ? "1" : "0") + "\n";
        appendLine(monthPath.c_str(), row);

        updateDailyExtremes();
    }

    // --- Day-rollover check (finalize+purge once per day) ---
    int currentDay = day();
    if (lastLoggedDay == -1) {
        lastLoggedDay = currentDay; // first run this boot - don't finalize on startup
    } else if (currentDay != lastLoggedDay) {
        finalizeDailyRollup();
        resetDailyExtremes();
        lastLoggedDay = currentDay;
    }

    // --- SD-absent retry (every 15 s) ---
    static unsigned long lastSdRetryMs = 0;
    const unsigned long SD_RETRY_PERIOD_MS = 15000;
    if (!sdPresent && millis() - lastSdRetryMs >= SD_RETRY_PERIOD_MS) {
        lastSdRetryMs = millis();
        if (SD.begin(PIN_SD_CS, sdSPI(), 4000000)) {
            sdPresent = true;
            SD.mkdir("/logs");
            SD.mkdir("/rollups");
            sdHasFreeSpace(); // measure the new card now
            drainSpilloverToSD();
        }
    }
}
