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
#include <esp_core_dump.h>
#include <stdio.h>
#include <unistd.h>

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
    uint32_t uptimeS;        // seconds (was ms, which wrapped after 49.7 days)
    char stage[12];          // part of loop() running (sdLoggerMarkStage)
    uint32_t freeHeap;       // at the last snapshot
    uint32_t minFreeHeap;    // lowest since boot
    char lowStage[12];       // part of loop() when that lowest point was reached
};

// Changed with the layout (2026-10-05: stage and heap added), so a snapshot
// left by older firmware isn't read with the new layout.
static const uint32_t SNAPSHOT_MAGIC = 0xFA57C0E1; // 2026-10-10: uptime in seconds (audit 5.3)
RTC_NOINIT_ATTR SystemSnapshot rtcSnapshot;

// Seconds since boot from the 64-bit microsecond timer: millis() / 1000 starts
// again at 0 after 49.7 days (audit 5.3). Differences of millis() are wrap-safe
// and stay as they are; only uptime shown as a number needed this.
uint32_t uptimeSeconds() { return (uint32_t)(esp_timer_get_time() / 1000000); }

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
    rtcSnapshot.uptimeS = uptimeSeconds();
    rtcSnapshot.freeHeap = ESP.getFreeHeap();
    rtcSnapshot.minFreeHeap = ESP.getMinFreeHeap();
}

static uint32_t lowSeen = UINT32_MAX;
static char lowStage[12] = "boot";

void sdLoggerMarkStage(const char* name) {
    // A new low since the last mark happened during the stage that just ran
    uint32_t m = ESP.getMinFreeHeap();
    if (m < lowSeen) {
        if (lowSeen != UINT32_MAX && lowSeen - m >= 4096) { // say so for steps of 4 KB or more
            Serial.printf("HEAP: new low %u bytes during %s\n", (unsigned)m, rtcSnapshot.stage);
        }
        lowSeen = m;
        strncpy(lowStage, rtcSnapshot.stage[0] ? rtcSnapshot.stage : "boot", sizeof(lowStage) - 1);
        memcpy(rtcSnapshot.lowStage, lowStage, sizeof(lowStage));
    }
    strncpy(rtcSnapshot.stage, name, sizeof(rtcSnapshot.stage) - 1);
    rtcSnapshot.stage[sizeof(rtcSnapshot.stage) - 1] = '\0';
}

const char* heapLowStage() { return lowStage; }
const char* sdLoggerStage() { return rtcSnapshot.stage; }

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
static bool drainPending = false;  // buffer to write to the card (remount, or left from before a restart)
static size_t drainPos = 0;        // bytes of the buffer already on the card (lost on restart: duplicates, not gaps)
static long spillBytes = -1;       // buffer size; -1 = measure on the next ask (audit 5.2)

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
    spillBytes = -1; // measured again only when asked: once per buffered row, not 8 times a second
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

// Writes text to an SD file ("a" appends, "w" replaces). True only if it
// reached the card. The Arduino File layer ignores the result of its final
// flush, so f.print() + f.close() "succeeded" on a card that was gone
// (audit 3.1); the C calls report every step. SD.begin() mounts at /sd.
// Parent folders must exist (made at mount).
static bool sdWriteChecked(const String &path, const char* mode, const String &text) {
    String full = "/sd" + path;
    FILE* fp = fopen(full.c_str(), mode);
    if (!fp) return false;
    bool ok = fwrite(text.c_str(), 1, text.length(), fp) == text.length();
    ok = fflush(fp) == 0 && ok;          // C buffer -> FAT layer
    ok = fsync(fileno(fp)) == 0 && ok;   // FAT layer -> card
    ok = fclose(fp) == 0 && ok;
    return ok;
}

// Appends to an SD file; a new file starts with its column names
static bool sdAppendChecked(const String &path, const String &text) {
    String header = SD.exists(path) ? String() : csvHeaderFor(path);
    return sdWriteChecked(path, "a", header.length() ? header + "\n" + text : text);
}

// The card stopped taking writes: pulled out or failing. The 15 s retry in
// sdLoggerLoop() mounts it again when it's back.
static void markCardMissing() {
    Serial.println("WARNING: SD write failed - card marked missing, logging to internal flash.");
    SD.end();
    sdPresent = false;
    usedPercent = -1;
}

// Appends one line to an SD file. Falls back to the buffer in internal flash
// if the card is absent, low on space, or the write fails.
static void appendLineImpl(const char* sdPath, const String &line) {
    if (sdHasFreeSpace()) {
        if (sdAppendChecked(sdPath, line)) return;
        markCardMissing();
    }
    appendToSpillover(sdPath, line);
}

// SD writes are their own watchdog stage, then the caller's is put back: a
// slow write from the network/MQTT/web step showed as that step (audit 2.4)
static void appendLine(const char* sdPath, const String &line) {
    char caller[sizeof(rtcSnapshot.stage)];
    memcpy(caller, rtcSnapshot.stage, sizeof(caller));
    sdLoggerMarkStage("sd:write");
    appendLineImpl(sdPath, line);
    sdLoggerMarkStage(caller);
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

// Asked by the LCD title bar every 200 ms, MQTT, the web page and the logger
// (through sdState()): the size is kept, not read from the file each time (5.2).
bool isSpilloverNearFull() {
    if (!isLittleFsMounted()) return false; // nothing to check if it's not even mounted
    if (spillBytes < 0) {
        spillBytes = 0;
        if (LittleFS.exists(SPILLOVER_PATH)) {
            File f = LittleFS.open(SPILLOVER_PATH, FILE_READ);
            if (f) { spillBytes = f.size(); f.close(); }
        }
    }
    return spillBytes > (long)(SPILLOVER_MAX_BYTES * 4 / 5); // >80% of cap
}

// Writes the buffer to the card in one go, in checked pieces of up to 4 KB
// (lines for one file are written together). The buffer is deleted only once
// all of it is on the card; a failed write keeps it and drainPos, and the
// next remount carries on from there (audit 3.2: it used to be deleted even
// when the writes had failed, up to ~2 days of rows).
static void drainSpilloverToSD() {
    if (!isLittleFsMounted() || !sdPresent) return;
    if (!LittleFS.exists(SPILLOVER_PATH)) { drainPos = 0; drainPending = false; return; }
    File f = LittleFS.open(SPILLOVER_PATH, FILE_READ);
    if (!f) return;
    size_t size = f.size();
    if (drainPos > size) drainPos = 0;
    f.seek(drainPos);
    size_t startPos = drainPos;

    // Each line names its file ("<path>\t<line>"); lines from older firmware
    // without a path go to the current month's log, as they used to.
    String monthPath = "/logs/" + String(year()) + "-" + (month() < 10 ? "0" : "") + String(month()) + ".csv";
    String runPath, run;
    size_t runEnd = drainPos;
    bool failed = false;
    while (true) {
        bool more = f.position() < size;
        String line, path;
        if (more) {
            line = f.readStringUntil('\n');
            path = monthPath;
            int tab = line.indexOf('\t');
            if (tab > 0) { path = line.substring(0, tab); line = line.substring(tab + 1); }
        }
        // Write the piece so far when the file changes, it reaches 4 KB, or at the end
        if (run.length() && (!more || path != runPath || run.length() >= 4096)) {
            if (!sdAppendChecked(runPath, run)) { failed = true; break; }
            drainPos = runEnd;
            run = "";
        }
        if (!more) break;
        runPath = path;
        if (line.length()) { run += line; run += '\n'; }
        runEnd = f.position();
    }
    f.close();

    if (failed) {
        markCardMissing();
        sdLogEvent("SD", "buffer kept: write failed after " + String((drainPos - startPos) / 1024) +
                         " of " + String((size - startPos) / 1024) + " KB, carried on when the card is back");
        return;
    }
    LittleFS.remove(SPILLOVER_PATH);
    spillBytes = 0;
    drainPos = 0;
    drainPending = false;
    spilloverCapWarned = false;
    Serial.println("SD card: buffered log data written from internal flash.");
    sdLogEvent("SD", "buffer written to the card: " + String((size - startPos + 1023) / 1024) + " KB");
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

// Kept in RTC memory through restarts (OTA, settings, watchdog, self-heal), so
// the day's summary covers the whole day and a restart over midnight still
// closes the old day (audit 3.6). Lost on a power cut, like rtcSnapshot.
// Magic and date sit with the values, so a build that moves them can't
// misread them. Change DAILY_MAGIC whenever this layout changes.
struct DailyRtc {
    uint32_t magic;
    uint32_t dateKey;          // yyyymmdd the values belong to, 0 = no day yet
    DailyExtremes ext;
};
static const uint32_t DAILY_MAGIC = 0xDA11E001;
RTC_NOINIT_ATTR static DailyRtc dailyRtc;
static DailyExtremes &today = dailyRtc.ext;

// "No samples" markers: any real value replaces them (written as-is to the
// rollups; the web page and HA show them as no value)
static void initExtremes(DailyExtremes &e) {
    e.localMin = e.netMin = e.blendMin = 1e6;
    e.localMax = e.netMax = e.blendMax = -1e6;
    e.fan1Min = e.fan2Min = 999999;
    e.fan1Max = e.fan2Max = -1;
    e.anySample = false;
}

static void resetDailyExtremes() { initExtremes(today); }

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

// Reads the 10 values after the first field of a daily.csv / alltime.csv row.
// False for a row cut short (e.g. by a power cut).
static bool parseExtremes(const String &line, DailyExtremes &e) {
    float v[10];
    int start = line.indexOf(',') + 1;
    if (start <= 0) return false;
    for (int i = 0; i < 10; i++) {
        int c = line.indexOf(',', start);
        if (c == -1 && i < 9) return false;
        v[i] = line.substring(start, c == -1 ? line.length() : c).toFloat();
        start = c + 1;
    }
    e.localMin = v[0]; e.localMax = v[1]; e.netMin = v[2]; e.netMax = v[3];
    e.blendMin = v[4]; e.blendMax = v[5];
    e.fan1Min = (long)v[6]; e.fan1Max = (long)v[7]; e.fan2Min = (long)v[8]; e.fan2Max = (long)v[9];
    return true;
}

static const char* DAILY_PATH = "/rollups/daily.csv";
static const char* ALLTIME_PATH = "/rollups/alltime.csv";
static const char* ALLTIME_TMP = "/rollups/alltime.tmp";
static bool allTimeStale = false;  // rebuild the all-time record (boot, remount, new day)

// The all-time record, worked out again from every row of daily.csv, which
// keeps every day since 2026-10-08 (it used to be cut to 30 days). Nothing is
// read from the old record, so a damaged or missing alltime.csv mends itself
// and edited daily rows count. Written to a temp file first, then swapped in:
// a power cut or card fault leaves the old record or none, never half of one
// (audit 3.3; it used to be deleted, then written).
static void rebuildAllTimeRecord() {
    if (!SD.exists(DAILY_PATH)) { allTimeStale = false; return; }
    File f = SD.open(DAILY_PATH, FILE_READ);
    if (!f) { markCardMissing(); return; }   // retried after the remount
    DailyExtremes all, d;
    initExtremes(all);
    int days = 0;
    while (f.available()) {
        String line = f.readStringUntil('\n');
        if (!line.length() || !isDigit(line[0]) || !parseExtremes(line, d)) continue; // column names, cut rows
        all.localMin = min(all.localMin, d.localMin); all.localMax = max(all.localMax, d.localMax);
        all.netMin = min(all.netMin, d.netMin);       all.netMax = max(all.netMax, d.netMax);
        all.blendMin = min(all.blendMin, d.blendMin); all.blendMax = max(all.blendMax, d.blendMax);
        all.fan1Min = min(all.fan1Min, d.fan1Min);    all.fan1Max = max(all.fan1Max, d.fan1Max);
        all.fan2Min = min(all.fan2Min, d.fan2Min);    all.fan2Max = max(all.fan2Max, d.fan2Max);
        days++;
    }
    f.close();
    if (days == 0) { allTimeStale = false; return; }

    String content = csvHeaderFor(ALLTIME_PATH) + "\n" + "ALL," + extremesToCsvFields(all) + "\n";
    SD.remove(ALLTIME_TMP);
    bool ok = sdWriteChecked(ALLTIME_TMP, "w", content);
    if (ok && SD.exists(ALLTIME_PATH)) ok = SD.remove(ALLTIME_PATH); // FAT rename won't overwrite
    if (ok) ok = SD.rename(ALLTIME_TMP, ALLTIME_PATH);
    if (!ok) { markCardMissing(); return; }  // the next rebuild clears the temp file
    allTimeStale = false;
}

// Closes the day the values belong to (dateKey yyyymmdd), which is not
// always yesterday: a restart can span more than a day.
static void finalizeDailyRollup(uint32_t dateKey) {
    if (!today.anySample) return; // nothing recorded yet (e.g. first-ever boot)

    char buf[12];
    snprintf(buf, sizeof(buf), "%04lu-%02lu-%02lu", (unsigned long)(dateKey / 10000),
             (unsigned long)(dateKey / 100 % 100), (unsigned long)(dateKey % 100));
    String rollupDate(buf);

    String row = rollupDate + "," + extremesToCsvFields(today) + "\n";
    appendLine(DAILY_PATH, row);   // kept for good (no 30-day purge since 2026-10-08)
    allTimeStale = true;

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
    // Rows buffered before a restart: written once the clock is set (sdLoggerLoop)
    drainPending = isLittleFsMounted() && LittleFS.exists(SPILLOVER_PATH);
    allTimeStale = true;  // also clears a temp file left by a power cut

    bool dayKept = esp_reset_reason() != ESP_RST_POWERON && dailyRtc.magic == DAILY_MAGIC;
    if (!dayKept) {
        resetDailyExtremes();
        dailyRtc.dateKey = 0;
        dailyRtc.magic = DAILY_MAGIC;
    }

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
        desc += " uptimeAtReset=" + String(rtcSnapshot.uptimeS) + "s";
        rtcSnapshot.stage[sizeof(rtcSnapshot.stage) - 1] = '\0';
        desc += " stage=" + String(rtcSnapshot.stage);
        rtcSnapshot.lowStage[sizeof(rtcSnapshot.lowStage) - 1] = '\0';
        desc += " heap=" + String(rtcSnapshot.freeHeap) + " minHeap=" + String(rtcSnapshot.minFreeHeap) + " during " + String(rtcSnapshot.lowStage);
    } else {
        desc += " | no prior state available (cold boot or RTC memory invalid)";
    }
    if (dayKept && dailyRtc.dateKey) {
        desc += " | day " + String(dailyRtc.dateKey) + " hi/lo kept";
        if (today.localMin <= today.localMax)
            desc += ": local " + String(today.localMin, 1) + "-" + String(today.localMax, 1) + "C";
    } else {
        desc += " | day hi/lo start over";
    }

    // After a crash or watchdog: the task and code addresses from the crash dump
    // in flash (2026-10-07: a TASK_WDT "stage=probe" was really the W5500 driver's
    // task starving loop(); the stage alone can't say that). Decode the addresses
    // with the firmware.elf of that build (docs/BOARDS.md); the full dump stays
    // in flash for esp-coredump.
    esp_reset_reason_t rr = esp_reset_reason();
    if (rr == ESP_RST_PANIC || rr == ESP_RST_INT_WDT || rr == ESP_RST_TASK_WDT || rr == ESP_RST_WDT) {
        esp_core_dump_summary_t *sum = (esp_core_dump_summary_t *)malloc(sizeof(esp_core_dump_summary_t));
        if (sum && esp_core_dump_get_summary(sum) == ESP_OK) {
            sum->exc_task[sizeof(sum->exc_task) - 1] = '\0';
            char buf[24];
            desc += " | crash: task=" + String(sum->exc_task[0] ? sum->exc_task : "?");
            snprintf(buf, sizeof(buf), " pc=0x%08lx bt=", (unsigned long)sum->exc_pc);
            desc += buf;
            uint32_t depth = min(sum->exc_bt_info.depth, (uint32_t)8);
            for (uint32_t i = 0; i < depth; i++) {
                snprintf(buf, sizeof(buf), "%s0x%08lx", i ? " " : "", (unsigned long)sum->exc_bt_info.bt[i]);
                desc += buf;
            }
            if (sum->exc_bt_info.corrupted) desc += " (corrupted)";
        } else {
            desc += " | crash: no dump in flash";
        }
        free(sum);
    }

    Serial.print("Boot event: "); Serial.println(desc);
    sdLogEvent("BOOT", desc);

    rtcSnapshot.stage[0] = '\0';
    rtcSnapshot.lowStage[0] = '\0';
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

    if (drainPending && sdPresent) drainSpilloverToSD();

    // --- Day change: close the old day first, so the new day's first row
    // isn't counted in it ---
    uint32_t dateKey = (uint32_t)year() * 10000 + month() * 100 + day();
    if (dailyRtc.dateKey == 0) {
        dailyRtc.dateKey = dateKey;   // first day this power-up
    } else if (dateKey != dailyRtc.dateKey) {
        finalizeDailyRollup(dailyRtc.dateKey);
        resetDailyExtremes();
        dailyRtc.dateKey = dateKey;
    }
    if (allTimeStale && sdPresent) rebuildAllTimeRecord();

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
            drainPending = true;  // written on the next pass (needs the clock)
            allTimeStale = true;  // the card may have been edited on a PC
        }
    }
}
