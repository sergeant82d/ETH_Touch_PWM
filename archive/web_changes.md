# ESP32-S3 Fan Controller — Handoff: Changes Not Yet in GitHub (baseline: commit `c23de85`)

**Scope note up front:** I don't have access to your actual GitHub repository, so I can't diff against commit `c23de85` directly. What follows is every substantive code change made across our sessions together, on the assumption that none of it has been committed yet. If some of this already made it into `c23de85`, treat the corresponding section as confirmation rather than new work.

Project: Waveshare ESP32-S3-Touch-LCD-2, external W5500 Ethernet, node `fan_controller_02`. 18 files, ~3,080 lines total.

---

## Table of Contents

1. [Boot Order & Storage Reliability](#1-boot-order--storage-reliability)
2. [NTP Sync Reliability](#2-ntp-sync-reliability)
3. [LCD Clock Source Fix](#3-lcd-clock-source-fix)
4. [Home Assistant Connection Logging](#4-home-assistant-connection-logging)
5. [Sketch Filename / Build-Info Fix](#5-sketch-filename--build-info-fix)
6. [Home Assistant Two-Way Sync — Thresholds](#6-home-assistant-two-way-sync--thresholds)
7. [Manual Fan Override — Core State & Fan-Curve Integration](#7-manual-fan-override--core-state--fan-curve-integration)
8. [Touch Input Framework](#8-touch-input-framework)
9. [LCD Manual Override Overlay UI](#9-lcd-manual-override-overlay-ui)
10. [Web Dashboard Manual Override Control](#10-web-dashboard-manual-override-control)
11. [Home Assistant Two-Way Sync — Manual Override (incl. two race-condition fixes)](#11-home-assistant-two-way-sync--manual-override-incl-two-race-condition-fixes)
12. [SD Card Data Logging (full subsystem)](#12-sd-card-data-logging-full-subsystem)
13. [Config-Change History Logging](#13-config-change-history-logging)
14. [SD / Network Status Indicators](#14-sd--network-status-indicators)
15. [Daily Rollup Push to Home Assistant](#15-daily-rollup-push-to-home-assistant)
16. [LCD Dashboard Visual Overhaul](#16-lcd-dashboard-visual-overhaul)
17. [Web Dashboard Data-Honesty Fix](#17-web-dashboard-data-honesty-fix)
18. [Hardware Pin Remap](#18-hardware-pin-remap)
19. [Home Assistant YAML (not in this repo, but required for the firmware to work)](#19-home-assistant-yaml-not-in-this-repo-but-required-for-the-firmware-to-work)
20. [Open Questions](#20-open-questions)

---

## 1. Boot Order & Storage Reliability

### 1a. LittleFS partition-label fix + self-healing mount

**What & why:** `LittleFS.begin()` defaults to looking for a partition literally named `"spiffs"`. This project's OTA-ready partition scheme (`app3M_fat9M_16MB`) names its data partition `"ffat"` instead, so the default call silently failed to find it. Separately, `formatOnFail=true` reliably reformats a *missing* filesystem but does not reliably catch genuine on-disk **corruption** (littlefs error -84, "corrupted dir pair") — that failure mode was showing up as a permanent mount failure on every subsequent boot rather than self-healing.

**File/function:** `config.h` (declaration), `config.cpp` (`mountLittleFSWithRecovery()`, `isLittleFsMounted()`)

**Exact code — `config.h`:**
```cpp
// Mounts LittleFS on the "ffat" partition, force-reformatting on a failed
// mount (including genuine corruption, not just a missing filesystem) so a
// bad partition self-heals on next boot instead of failing permanently.
// Call this once in setup() instead of LittleFS.begin() directly.
bool mountLittleFSWithRecovery();

// True only if LittleFS is confirmed successfully mounted (either the
// initial attempt or the reformat-recovery attempt succeeded). Other code
// (notably sd_logger.cpp's SD-absent spillover path) must check this
// before calling any LittleFS function - calling filesystem operations
// against a LittleFS that never actually mounted is a known way for
// ESP32's VFS layer to hang indefinitely rather than fail cleanly.
bool isLittleFsMounted();
```

**Exact code — `config.cpp`:**
```cpp
static bool littleFsMounted = false;

bool mountLittleFSWithRecovery() {
    if (LittleFS.begin(true, "/littlefs", 10, "ffat")) {
        littleFsMounted = true;
        return true;
    }

    Serial.println("LittleFS mount failed (possible corruption) - reformatting...");
    if (!LittleFS.format()) {
        Serial.println("LittleFS format failed - storage may be faulty.");
        littleFsMounted = false;
        return false;
    }
    if (LittleFS.begin(false, "/littlefs", 10, "ffat")) {
        Serial.println("LittleFS reformatted and mounted - settings reset to defaults.");
        littleFsMounted = true;
        return true;
    }
    Serial.println("LittleFS still failed to mount after reformat.");
    littleFsMounted = false;
    return false;
}

bool isLittleFsMounted() { return littleFsMounted; }
```

**Called from `ESP32_S3_FanController_2inch.ino`, `setup()`:**
```cpp
if (mountLittleFSWithRecovery()) {
    loadSettings();
} else {
    Serial.println("Critical error: LittleFS mount/format failed.");
}
```

**Decisions made:** Force a reformat on corruption rather than leaving a permanently-broken partition — settings reset to compiled-in defaults is an acceptable tradeoff since settings are small and re-enterable via the web form. **Rejected:** attempting partial recovery/repair of a corrupted LittleFS image — not supported by the library and not worth the complexity for a small settings file.

**Tested:** Yes — confirmed via real hardware log showing the corruption error, and separately confirmed via SD event-log data (a `tMin` change survived a reboot and was read back correctly), proving the mount + persistence genuinely works end-to-end, not just "doesn't hang."

---

### 1b. LittleFS-mounted guard in the SD logger (hang-prevention)

**What & why:** Even with 1a in place, if LittleFS somehow still fails to mount (e.g., unresolved hardware/flash-wear issue), `sd_logger.cpp`'s SD-absent spillover path was calling `LittleFS.open()`/`LittleFS.exists()`/`LittleFS.remove()` unconditionally. Calling filesystem operations against a LittleFS that never mounted is a known way for ESP32's VFS layer to **hang indefinitely** rather than fail cleanly. This was diagnosed as the likely cause of a real observed boot hang (board froze consistently right after the touch-disabled diagnostic print, immediately before the SD logger's own "initial free heap" print — i.e., inside `sdLoggerInit()`).

**File/function:** `sd_logger.cpp` — `appendToSpillover()`, `isSpilloverNearFull()`, `drainSpilloverToSD()`

**Exact code (the three guarded functions):**
```cpp
static bool littleFsUnavailableWarned = false;

static void appendToSpillover(const String &line) {
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
    // ... (unchanged body — see section 12)
}
```

**Decisions made:** Fail with a one-time warning and drop the log row, rather than retry or block. **Rejected:** retry loops (risk of the same hang recurring); blocking the caller until LittleFS recovers (no recovery mechanism exists mid-run — only at boot).

**Tested:** The guard logic itself has not been exercised against a genuinely-still-broken LittleFS in a live test (the actual root cause turned out to be a power-supply issue, covered in section 1c, and LittleFS mounted fine once that was fixed). This is a defensive fix that has not been triggered in practice since being added.

---

### 1c. Boot order swap (Ethernet before Display)

**What & why:** The new network-status LCD dot (section 14) made `drawTitleBar()` call `Ethernet.linkStatus()`. `drawTitleBar()` runs once inside `displayInit()`, which was running **before** `networkInit()` in `setup()`. This meant the very first title-bar draw queried the W5500 over SPI before `Ethernet.begin()`/`Ethernet.init()` had ever run — an out-of-order SPI transaction to an unconfigured chip. This was observed on real hardware to corrupt the subsequent real initialization: `Ethernet.hardwareStatus()` came back `0` (no W5500 detected at all) and the IP was stuck at `255.255.255.255`, even though the wiring was fine.

**File/function:** `ESP32_S3_FanController_2inch.ino`, `setup()`

**Exact code:**
```cpp
// networkInit() must come before displayInit(): drawTitleBar() (called
// once inside displayInit() for the initial frame) now queries
// Ethernet.linkStatus() for the network status dot. Querying it before
// Ethernet.begin()/init() have actually run is an out-of-order SPI
// transaction to a chip that isn't set up yet, and was observed to
// corrupt the real initialization moments later - hardwareStatus()
// came back 0 (no W5500 detected at all) and the IP stuck at
// 255.255.255.255, even though the wiring/hardware was fine.
networkInit();
displayInit();
sensorsInit();
```

(Was: `displayInit(); networkInit(); sensorsInit();`)

**Decisions made:** Reorder rather than guard `drawTitleBar()` against being called too early — reordering is simpler, has no other dependency conflicts (`sdLoggerInit()`, which needs `displayInit()`'s shared SPI bus via `getDisplaySPI()`, still runs after `displayInit()` regardless of where `networkInit()` sits), and fixes the root cause rather than papering over a symptom.

**Tested:** Yes, confirmed on real hardware — subsequent boot logs showed correct hardware status and IP assignment.

---

### 1d. Root cause of the original overnight lockup: NOT a code bug

**What & why:** Before the above was diagnosed, an unrelated, more serious symptom was under investigation: full, watchdog-defeating system lockups after ~15 minutes of uptime, twice, unattended. Touch/I2C was the leading suspect (newest, least hardware-verified, continuously-running code) and was temporarily disabled (`#define TOUCH_ENABLED 0`) with heap-usage diagnostic logging added in parallel. **Resolution: the actual cause was insufficient power from a PC USB port** — switching to external power resolved it completely. Touch was never at fault.

**File/function:** `ESP32_S3_FanController_2inch.ino`

**Exact code (current state, re-enabled):**
```cpp
// --- Diagnostic toggle: overnight lockup investigation - RESOLVED ---
// The lockup was traced to insufficient power (PC USB port couldn't
// sustain the LCD + Ethernet + SD card load) - not touch/I2C, which was
// the original lead suspect. Confirmed resolved by switching to external
// power. Touch re-enabled; the diagnostic heap logging further down in
// this file is left in place as ongoing health telemetry rather than
// removed, since it's cheap and still useful.
#define TOUCH_ENABLED 1
```

The heap-logging diagnostic (every 60s, in `loop()`) was **kept** rather than removed, as ongoing health telemetry:
```cpp
// --- DIAGNOSTIC: heap logging (every 60s) ---
static unsigned long lastHeapLog = 0;
const unsigned long HEAP_LOG_PERIOD_MS = 60000;
if (millis() - lastHeapLog >= HEAP_LOG_PERIOD_MS) {
    lastHeapLog = millis();
    Serial.print("DIAGNOSTIC: uptime=");
    Serial.print(millis() / 1000);
    Serial.print("s  freeHeap=");
    Serial.print(ESP.getFreeHeap());
    Serial.print("  minFreeHeap=");
    Serial.println(ESP.getMinFreeHeap());
}
```

**Decisions made:** Keep the heap logging permanently rather than strip it out now that the mystery is solved — it's cheap and provides ongoing visibility. **Practical note for whoever maintains the deployed hardware long-term:** confirm the actual power supply used in production (not just dev bench setup) can sustain LCD + Ethernet + SD simultaneously, especially during SD writes.

**Tested:** Yes — confirmed resolved via external power; no further lockups observed after the switch.

---

## 2. NTP Sync Reliability

**What & why:** NTP requests reliably failed on attempt 1 after every boot, on both boards tested. Root cause: WizNet-chip Ethernet libraries can silently drop the very first UDP packet sent to a new destination, because the chip needs to resolve the gateway's MAC address via ARP before it can actually transmit, and the library doesn't automatically retry the send once ARP resolves. Confirmed via log timestamps: attempt 1 fails, attempt 2 (300ms later) succeeds immediately.

**File/function:** `network.cpp` — `getNtpTime()`, `sendNTPpacket()`

**Exact code:**
```cpp
static void sendNTPpacket(const char* address) {
    memset(packetBuffer, 0, NTP_PACKET_SIZE);
    packetBuffer[0]  = 0b11100011; // LI, Version, Mode
    packetBuffer[1]  = 0;          // Stratum
    packetBuffer[2]  = 6;          // Polling interval
    packetBuffer[3]  = 0xEC;       // Peer clock precision
    packetBuffer[12] = 49;
    packetBuffer[13] = 0x4E;
    packetBuffer[14] = 49;
    packetBuffer[15] = 52;

    // beginPacket() returns 0 if it couldn't allocate/ready a socket for
    // this send (e.g. W5500 socket pool exhausted) - worth knowing before
    // blaming the network path for an NTP timeout.
    if (!Udp.beginPacket(address, 123)) {
        Serial.println("NTP: beginPacket() failed - no free UDP socket?");
        return;
    }
    Udp.write(packetBuffer, NTP_PACKET_SIZE);
    Udp.endPacket();
}

time_t getNtpTime() {
    const int MAX_ATTEMPTS = 3;

    for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
        while (Udp.parsePacket() > 0) ; // flush stale packets

        Serial.print("Requesting NTP time (attempt ");
        Serial.print(attempt); Serial.print("/"); Serial.print(MAX_ATTEMPTS); Serial.println(")...");
        sendNTPpacket(ntpServerName);

        uint32_t beginWait = millis();
        while (millis() - beginWait < 1500) {
            int size = Udp.parsePacket();
            if (size >= NTP_PACKET_SIZE) {
                Udp.read(packetBuffer, NTP_PACKET_SIZE);

                unsigned long secsSince1900 =
                    ((unsigned long)packetBuffer[40] << 24) |
                    ((unsigned long)packetBuffer[41] << 16) |
                    ((unsigned long)packetBuffer[42] << 8)  |
                     (unsigned long)packetBuffer[43];

                unsigned long secsSince1970 = secsSince1900 - 2208988800UL;
                Serial.print("NTP sync successful on attempt "); Serial.println(attempt);
                return secsSince1970 + (config.tzOffset * 3600);
            }
        }

        // A dropped first UDP packet (e.g. the W5500 needing to resolve the
        // gateway's MAC via ARP before it can actually send) is a known
        // failure mode on WizNet-chip Ethernet libraries - a short pause
        // before retrying gives that time to settle rather than failing
        // outright on attempt 1.
        if (attempt < MAX_ATTEMPTS) delay(300);
    }

    Serial.println("NTP sync timed out after all attempts.");
    return 0;
}
```

**Decisions made:** 3-attempt retry with a 300ms gap, rather than a single attempt with a longer timeout — this specifically targets the ARP-resolution window rather than just waiting longer for the same doomed packet. **Also ruled out during diagnosis, in order:** DNS resolution (bypassed entirely by testing with a raw IP — confirmed not the cause), W5500 socket exhaustion for the UDP socket specifically (confirmed via explicit success/failure logging on `Udp.begin()` — the socket opened fine), gateway misconfiguration (confirmed correct by the user). **`ntpServerName` is currently hardcoded to Cloudflare's NTP IP (`162.159.200.1`)** as a diagnostic leftover — flagged as an open question (see section 20) on whether to revert to `"pool.ntp.org"` now that DNS was proven not to be the issue.

**Tested:** Yes, confirmed via log timestamps on real hardware — attempt 2 succeeding immediately after attempt 1's failure, matching the ARP-timing theory exactly.

---

## 3. LCD Clock Source Fix

**What & why:** The LCD clock used `getLocalTime()` (from `<time.h>`), which reads the ESP32 core's own **built-in SNTP client** — a completely separate clock system from `TimeLib`, which is what the web server and the NTP retry logic (section 2) actually feed via `setSyncProvider(getNtpTime)`. The firmware never calls `configTime()`, so `getLocalTime()` was permanently unfed — the LCD clock would show "syncing..." forever even after `TimeLib`'s own sync was working correctly.

**File/function:** `display.cpp` — `updateMainDashboardUI()` (clock rendering section)

**Exact code (current, using `TimeLib` accessors instead of `getLocalTime()`):**
```cpp
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
```

**Decisions made:** Switch the LCD to `TimeLib`'s own accessors (`hour()`, `minute()`, `hourFormat12()`, `isAM()`, `timeStatus()`) rather than also calling `configTime()` to feed the separate SNTP system. **Rejected:** running two parallel NTP mechanisms (the existing custom retry-hardened one, plus the ESP32 core's built-in SNTP) — redundant network traffic and a second thing that could independently fail, for no benefit once one clock source is shared everywhere.

**Tested:** Yes, confirmed on real hardware — LCD clock populates correctly once NTP sync completes.

---

## 4. Home Assistant Connection Logging

**What & why:** The original `fetchHomeAssistantTemperature()` printed `"Connecting to HA (...)..."` unconditionally on every single 2-second cycle, with **no confirmation ever printed on the success path**. A perfectly healthy connection looked, in the log, like it was stuck retrying forever. Separately, the GET/parse stage (pulling the network sensor back) had a completely silent failure path — if the TCP connect succeeded but the JSON parse or entity lookup failed, nothing was ever logged.

**File/function:** `home_assistant.cpp` — `fetchHomeAssistantTemperature()`

**Exact code (relevant excerpt — full function is in section 19's file dump, but this is the changed logic):**
```cpp
void fetchHomeAssistantTemperature() {
    // Only log state *transitions* (first success, connection lost, pull
    // failing even though the connection succeeded) plus an infrequent
    // heartbeat - not every single 2s cycle regardless of outcome.
    static bool lastConnectOk = false;
    static bool lastFullSyncOk = false;
    static unsigned long lastHeartbeatMs = 0;
    const unsigned long HEARTBEAT_PERIOD_MS = 300000; // 5 min

    EthernetClient client;

    if (!client.connect(config.haHost, config.haPort)) {
        if (lastConnectOk) {
            Serial.print("HA connection lost ("); Serial.print(config.haHost);
            Serial.print(":"); Serial.print(config.haPort); Serial.println(") - now failing.");
        }
        lastConnectOk = false;
        lastFullSyncOk = false;
        networkSensorHealthy = false;
        return;
    }

    if (!lastConnectOk) {
        Serial.print("Connected to HA ("); Serial.print(config.haHost);
        Serial.print(":"); Serial.print(config.haPort); Serial.println(") successfully.");
    }
    lastConnectOk = true;

    // ... (POST telemetry, GET network sensor - unchanged from original) ...

    if (pullOk) {
        if (!lastFullSyncOk) {
            Serial.print("HA sync fully OK - network temp = "); Serial.print(networkTempC, 1); Serial.println("C");
        } else if (millis() - lastHeartbeatMs >= HEARTBEAT_PERIOD_MS) {
            lastHeartbeatMs = millis();
            Serial.print("HA sync heartbeat: OK, network temp = "); Serial.print(networkTempC, 1); Serial.println("C");
        }
        lastFullSyncOk = true;
    } else {
        if (lastFullSyncOk) {
            Serial.println("HA connection is fine, but the network-temp pull just started failing (bad entity/parse?).");
        }
        lastFullSyncOk = false;
    }
}
```

**Decisions made:** Log only state transitions (first success, connection lost, pull-specifically-failing) plus a 5-minute heartbeat, rather than every cycle or nothing at all. This also newly distinguishes "TCP connect failed" from "connected fine, but the GET/parse failed" — previously indistinguishable failure modes.

**Tested:** Yes, confirmed via real serial logs matching the expected pattern (one "Connected successfully," then silence, then periodic heartbeats).

**Known follow-up issue (unresolved):** the user later reported the "Connected successfully" message printing even when the connection apparently isn't fully working — flagged but not root-caused; see Open Questions.

---

## 5. Sketch Filename / Build-Info Fix

**What & why:** Two related bugs in the web page's "Firmware Build Info" section. (a) `__FILE__` inside `web_server.cpp` reports `web_server.cpp`'s own path, not the main sketch's filename — `__FILE__` is per-translation-unit, it cannot see the main `.ino`'s name from another file. (b) Separately, `__DATE__`/`__TIME__` are baked in whenever the file containing them is *actually recompiled*, not whenever you upload — Arduino's incremental build reuses an unchanged file's object code, so the Build Time can silently lag behind the real flash time by hours if only other files were being edited that session.

**File/function:** `ESP32_S3_FanController_2inch.ino` (defines the constant), `web_server.h` (extern declaration), `web_server.cpp` (usage)

**Exact code — `.ino`:**
```cpp
// __FILE__ only reports each module's own filename when used inside a .cpp
// file - it can't see the main sketch's name from web_server.cpp or anywhere
// else. Defined once, here, at the actual source of truth; web_server.cpp
// displays this instead of trying to derive it via __FILE__.
// Update this string if the sketch is ever renamed.
const char* SKETCH_FILENAME = "ESP32_S3_FanController_2inch.ino";
```

**Exact code — `web_server.h`:**
```cpp
// Defined in the main .ino - see the comment there for why this exists
// instead of just using __FILE__ inside web_server.cpp.
extern const char* SKETCH_FILENAME;
```

**Exact code — `web_server.cpp` (usage + the (b) explainer comment):**
```cpp
client.print("<strong>Source File:</strong> "); client.print(SKETCH_FILENAME); client.println("<br>");
// NOTE: __DATE__/__TIME__ are baked in whenever THIS file is actually
// compiled, not whenever you upload - Arduino's incremental build
// reuses this file's object file unchanged if this file itself hasn't
// been edited, even while other files (and the overall binary) get
// freshly rebuilt and flashed. If this timestamp looks stuck across
// several uploads, that's why - it means only other files changed in
// that stretch. This comment line is itself a trivial edit to force a
// fresh recompile right now.
client.print("<strong>Build Date:</strong> ");  client.print(__DATE__); client.println("<br>");
client.print("<strong>Build Time:</strong> ");  client.print(__TIME__); client.println("");
```

**Decisions made:** Define the filename once at its actual source of truth (the `.ino`) and reference it via `extern`, matching the pattern later reused for nothing else in this codebase (single instance of this pattern). No fix exists for (b) beyond documentation — it's an inherent Arduino build-system behavior, not something fixable in source.

**Tested:** Yes — confirmed the filename displays correctly; the stale-timestamp behavior was directly observed and diagnosed (not a bug, expected behavior once understood).

---

## 6. Home Assistant Two-Way Sync — Thresholds

**What & why:** `tMin`/`tMax` needed to sync bidirectionally between the web page and Home Assistant `input_number` helpers, without fighting each other or hammering the W5500's limited socket pool.

**File/function:** `config.h`/`.cpp` (new fields), `home_assistant.h`/`.cpp` (`fetchThresholdsFromHA()`, `pushThresholdsToHA()`, `fetchEntityFloatState()`, `pushEntityFloatState()`), `web_server.cpp` (`/submit` handler), `.ino` (60s poll timer)

**Exact code — new config fields (`config.h`):**
```cpp
char haTMinEntity[64]; // HA input_number entity ID, e.g. "input_number.fan_ctrl_01_tmin"
char haTMaxEntity[64]; // stores/polls in Celsius, matching tMin/tMax's internal units
```
Defaults (`config.cpp`): `"input_number.fan_ctrl_02_tmin"`, `"input_number.fan_ctrl_02_tmax"`.

**Exact code — `home_assistant.cpp`:**
```cpp
static bool fetchEntityFloatState(const char* entityId, float &outValue) {
    EthernetClient client;
    if (!client.connect(config.haHost, config.haPort)) return false;

    String route = "GET /api/states/" + String(entityId) + " HTTP/1.1";
    client.println(route);
    client.print("Host: "); client.println(config.haHost);
    client.print("Authorization: "); client.println(config.haToken);
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();

    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
    }
    String payload = "";
    while (client.available()) payload += (char)client.read();
    client.stop();

    JsonDocument doc;
    if (deserializeJson(doc, payload)) return false;

    String stateStr = doc["state"].as<String>();
    if (stateStr == "unknown" || stateStr == "unavailable" || stateStr.length() == 0) return false;

    outValue = stateStr.toFloat();
    return true;
}

static bool pushEntityFloatState(const char* entityId, float value) {
    EthernetClient client;
    if (!client.connect(config.haHost, config.haPort)) return false;

    JsonDocument doc;
    doc["entity_id"] = entityId;
    doc["value"] = value;
    String jsonPayload;
    serializeJson(doc, jsonPayload);

    client.println("POST /api/services/input_number/set_value HTTP/1.1");
    client.print("Host: "); client.println(config.haHost);
    client.print("Authorization: "); client.println(config.haToken);
    client.println("Content-Type: application/json");
    client.print("Content-Length: "); client.println(jsonPayload.length());
    client.println("Connection: close\r\n");
    client.println(jsonPayload);

    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
    }
    client.stop();
    return true;
}

void fetchThresholdsFromHA() {
    if (strlen(config.haTMinEntity) == 0 || strlen(config.haTMaxEntity) == 0) return;

    float haTMin, haTMax;
    bool gotMin = fetchEntityFloatState(config.haTMinEntity, haTMin);
    bool gotMax = fetchEntityFloatState(config.haTMaxEntity, haTMax);

    const float EPSILON = 0.05; // ignore float noise, only react to real changes
    bool changed = false;

    if (gotMin && fabs(haTMin - config.tMin) > EPSILON) {
        Serial.print("tMin changed via HA: "); Serial.print(config.tMin);
        Serial.print(" -> "); Serial.println(haTMin);
        sdLogEvent("CONFIG", "source=HA field=tMin old=" + String(config.tMin, 1) + "C new=" + String(haTMin, 1) + "C");
        config.tMin = haTMin;
        changed = true;
    }
    if (gotMax && fabs(haTMax - config.tMax) > EPSILON) {
        Serial.print("tMax changed via HA: "); Serial.print(config.tMax);
        Serial.print(" -> "); Serial.println(haTMax);
        sdLogEvent("CONFIG", "source=HA field=tMax old=" + String(config.tMax, 1) + "C new=" + String(haTMax, 1) + "C");
        config.tMax = haTMax;
        changed = true;
    }

    if (changed) saveSettings();
}

void pushThresholdsToHA() {
    if (strlen(config.haTMinEntity) > 0) pushEntityFloatState(config.haTMinEntity, config.tMin);
    if (strlen(config.haTMaxEntity) > 0) pushEntityFloatState(config.haTMaxEntity, config.tMax);
}
```

**Exact code — web form trigger (`web_server.cpp`, inside the `/submit` handler):**
```cpp
if (tminStr.length() > 0 && tmaxStr.length() > 0) {
    float oldTMin = config.tMin;
    float oldTMax = config.tMax;
    float parsedMin = tminStr.toFloat();
    float parsedMax = tmaxStr.toFloat();
    if (submittedAsFahrenheit) {
        config.tMin = (parsedMin - 32.0) * 5.0 / 9.0;
        config.tMax = (parsedMax - 32.0) * 5.0 / 9.0;
    } else {
        config.tMin = parsedMin;
        config.tMax = parsedMax;
    }
    if (fabs(config.tMin - oldTMin) > 0.05) {
        sdLogEvent("CONFIG", "source=web field=tMin old=" + String(oldTMin, 1) + "C new=" + String(config.tMin, 1) + "C");
    }
    if (fabs(config.tMax - oldTMax) > 0.05) {
        sdLogEvent("CONFIG", "source=web field=tMax old=" + String(oldTMax, 1) + "C new=" + String(config.tMax, 1) + "C");
    }
    pushThresholdsToHA(); // immediate push, prevents the 60s poll reverting the web change
}
```

**Exact code — poll timer (`.ino`, `loop()`):**
```cpp
// --- HA-editable tMin/tMax poll (every 60s) ---
// Deliberately slow - these change rarely, and every poll costs 2 more
// W5500 socket open/close cycles on top of the 2s telemetry cycle's own
// socket usage. A shorter interval here (originally 15s) is suspected
// to have reintroduced the same W5500 socket-contention issue that
// caused the original HA-outage bug.
static unsigned long lastThresholdPoll = 0;
const unsigned long HA_THRESHOLD_POLL_MS = 60000;
if (millis() - lastThresholdPoll >= HA_THRESHOLD_POLL_MS) {
    lastThresholdPoll = millis();
    fetchThresholdsFromHA();
}
```

**Decisions made:** Polling interval started at 15s, was raised to 60s after real-world testing showed it reintroduced W5500 socket contention (a prior, separately-diagnosed outage). Device pushes immediately on web-form change so HA's stored value doesn't fight the next poll. **Rejected:** keeping the 15s interval — directly caused loop stutter and an intermittent empty network-temperature bar on the LCD in testing, traced to socket contention with the 2s telemetry cycle.

**Tested:** Yes, extensively — user confirmed both directions (web→HA and HA→web) working correctly after the interval fix.

---

## 7. Manual Fan Override — Core State & Fan-Curve Integration

**What & why:** A manual override mechanism was needed so the fan speed can be forced to a specific duty cycle from any control surface (LCD, web, HA), while never persisting across reboot and never overriding the sensor-loss failsafe.

**File/function:** `sensors.h` (extern declarations), `sensors.cpp` (`calculateFanCurve()`)

**Exact code — `sensors.h`:**
```cpp
// Manual fan-speed override (bookmarked feature: touch UI framework).
// Per the boot-behavior rule, this always starts false - override never
// persists across reboot, and the sensor-loss failsafe in
// evaluateSensorFailsafes() takes priority over it regardless of state.
extern bool manualOverrideActive;
extern int manualOverrideDutyCycle; // 0-255, only meaningful when active
```

**Exact code — `sensors.cpp` (global definitions):**
```cpp
bool manualOverrideActive = false;
int manualOverrideDutyCycle = 255; // defaults to full speed per spec, when engaged
```

**Exact code — `sensors.cpp`, `calculateFanCurve()` (the override-priority check added at the top):**
```cpp
void calculateFanCurve(float targetTemp) {
    int targetDuty = 0;

    // Manual override takes priority over the auto curve, per spec - but
    // NOT over the total-sensor-blackout failsafe, which forces full duty
    // directly in evaluateSensorFailsafes() without going through this
    // function at all, so that path is unaffected by override state.
    if (manualOverrideActive) {
        targetDuty = manualOverrideDutyCycle;
    } else if (config.tMax <= config.tMin) {
        targetDuty = 255; // corrupt thresholds -> fail safe to full power
    } else if (targetTemp < config.tMin) {
        targetDuty = 0;
    } else if (targetTemp >= config.tMax) {
        targetDuty = 255;
    } else {
        targetDuty = (int)(51.0 + ((targetTemp - config.tMin) / (config.tMax - config.tMin)) * 204.0);
    }

    for (int i = 0; i < NUM_FANS; i++) {
        if (i < config.fanCount && pwmPins[i] >= 0) {
            currentDutyCycles[i] = targetDuty;
            ledcWrite(pwmPins[i], targetDuty);
        } else {
            currentDutyCycles[i] = 0;
            if (pwmPins[i] >= 0) ledcWrite(pwmPins[i], 0);
        }
    }
}
```

**Decisions made:** Override state lives in `sensors.h`/`.cpp` as plain globals, **deliberately never added to the `SystemConfig` struct** — this is the mechanism that guarantees it can never be persisted to LittleFS and therefore can never survive a reboot, satisfying the locked-in boot-behavior rule. The failsafe's total-blackout branch (in `evaluateSensorFailsafes()`) writes `ledcWrite()` directly and never calls `calculateFanCurve()` at all, so it's structurally immune to override state regardless of this change.

**Tested:** Yes — confirmed override correctly forces duty cycle, and confirmed (via manual testing pulling both sensors) that the blackout failsafe still wins regardless of override state.

---

## 8. Touch Input Framework

**What & why:** A CST816D touch driver was needed to support LCD interaction (Manual Control button, override overlay). No existing library was used — a minimal from-scratch I2C driver was written instead, to avoid an external dependency for a small, well-documented register set.

**File/function:** New files `touch.h`, `touch.cpp`

**Exact code — `touch.h` (full file):**
```cpp
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
```

**Exact code — `touch.cpp` (full file):**
```cpp
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
```

**Called from `.ino`:**
```cpp
#if TOUCH_ENABLED
touchInit();
#endif
// ... in loop():
#if TOUCH_ENABLED
static unsigned long lastTouchPoll = 0;
const unsigned long TOUCH_POLL_MS = 30;
if (millis() - lastTouchPoll >= TOUCH_POLL_MS) {
    lastTouchPoll = millis();
    handleTouchInput();
}
#endif
```

**Decisions made:** Minimal hand-rolled register-level driver over an existing CST816S/D Arduino library — the register set is small and well-documented across the CST8xx family, and this avoids a new dependency. Polled at 30ms rather than interrupt-driven via `PIN_TP_INT` — simpler, and 30ms is fast enough to feel responsive for slider dragging without hammering the I2C bus. **Explicitly flagged as unverified:** the raw-to-display coordinate transform is a best guess based on typical `setRotation(1)` behavior and has never been confirmed against the physical panel.

**Tested:** Partially. The touch subsystem was disabled for an extended period during the (ultimately power-related, not touch-related) lockup investigation. Since being re-enabled, the user reported it "working fine" for basic interaction (see section 9), but the coordinate-transform accuracy specifically has not been explicitly re-confirmed since re-enabling.

---

## 9. LCD Manual Override Overlay UI

**What & why:** A touch-driven UI was needed on the LCD for engaging/adjusting/disengaging manual override: tap the Manual Control button to engage (defaults to 100%), a full-screen overlay opens with a slider and Cancel/Keep-On buttons.

**File/function:** `display.cpp` — `drawOverrideOverlay()`, `drawOverlaySliderOnly()`, `handleTouchInput()`, `isOverlayOpen()`, plus guards added to `updateMainDashboardUI()` and `refreshBarsOnly()`

**Exact code (overlay geometry + drawing, current state):**
```cpp
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
            bool inSliderZone = pointInRect(tx, ty - 10, OVERLAY_SLIDER_X, OVERLAY_SLIDER_Y, OVERLAY_SLIDER_W, OVERLAY_SLIDER_H + 20);
            if (inSliderZone) {
                int rel = constrain(tx - OVERLAY_SLIDER_X, 0, OVERLAY_SLIDER_W);
                manualOverrideDutyCycle = map(rel, 0, OVERLAY_SLIDER_W, 0, 255);
                drawOverlaySliderOnly();
            } else if (!wasPressed) {
                if (pointInRect(tx, ty, OVERLAY_CANCEL_X, OVERLAY_BTN_Y, OVERLAY_BTN_W, OVERLAY_BTN_H)) {
                    manualOverrideActive = false;
                    overlayOpen = false;
                    pushOverrideSwitchToHA();
                    Serial.println("Override DEACTIVATED via LCD (Cancel)");
                    sdLogEvent("OVERRIDE", "source=LCD action=OFF (cancel)");
                } else if (pointInRect(tx, ty, OVERLAY_KEEPON_X, OVERLAY_BTN_Y, OVERLAY_BTN_W, OVERLAY_BTN_H)) {
                    overlayOpen = false;
                    pushOverrideSpeedToHA();
                    int pct = (manualOverrideDutyCycle * 100) / 255;
                    Serial.print("Override kept ON via LCD - speed="); Serial.print(pct); Serial.println("%");
                    sdLogEvent("OVERRIDE", "source=LCD action=SPEED (keep-on) speed=" + String(pct) + "%");
                }
            }
        }
    } else {
        if (pressed && !wasPressed) {
            if (pointInRect(tx, ty, CENTER_X, MANUAL_BTN_Y, CENTER_W, MANUAL_BTN_H)) {
                if (!manualOverrideActive) {
                    manualOverrideActive = true;
                    manualOverrideDutyCycle = 255;
                    overlayOpen = true;
                    drawOverrideOverlay();
                    pushOverrideSwitchToHA();
                    Serial.println("Override ACTIVATED via LCD - speed=100%");
                    sdLogEvent("OVERRIDE", "source=LCD action=ON speed=100%");
                } else {
                    manualOverrideActive = false;
                    pushOverrideSwitchToHA();
                    Serial.println("Override DEACTIVATED via LCD (tap while flashing)");
                    sdLogEvent("OVERRIDE", "source=LCD action=OFF (tap-while-flashing)");
                }
            }
        }
    }
    wasPressed = pressed;
}
```

**Guards added to prevent the overlay from being clobbered by the periodic redraw timers:**
```cpp
void updateMainDashboardUI() {
    if (overlayOpen) return; // overlay owns the screen while open
    // ...
}
void refreshBarsOnly() {
    if (overlayOpen) return; // overlay owns the screen while open
    // ...
}
```

**Decisions made:** Full-screen takeover rather than a true overlay-on-top-of-dashboard — with no off-screen framebuffer, a partial overlay would fight the 2s dashboard refresh and 200ms bar-flash refresh over the same pixels. **Rejected:** partial/dimmed overlay as originally mocked up — deemed too risky for tearing/flicker bugs given the existing periodic redraw timers, and explicitly deferred pending the (still-open) screen-flicker fix. Button push logic split into switch-only vs. speed-only (see section 11) to avoid a race condition discovered later.

**Tested:** Yes — user confirmed engage/disengage/adjust flow works correctly on real hardware after touch was re-enabled.

---

## 10. Web Dashboard Manual Override Control

**What & why:** The web dashboard needed its own override toggle + slider, functionally equivalent to the LCD overlay and kept in sync with it and HA.

**File/function:** `web_server.cpp` — new `/override_set` endpoint, new HTML/CSS/JS block

**Exact code — endpoint (`handleNativeWebTraffic()`):**
```cpp
if (req.indexOf("GET /override_set") != -1) {
    bool wasActive = manualOverrideActive;
    int wasDuty = manualOverrideDutyCycle;

    String activeParam = getUrlParam(req, "active=");
    String speedParam  = getUrlParam(req, "speed=");

    bool switchChanged = false;
    bool speedOnlyChanged = false;

    if (activeParam.length() > 0 && activeParam.charAt(0) == '1') {
        manualOverrideActive = true;
        manualOverrideDutyCycle = speedParam.length() > 0 ? constrain(speedParam.toInt(), 0, 255) : 255;
        switchChanged = true;
    } else if (activeParam.length() > 0 && activeParam.charAt(0) == '0') {
        manualOverrideActive = false;
        switchChanged = true;
    } else if (manualOverrideActive && speedParam.length() > 0) {
        manualOverrideDutyCycle = constrain(speedParam.toInt(), 0, 255);
        speedOnlyChanged = true;
    }

    if (switchChanged) pushOverrideSwitchToHA();
    else if (speedOnlyChanged) pushOverrideSpeedToHA();

    int pct = (manualOverrideDutyCycle * 100) / 255;
    if (manualOverrideActive != wasActive) {
        Serial.print("Override "); Serial.print(manualOverrideActive ? "ACTIVATED" : "DEACTIVATED");
        Serial.print(" via web - speed="); Serial.print(pct); Serial.println("%");
        sdLogEvent("OVERRIDE", String("source=web action=") + (manualOverrideActive ? "ON" : "OFF") + " speed=" + String(pct) + "%");
    } else if (manualOverrideDutyCycle != wasDuty) {
        Serial.print("Override SPEED changed via web: "); Serial.print(pct); Serial.println("%");
        sdLogEvent("OVERRIDE", "source=web action=SPEED speed=" + String(pct) + "%");
    }

    client.println("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\n");
    client.println("{\"ok\":true}");
    delay(1);
    client.stop();
    return;
}
```

**Bug found and fixed in this endpoint — "getUrlParam trailing-text" bug:**

*What & why:* Turning override **ON** worked (`?active=1&speed=255` — the `&` gives `getUrlParam()` a clean delimiter), but turning **OFF** silently never worked (`?active=0` with nothing after it — `getUrlParam()` grabs everything to the end of the raw HTTP request line, including the trailing `" HTTP/1.1"`, so `activeParam == "0"` was comparing against `"0 HTTP/1.1"` and always failing).

*Fix:* check the first character instead of exact string equality (see the `activeParam.charAt(0) == '1'` / `'0'` checks in the code above — this is the fixed version).

**Exact code — CSS (added to the existing `<style>` block):**
```css
.override-card{border:2px solid #dc3545; border-radius:6px; padding:15px; margin-bottom:15px; text-align:center;}
.override-btn{width:100%; padding:12px; font-weight:bold; font-size:15px; border-radius:4px; border:2px solid #dc3545; background:#ffffff; color:#dc3545; cursor:pointer;}
.override-btn.active-flash{animation:overrideFlash 1s step-start infinite;}
@keyframes overrideFlash{0%,100%{background:#dc3545; color:#ffffff;} 50%{background:#330000; color:#dc3545;}}
.override-slider-wrap{margin-top:12px; display:none;}
.override-slider-wrap input[type=range]{width:100%;}
```

**Exact code — JS:**
```js
let overrideDragging = false;

function updateOverrideUI(active, speed) {
  const btn = document.getElementById('overrideBtn');
  const wrap = document.getElementById('overrideSliderWrap');
  const slider = document.getElementById('overrideSlider');
  btn.dataset.active = active ? '1' : '0';
  btn.innerText = active ? 'Manual Override: ON (tap to disable)' : 'Manual Override (tap to enable)';
  btn.classList.toggle('active-flash', active);
  wrap.style.display = active ? 'block' : 'none';
  if (!overrideDragging) {
    slider.value = speed;
    document.getElementById('overrideSpeedLabel').innerText = Math.round(speed / 255 * 100) + '%';
  }
}

function toggleOverride() {
  const isActive = document.getElementById('overrideBtn').dataset.active === '1';
  const url = isActive ? '/override_set?active=0' : '/override_set?active=1&speed=255';
  fetch(url).then(() => fetchLiveTelemetry());
}

function onOverrideSliderInput(val) {
  overrideDragging = true;
  document.getElementById('overrideSpeedLabel').innerText = Math.round(val / 255 * 100) + '%';
}

function onOverrideSliderChange(val) {
  fetch('/override_set?speed=' + val).then(() => { overrideDragging = false; });
}
```

**Exact code — HTML (server-rendered with true initial state):**
```cpp
{
    int initialPct = (manualOverrideDutyCycle * 100) / 255;
    client.println("<div class='override-card'>");
    client.print("<button type='button' id='overrideBtn' class='override-btn");
    client.print(manualOverrideActive ? " active-flash" : "");
    client.print("' onclick='toggleOverride()' data-active='");
    client.print(manualOverrideActive ? "1" : "0");
    client.print("'>");
    client.print(manualOverrideActive ? "Manual Override: ON (tap to disable)" : "Manual Override (tap to enable)");
    client.println("</button>");
    client.print("<div id='overrideSliderWrap' class='override-slider-wrap' style='display:");
    client.print(manualOverrideActive ? "block" : "none");
    client.println(";'>");
    client.print("Fan Speed: <span id='overrideSpeedLabel'>"); client.print(initialPct); client.println("%</span>");
    client.print("<input type='range' id='overrideSlider' min='0' max='255' value='"); client.print(manualOverrideDutyCycle);
    client.println("' oninput='onOverrideSliderInput(this.value)' onchange='onOverrideSliderChange(this.value)'>");
    client.println("</div>");
    client.println("</div>");
}
```

**Decisions made:** Separate `/override_set` endpoint rather than reusing `/submit` — override is a live action, never persisted, and mixing it with the settings-form handler (which does persist and can trigger a reboot on network-setting changes) risked confusion. CSS `@keyframes` animation for the flash rather than JS-driven interval toggling — simpler, and the browser handles the timing natively.

**Tested:** Yes — including the OFF-bug diagnosis and fix, confirmed working afterward.

---

## 11. Home Assistant Two-Way Sync — Manual Override (incl. two race-condition fixes)

**What & why:** Override needed to sync with HA (`input_boolean` for on/off, `input_number` for speed), pushed instantly via HA automation rather than slow polling, with the device polling only as an infrequent safety net.

**File/function:** `config.h`/`.cpp` (new fields), `home_assistant.h`/`.cpp` (`fetchOverrideFromHA()`, `pushOverrideSwitchToHA()`, `pushOverrideSpeedToHA()`, `fetchEntityBoolState()`, `pushEntityBoolState()`), `.ino` (poll timer)

**Exact code — new config fields:**
```cpp
char haOverrideSwitchEntity[64]; // input_boolean entity - on/off mirrors manualOverrideActive
char haOverrideSpeedEntity[64];  // input_number entity (0-255) - mirrors manualOverrideDutyCycle
```
Defaults: `"input_boolean.fan_ctrl_02_override"`, `"input_number.fan_ctrl_02_override_speed"`.

**Exact code — full sync logic (`home_assistant.cpp`):**
```cpp
static bool fetchEntityBoolState(const char* entityId, bool &outValue) {
    EthernetClient client;
    if (!client.connect(config.haHost, config.haPort)) return false;

    String route = "GET /api/states/" + String(entityId) + " HTTP/1.1";
    client.println(route);
    client.print("Host: "); client.println(config.haHost);
    client.print("Authorization: "); client.println(config.haToken);
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();

    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
    }
    String payload = "";
    while (client.available()) payload += (char)client.read();
    client.stop();

    JsonDocument doc;
    if (deserializeJson(doc, payload)) return false;

    String stateStr = doc["state"].as<String>();
    if (stateStr != "on" && stateStr != "off") return false;

    outValue = (stateStr == "on");
    return true;
}

static bool pushEntityBoolState(const char* entityId, bool value) {
    EthernetClient client;
    if (!client.connect(config.haHost, config.haPort)) return false;

    JsonDocument doc;
    doc["entity_id"] = entityId;
    String jsonPayload;
    serializeJson(doc, jsonPayload);

    String route = String("POST /api/services/input_boolean/") + (value ? "turn_on" : "turn_off") + " HTTP/1.1";
    client.println(route);
    client.print("Host: "); client.println(config.haHost);
    client.print("Authorization: "); client.println(config.haToken);
    client.println("Content-Type: application/json");
    client.print("Content-Length: "); client.println(jsonPayload.length());
    client.println("Connection: close\r\n");
    client.println(jsonPayload);

    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
    }
    client.stop();
    return true;
}

void fetchOverrideFromHA() {
    if (strlen(config.haOverrideSwitchEntity) == 0) return;

    bool haOn;
    bool gotOn = fetchEntityBoolState(config.haOverrideSwitchEntity, haOn);
    if (!gotOn) return;

    float haSpeed;
    bool gotSpeed = (strlen(config.haOverrideSpeedEntity) > 0) &&
                    fetchEntityFloatState(config.haOverrideSpeedEntity, haSpeed);

    if (haOn != manualOverrideActive) {
        manualOverrideActive = haOn;
        if (haOn) {
            manualOverrideDutyCycle = gotSpeed ? constrain((int)haSpeed, 0, 255) : 255;
        }
        int pct = (manualOverrideDutyCycle * 100) / 255;
        Serial.print("Override "); Serial.print(haOn ? "ACTIVATED" : "DEACTIVATED");
        Serial.print(" via HA - speed="); Serial.print(pct); Serial.println("%");
        sdLogEvent("OVERRIDE", String("source=HA action=") + (haOn ? "ON" : "OFF") + " speed=" + String(pct) + "%");
    } else if (haOn && gotSpeed) {
        int newDuty = constrain((int)haSpeed, 0, 255);
        if (abs(newDuty - manualOverrideDutyCycle) > 2) {
            manualOverrideDutyCycle = newDuty;
            int pct = (manualOverrideDutyCycle * 100) / 255;
            Serial.print("Override SPEED changed via HA: "); Serial.print(pct); Serial.println("%");
            sdLogEvent("OVERRIDE", "source=HA action=SPEED speed=" + String(pct) + "%");
        }
    }
}

void pushOverrideSwitchToHA() {
    if (strlen(config.haOverrideSwitchEntity) > 0) pushEntityBoolState(config.haOverrideSwitchEntity, manualOverrideActive);
}

void pushOverrideSpeedToHA() {
    if (strlen(config.haOverrideSpeedEntity) > 0) pushEntityFloatState(config.haOverrideSpeedEntity, manualOverrideDutyCycle);
}
```

**Exact code — poll timer (`.ino`, safety-net only, 5 minutes):**
```cpp
// --- HA manual-override poll (every 5 min - safety-net fallback only) ---
// HA now pushes override changes to /override_set immediately via a
// rest_command automation (near-zero latency, unlike polling). This
// slow poll just catches the rare case where a push got missed.
static unsigned long lastOverridePoll = 30000; // starts offset from boot
const unsigned long HA_OVERRIDE_POLL_MS = 300000;
if (millis() - lastOverridePoll >= HA_OVERRIDE_POLL_MS) {
    lastOverridePoll = millis();
    fetchOverrideFromHA();
}
```

### Race Condition #1 (fixed, then found insufficient) — push ordering

**What happened:** Initially, a single combined `pushOverrideToHA()` pushed switch-state then speed as two sequential calls. HA's own automation (triggered by the switch-state change) would read whatever was *currently* in the speed helper — which, at that exact instant, hadn't been updated yet — and bounce a stale value back to the device. Reordering to push speed first, then switch, was tried as a fix.

### Race Condition #2 (the actual fix) — split into independent push functions

**What & why:** The reordering fix wasn't sufficient — a second, subtler race existed between HA's *own two automations* (one for the switch, one for the speed helper) interleaving unpredictably regardless of device-side send order, since HA processes them asynchronously. **Root design fix:** stop pushing both together at all. Split into two independent functions (`pushOverrideSwitchToHA()`, `pushOverrideSpeedToHA()` — shown above), called individually depending on what actually changed:
- Engage/disengage → push **switch only**. HA's own automation resets its speed helper to 255 itself (via the automation YAML, section 19) — the device never needs to push a speed value at that exact moment, removing the entire class of race.
- Genuine slider adjustment while already active → push **speed only**.

**This bug was diagnosed directly from SD event-log data** — two `OVERRIDE` log rows with identical timestamps (same second) showing a value flip, which was the concrete evidence that led to the "stop combining pushes" fix.

**Decisions made:** Eliminate the race by removing the redundant simultaneous push entirely, rather than trying to out-time HA's async automation processing. This is documented at length in `home_assistant.h`'s comments (see section 19's file dump) specifically so a future maintainer doesn't accidentally recombine these two functions.

**Tested:** Yes — confirmed via SD log data showing correct, single-value transitions after the fix, across LCD/web/HA-initiated engage-disengage-reengage sequences.

---

## 12. SD Card Data Logging (full subsystem)

**What & why:** Full data-logging spec: per-minute time-series, daily hi/lo rollups with retention, boot-reason + last-state recovery, storage safety, SD-absent graceful degradation.

**File/function:** New files `sd_logger.h`, `sd_logger.cpp` (461 lines) — full content below since this is an entirely new subsystem.

**Exact code — `sd_logger.h` (full file):**
```cpp
#ifndef SD_LOGGER_H
#define SD_LOGGER_H

#include <Arduino.h>

void sdLoggerInit();
void sdLoggerLoop();
void sdLoggerUpdateSnapshot();
void sdLogEvent(const String &category, const String &description);
bool isSdCardPresent();
bool isSpilloverNearFull();

#endif // SD_LOGGER_H
```
*(Comments omitted here for brevity — full commented version is what's on disk; see the doc-comments already reproduced in sections 1b/13/14 above for the exact wording.)*

**Exact code — `sd_logger.cpp`, RTC snapshot (survives resets, not cold boots):**
```cpp
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
};

static const uint32_t SNAPSHOT_MAGIC = 0xFA57C0DE;
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
```

**Exact code — storage/spillover core:**
```cpp
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

static bool sdHasFreeSpace() {
    if (!sdPresent) return false;
    uint64_t total = SD.totalBytes();
    uint64_t used = SD.usedBytes();
    if (total == 0) return false;
    return (total - used) > MIN_FREE_BYTES;
}

// appendToSpillover(), appendLine(), sdLogEvent(), isSdCardPresent(),
// isSpilloverNearFull(), drainSpilloverToSD() - see section 1b for the
// current (LittleFS-guarded) versions of these functions.
```

**Exact code — daily hi/lo rollup:**
```cpp
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
```

**Bug found and fixed — `FILE_WRITE` appends, does not truncate:**

*What & why:* ESP32's SD library's `FILE_WRITE` mode **appends** rather than truncating, contrary to what the name suggests. Both the never-purged all-time record and the 30-day purge rewrite need true overwrite semantics — without this fix, both would have silently grown forever with duplicate/stale rows instead of replacing content.

*Fix — `updateAllTimeRecord()`:*
```cpp
static void updateAllTimeRecord(const DailyExtremes &finalizedDay) {
    DailyExtremes allTime = finalizedDay;

    if (sdPresent && SD.exists("/rollups/alltime.csv")) {
        File f = SD.open("/rollups/alltime.csv", FILE_READ);
        if (f) {
            String line = f.readStringUntil('\n');
            f.close();
            int idx = line.indexOf(',');
            if (idx != -1) {
                String rest = line.substring(idx + 1);
                String work = rest;
                float* floatTargets[6] = {&allTime.localMin, &allTime.localMax, &allTime.netMin, &allTime.netMax, &allTime.blendMin, &allTime.blendMax};
                for (int field = 0; field < 6; field++) {
                    int c = work.indexOf(',');
                    String tok = (c == -1) ? work : work.substring(0, c);
                    *floatTargets[field] = tok.toFloat();
                    if (c == -1) break;
                    work = work.substring(c + 1);
                }
                long* longTargets[4] = {&allTime.fan1Min, &allTime.fan1Max, &allTime.fan2Min, &allTime.fan2Max};
                for (int field = 0; field < 4; field++) {
                    int c = work.indexOf(',');
                    String tok = (c == -1) ? work : work.substring(0, c);
                    *longTargets[field] = tok.toInt();
                    if (c == -1) break;
                    work = work.substring(c + 1);
                }
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
        // must remove first for a true overwrite.
        SD.remove("/rollups/alltime.csv");
        File out = SD.open("/rollups/alltime.csv", FILE_WRITE);
        if (out) {
            out.print("ALL," + extremesToCsvFields(allTime) + "\n");
            out.close();
        }
    }
}
```

*Fix — `purgeOldDailyRows()` (30-day retention, ISO-date string comparison since ISO dates sort correctly as plain strings):*
```cpp
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
```

**Bug found and fixed — daily rollup date off-by-one:**

*What & why:* `finalizeDailyRollup()` runs right *after* the day has already rolled over (detected by comparing `day()` to the previous loop's value). Reading "today's" date at that exact moment gives the **new** day, not the day whose data is being finalized — mislabeling yesterday's hi/lo extremes with today's date.

*Fix:*
```cpp
static void finalizeDailyRollup() {
    if (!today.anySample) return;

    // This runs right after the day has already rolled over, so reading
    // the live date would incorrectly label yesterday's accumulated data
    // with today's date. Back up ~12h from "now" to land safely in
    // yesterday regardless of exactly when this fires relative to midnight.
    time_t yesterdayTime = now() - 12UL * 3600;
    char buf[12];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year(yesterdayTime), month(yesterdayTime), day(yesterdayTime));
    String rollupDate(buf);

    String row = rollupDate + "," + extremesToCsvFields(today) + "\n";
    appendLine("/rollups/daily.csv", row);
    updateAllTimeRecord(today);
    purgeOldDailyRows();

    pushDailyRollupToHA(rollupDate,
                        today.localMin, today.localMax,
                        today.netMin, today.netMax,
                        today.blendMin, today.blendMax,
                        today.fan1Min, today.fan1Max,
                        today.fan2Min, today.fan2Max);
}
```

**Exact code — init + loop:**
```cpp
void sdLoggerInit() {
    pinMode(PIN_SD_CS, OUTPUT);
    digitalWrite(PIN_SD_CS, HIGH);

    sdPresent = SD.begin(PIN_SD_CS, getDisplaySPI(), 4000000);
    if (sdPresent) {
        SD.mkdir("/logs");
        SD.mkdir("/rollups");
        Serial.println("SD card mounted.");
    } else {
        Serial.println("SD card not detected at boot - logging will spill to internal flash until it's inserted.");
    }

    resetDailyExtremes();

    String reason = resetReasonString();
    String desc = "reason=" + reason;

    if (esp_reset_reason() != ESP_RST_POWERON && rtcSnapshot.magic == SNAPSHOT_MAGIC) {
        desc += " | lastState: blend=" + String(rtcSnapshot.blendedAverageC, 1) + "C";
        desc += " fan1=" + String(rtcSnapshot.fan1RPM) + "rpm fan2=" + String(rtcSnapshot.fan2RPM) + "rpm";
        desc += " override=" + String(rtcSnapshot.manualOverrideActive ? "ON" : "off");
        desc += " localOK=" + String(rtcSnapshot.localSensorHealthy ? "y" : "n");
        desc += " netOK=" + String(rtcSnapshot.networkSensorHealthy ? "y" : "n");
        desc += " uptimeAtReset=" + String(rtcSnapshot.uptimeMs / 1000) + "s";
    } else {
        desc += " | no prior state available (cold boot or RTC memory invalid)";
    }

    Serial.print("Boot event: "); Serial.println(desc);
    sdLogEvent("BOOT", desc);

    sdLoggerUpdateSnapshot();
}

void sdLoggerLoop() {
    if (timeStatus() == timeNotSet) return;

    static unsigned long lastMinuteLogMs = 0;
    const unsigned long MINUTE_LOG_PERIOD_MS = 60000;
    if (millis() - lastMinuteLogMs >= MINUTE_LOG_PERIOD_MS) {
        lastMinuteLogMs = millis();
        String monthPath = "/logs/" + String(year()) + "-" + (month() < 10 ? "0" : "") + String(month()) + ".csv";
        String row = timestampNow() + "," +
                     String(localTempC, 1) + "," +
                     String(networkTempC, 1) + "," +
                     String(blendedAverageC, 1) + "," +
                     String(currentRPMs[0]) + "," +
                     String(currentRPMs[1]) + "\n";
        appendLine(monthPath.c_str(), row);
        updateDailyExtremes();
    }

    int currentDay = day();
    if (lastLoggedDay == -1) {
        lastLoggedDay = currentDay;
    } else if (currentDay != lastLoggedDay) {
        finalizeDailyRollup();
        resetDailyExtremes();
        lastLoggedDay = currentDay;
    }

    static unsigned long lastSdRetryMs = 0;
    const unsigned long SD_RETRY_PERIOD_MS = 60000;
    if (!sdPresent && millis() - lastSdRetryMs >= SD_RETRY_PERIOD_MS) {
        lastSdRetryMs = millis();
        if (SD.begin(PIN_SD_CS, getDisplaySPI(), 4000000)) {
            sdPresent = true;
            SD.mkdir("/logs");
            SD.mkdir("/rollups");
            drainSpilloverToSD();
        }
    }
}
```

**Related fix in `display.cpp`, shared SPI bus (needed for SD to work at all):**

*What & why:* The LCD/SD shared SPI bus was originally initialized without a MISO line — fine for the LCD (write-only), fatal for SD reads, which need MISO. Omitting it would leave SD reads silently broken even if writes appeared to work.

```cpp
// MISO (PIN_SD_MISO) is wired even though the LCD itself never reads
// data back - the SD card shares this bus and needs it. Omitting it
// here would leave the bus's MISO line unconfigured, and SD reads
// would silently fail even though writes might appear to work.
LcdSPI.begin(PIN_LCD_SCLK, PIN_SD_MISO, PIN_LCD_MOSI, -1);
```
(Was: `LcdSPI.begin(PIN_LCD_SCLK, -1, PIN_LCD_MOSI, -1);`)

**Decisions made:** Monthly CSVs kept indefinitely (no purge) for raw time-series; 30-day retention + all-time record for daily rollups. SD-absent spillover capped at 200KB on internal LittleFS, draining back automatically. Free-space check (5MB margin) before any SD write. **Rejected:** any attempt to make the spillover mechanism perfectly correct across month boundaries during a card-out — accepted as a known, documented limitation for what's meant to be a brief-outage safety net, not a long-term storage solution.

**Tested:** Yes, extensively, against real hardware and real recovered card data — boot events, RTC snapshot recovery across an actual reset, and the monthly time-series file were all confirmed correct from an actual card pull.

---

## 13. Config-Change History Logging

**What & why:** `tMin`/`tMax` edits needed to log to SD with source attribution, matching the pattern already built for override events. (Full code for this is already shown inline in section 6 — the `sdLogEvent("CONFIG", ...)` calls in both `web_server.cpp`'s `/submit` handler and `home_assistant.cpp`'s `fetchThresholdsFromHA()`.)

**Decisions made:** 0.05°C float-noise tolerance before logging, so resubmitting the same form value doesn't spam identical-looking rows.

**Tested:** Yes — confirmed via real recovered SD event-log data showing correct old/new value chains across multiple reboots, which also incidentally proved LittleFS persistence was working (see section 1a).

---

## 14. SD / Network Status Indicators

**What & why:** SD card presence and network connectivity needed to be visible at a glance, since both fail silently otherwise (SD falls back to internal spillover with no visible sign; network loss just means the web page becomes unreachable).

**File/function:** `sd_logger.h`/`.cpp` (`isSdCardPresent()` — already shown in section 12), `network.h`/`.cpp` (`isEthernetConnected()`, `isNetworkConnected()`), `display.cpp` (`drawTitleBar()`), `web_server.cpp` (JSON field + status line)

**Exact code — `network.h`:**
```cpp
// True if the W5500's physical Ethernet link is up (checks the actual
// hardware link-detect state, not just whether an IP is assigned - with
// a static IP config, the IP would still show as "assigned" even with
// the cable unplugged, so link status is the more honest signal).
bool isEthernetConnected();

// True if ANY network path is up - currently just Ethernet, since Wi-Fi
// fallback (bookmark #5) isn't implemented in this codebase yet. Once it
// is, this should become `isEthernetConnected() || isWifiConnected()` -
// this function is the intended single call site for that.
bool isNetworkConnected();
```

**Exact code — `network.cpp`:**
```cpp
bool isEthernetConnected() {
    return Ethernet.linkStatus() == LinkON;
}

bool isNetworkConnected() {
    return isEthernetConnected();
}
```

**Exact code — `display.cpp`, `drawTitleBar()` (the two status dots):**
```cpp
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
```

**Exact code — `web_server.cpp` (SD status only — network status was explicitly NOT added to the web page, see decision below):**
```cpp
// JSON field:
json += "\"sd_present\":" + String(isSdCardPresent() ? "true" : "false") + ",";

// JS:
function updateSdStatus(present) {
  const el = document.getElementById('sdStatusText');
  if (!el) return;
  el.innerText = present ? 'OK' : 'Not present (buffering internally)';
  el.style.color = present ? '#28a745' : '#dc3545';
}

// HTML (server-rendered initial state):
bool sdOk = isSdCardPresent();
client.print("<div class='card-large' style='text-align:center;'>&#128190; <strong>SD Card:</strong> ");
client.print("<span id='sdStatusText' style='font-weight:bold; color:");
client.print(sdOk ? "#28a745" : "#dc3545");
client.print(";'>");
client.print(sdOk ? "OK" : "Not present (buffering internally)");
client.println("</span></div>");
```

**Decisions made:** SD status shown on **both** LCD and web. Network status shown on the **LCD only** — explicitly decided against adding a web-page network indicator, on the reasoning that if the network is down, there's no way to reach the web page to see the indicator in the first place. This was later revisited and explicitly re-confirmed as "LCD-only for now" but bookmarked as a possible future addition (see Open Questions / backlog). `isSpilloverNearFull()` (section 1b) exists but is **not currently wired into either indicator** — both are simple binary present/absent, not a three-state (OK / degraded / absent) indicator.

**Tested:** Yes, both indicators confirmed visually correct on real hardware.

---

## 15. Daily Rollup Push to Home Assistant

**What & why:** The daily hi/lo summary needed to reach HA once a day, separate from both the continuous 2s live telemetry and the raw SD log (which stays local per the original spec).

**File/function:** `home_assistant.h`/`.cpp` (`pushDailyRollupToHA()`), called from `sd_logger.cpp`'s `finalizeDailyRollup()` (already shown in section 12)

**Exact code — `home_assistant.h` declaration:**
```cpp
// Pushes the day's finalized hi/lo summary to HA as a dedicated entity
// (sensor.<nodeID>_daily_summary), separate from the continuous 2s live
// telemetry push - this is the once-a-day rollup, not the raw log. Call
// this from sd_logger.cpp right after a day's extremes are finalized.
// All temperature values are Celsius, matching internal storage.
void pushDailyRollupToHA(const String &date,
                          float localMinC, float localMaxC,
                          float netMinC, float netMaxC,
                          float blendMinC, float blendMaxC,
                          long fan1MinRpm, long fan1MaxRpm,
                          long fan2MinRpm, long fan2MaxRpm);
```

**Exact code — `home_assistant.cpp` implementation:**
```cpp
void pushDailyRollupToHA(const String &date,
                          float localMinC, float localMaxC,
                          float netMinC, float netMaxC,
                          float blendMinC, float blendMaxC,
                          long fan1MinRpm, long fan1MaxRpm,
                          long fan2MinRpm, long fan2MaxRpm) {
    EthernetClient client;
    if (!client.connect(config.haHost, config.haPort)) {
        Serial.println("Daily rollup push to HA failed - couldn't connect.");
        return;
    }

    JsonDocument doc;
    doc["state"] = date;
    JsonObject attrs = doc["attributes"].to<JsonObject>();
    attrs["local_min_c"] = String(localMinC, 1);
    attrs["local_max_c"] = String(localMaxC, 1);
    attrs["net_min_c"] = String(netMinC, 1);
    attrs["net_max_c"] = String(netMaxC, 1);
    attrs["blend_min_c"] = String(blendMinC, 1);
    attrs["blend_max_c"] = String(blendMaxC, 1);
    attrs["fan1_min_rpm"] = fan1MinRpm;
    attrs["fan1_max_rpm"] = fan1MaxRpm;
    attrs["fan2_min_rpm"] = fan2MinRpm;
    attrs["fan2_max_rpm"] = fan2MaxRpm;

    String jsonPayload;
    serializeJson(doc, jsonPayload);

    String route = "POST /api/states/sensor." + String(config.nodeID) + "_daily_summary HTTP/1.1";
    client.println(route);
    client.print("Host: "); client.println(config.haHost);
    client.print("Authorization: "); client.println(config.haToken);
    client.println("Content-Type: application/json");
    client.print("Content-Length: "); client.println(jsonPayload.length());
    client.println("Connection: close\r\n");
    client.println(jsonPayload);

    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
    }
    client.stop();
    Serial.println("Daily rollup summary pushed to HA.");
}
```

**Decisions made:** New dedicated entity (`sensor.<nodeID>_daily_summary`) rather than piggybacking on the existing live-telemetry entity — keeps the once-daily summary cleanly separate in HA's UI/history from the 2s live data.

**Tested:** Code-complete and wired in; **not yet confirmed with a real full day-cycle observation** at the time of this handoff (the daily rollover trigger only fires once per real calendar day, so this hasn't had a full natural test cycle yet — flagged as an open item).

---

## 16. LCD Dashboard Visual Overhaul

**What & why:** A large, multi-round iterative redesign of the LCD dashboard, covering bar rendering, layout repositioning, fonts, and the Manual Control button. This is the most heavily-iterated part of the whole project. Full current-state code for every function mentioned here is in the complete `display.cpp` dump (section 19).

### 16a. Bar gauges: banding → single-color threshold flash

**What & why:** Original design used continuous color-banding (blue→white→green→yellow→orange→red across the gauge range). Revised, per explicit request, to: single identity color per bar (cyan=fans, orange=local, magenta=network), flashing in that color at ≥90% of range, flashing **red** at ≥95%.

```cpp
static const float BAR_FLASH_THRESHOLD = 0.90;
static const float BAR_RED_THRESHOLD   = 0.95;

static bool resolveBarFill(float value, float minV, float maxV, uint16_t identityColor, uint16_t &outColor) {
    if (maxV <= minV) { outColor = identityColor; return true; }
    float pct = (value - minV) / (maxV - minV);
    if (pct < 0.0) pct = 0.0;
    if (pct >= BAR_RED_THRESHOLD) { outColor = ST77XX_RED; return blinkPhaseOn(); }
    if (pct >= BAR_FLASH_THRESHOLD) { outColor = identityColor; return blinkPhaseOn(); }
    outColor = identityColor;
    return true;
}
```
This required building a fast independent redraw timer (`refreshBarsOnly()`, called every 200ms from `.ino`, separate from the 2s full dashboard refresh) — without it, a flash would just crawl at 2s intervals instead of reading as an actual flash.

### 16b. Bar rendering bugs (two, both fixed)

**Bug 1 — outline drawn before fill:** originally `drawRoundRect()` (outline) was called *before* the square-cornered fill, so the fill's sharp corners visually poked past the rounded border. First fix attempt: draw outline *after* fill (masks a 1px stroke). **Still insufficient** — square fill corners still visible at the bottom corners of tall bars.

**Bug 2 (the actual fix) — fill itself made rounded:**
```cpp
if (fillH > 0) {
    // Rounded, not square, corners on the fill itself - a plain
    // fillRect has sharp corners that poke past the bar's rounded
    // outline right at the edges. Radius is clamped to half the
    // fill's own height so this stays safe even when the fill is
    // very short.
    int fillRadius = min(6, fillH / 2);
    screenMain.fillRoundRect(innerX, innerY + (innerH - fillH), innerW, fillH, fillRadius, fillColor);
}
```
Current `drawGaugeBar()` (full, both fixes combined):
```cpp
static void drawGaugeBar(int x, int y, int w, int h, uint16_t identityColor, bool hasValue, float value, float minV, float maxV) {
    screenMain.fillRoundRect(x, y, w, h, BAR_CORNER_RADIUS, ST77XX_BLACK);
    int innerX = x + BAR_OUTLINE_INSET;
    int innerY = y + BAR_OUTLINE_INSET;
    int innerW = w - 2 * BAR_OUTLINE_INSET;
    int innerH = h - 2 * BAR_OUTLINE_INSET;

    if (hasValue) {
        uint16_t fillColor;
        bool drawFillNow = resolveBarFill(value, minV, maxV, identityColor, fillColor);
        if (drawFillNow) {
            float clampedVal = constrain(value, minV, maxV);
            int fillH = (maxV > minV) ? (int)map((long)(clampedVal * 100), (long)(minV * 100), (long)(maxV * 100), 0, innerH) : 0;
            if (fillH > 0) {
                int fillRadius = min(6, fillH / 2);
                screenMain.fillRoundRect(innerX, innerY + (innerH - fillH), innerW, fillH, fillRadius, fillColor);
            }
        }
    }
    screenMain.drawRoundRect(x, y, w, h, BAR_CORNER_RADIUS, identityColor);
}
```

### 16c. Layout repositioning

Value labels moved above each bar (bare integers, no unit letter — see 16e); IP relocated under the Fan RPM column (yellow, no label); date relocated under the Temperature column (`YYYY/MM/DD` format, changed from `MM/DD/YY`); time-only clock at top of center column; five redundant center-column text rows (Fan1/Fan2/Local/Net/Average) **removed entirely** once the bar-top labels and big number made them redundant; column header labels nudged 6px right to fix a visual left-offset.

### 16d. Font upgrade

Two bundled Adafruit_GFX fonts added (no new library dependency — these ship with the already-used library):
```cpp
#include <Fonts/FreeSansBold24pt7b.h> // used for the big temp number
#include <Fonts/FreeSansBold18pt7b.h> // used for clock/F-C/button
```
A reusable helper measures real rendered size before committing to a font, always falling back safely:
```cpp
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
```
The big temperature number additionally attempts the 24pt font at **2x scale** first, cascading down through 1x, then classic-font fallbacks — see the full loop in `updateMainDashboardUI()` in section 19's file dump.

**Bug found and fixed — font-reset-before-print:** an early version of the button's unit-indicator fallback measured the 18pt font's size correctly, then called `setFont(NULL)` **before** actually printing — silently rendering the tiny classic font instead of the intended 18pt fallback, while the (correctly-sized) degree circle made the mismatch obvious. Fixed by keeping the font set through the actual `print()` call.

**Degree symbol:** Adafruit_GFX's default font has no `°` glyph at all (ASCII-only). Implemented as a small drawn circle (`screenMain.drawCircle()`) positioned as superscript, rather than pulling in a full extended-font library for one character.

### 16e. Manual Control button

Grew from a short single-line strip to a tall, three-row button (top edge fixed at y=150, bottom extended to near the screen edge): unit indicator (color-matched to the live temperature reading, own permanent black background band so it's never invisible if the button is also flashing red) on top, "Manual"/"Control" below. Full current code is in section 19's file dump (`drawManualControlButton()`).

**Decisions made throughout this whole section:** every font/size decision uses runtime `getTextBounds()` measurement with a safe fallback chain, rather than assuming exact glyph metrics — explicitly because exact bundled-font pixel dimensions can't be known without compiling against the real font data. **Rejected:** attempting to hardcode "the right" font size for each element based on assumed metrics — this approach was tried implicitly at first and abandoned once it became clear it couldn't be verified without live hardware feedback.

**Tested:** Extensively, over many iterative rounds, with the user confirming the final result "looks great." The touch coordinate transform used to interact with the (now much larger) button hit-target was not re-verified as part of this visual work specifically.

---

## 17. Web Dashboard Data-Honesty Fix

**What & why:** The web page's "HA Network" stat card and the `/ajax_data` JSON endpoint both printed `networkTempC`/`localTempC` **unconditionally**, with no check on sensor health — unlike the LCD, which correctly shows an empty/red-outlined bar when a sensor is unhealthy. When a sensor had never successfully reported (e.g., a misconfigured `haSensor` entity), the web page would confidently display the untouched `0.0` startup default as if it were a real reading.

**File/function:** `web_server.cpp` — both the `/ajax_data` handler and the initial page-render section

**Exact code:**
```cpp
// /ajax_data:
String localStr   = localSensorHealthy ? String(liveLocal, 1) : "--";
String networkStr = networkSensorHealthy ? String(liveNetwork, 1) : "--";
String blendedStr = (localSensorHealthy || networkSensorHealthy) ? String(liveBlended, 1) : "--";

// Initial page render:
String initialLocalStr   = localSensorHealthy ? String(initialLocal, 1) : "--";
String initialNetworkStr = networkSensorHealthy ? String(initialNetwork, 1) : "--";
String initialBlendedStr = (localSensorHealthy || networkSensorHealthy) ? String(initialBlended, 1) : "--";
```

**Decisions made:** Match the LCD's honesty convention (`"--"` for unhealthy) exactly, on both the initial server-rendered page and the live JSON poll.

**Tested:** Yes — this was the fix that led directly to correctly diagnosing a separate, real issue (a placeholder/breadboard `haSensor` entity that had never actually resolved), which the misleading `0.0` display had been masking.

---

## 18. Hardware Pin Remap

**What & why:** User requested a specific pin layout for easier wiring: PWM1=GPIO2 (unchanged), TACH1=GPIO4, PWM2=GPIO6, TACH2=GPIO16. Verified against the ESP32-S3's actual reserved-pin ranges (octal PSRAM/flash on GPIO26–37, USB D+/D− on 19/20, UART0 on 43/44, strapping pins 0/3/45/46) — none of the requested pins conflict.

**File/function:** `pins.h`

**Exact code (before → after):**
```cpp
// Before:
#define PWM1_PIN    2
#define PWM2_PIN    4
#define TACH1_PIN 6
#define TACH2_PIN 7

// After:
#define PWM1_PIN    2
#define PWM2_PIN    6
#define TACH1_PIN 4
#define TACH2_PIN 16
```
Spare-pins comment updated to match: `// GPIO7, 15, 17, 18, 21 remain unused - e.g. fan channels 3/4.`

**Decisions made:** No other files needed changes — confirmed `pwmPins[]`/`tachPins[]` in `sensors.cpp` are built entirely from these macros, with no raw pin numbers hardcoded elsewhere.

**Tested:** Not yet — this was a source-only change at the end of the session; no hardware rewiring/flash-and-verify has been reported yet.

---

## 19. Home Assistant YAML (not in this repo, but required for the firmware to work)

This isn't part of the Arduino codebase, but is required configuration on the HA side for sections 6/11/15 to function, and isn't tracked anywhere else:

```yaml
rest_command:
  fan_ctrl_02_set_override:
    url: "http://192.168.10.54/override_set?active={{ active }}&speed={{ speed }}"
    method: GET

automation:
  - alias: "Fan Ctrl 02 - Push Override Switch to Device"
    triggers:
      - entity_id: input_boolean.fan_ctrl_02_override
        trigger: state
    actions:
      - choose:
          - conditions:
              - condition: state
                entity_id: input_boolean.fan_ctrl_02_override
                state: "on"
            sequence:
              - action: input_number.set_value
                target:
                  entity_id: input_number.fan_ctrl_02_override_speed
                data:
                  value: 255
                # Deliberately NOT also calling rest_command here - this
                # set_value change triggers "Push Override Speed to Device"
                # on its own, with the value already fully settled. Calling
                # rest_command here too raced against that automation and
                # could send a stale value that arrived second - see
                # section 11 for the full race-condition history.
          - conditions:
              - condition: state
                entity_id: input_boolean.fan_ctrl_02_override
                state: "off"
            sequence:
              - action: rest_command.fan_ctrl_02_set_override
                data:
                  active: "0"
                  speed: "0"

  - alias: "Fan Ctrl 02 - Push Override Speed to Device"
    triggers:
      - entity_id: input_number.fan_ctrl_02_override_speed
        trigger: state
    conditions:
      - condition: state
        entity_id: input_boolean.fan_ctrl_02_override
        state: "on"
    actions:
      - action: rest_command.fan_ctrl_02_set_override
        data:
          active: "1"
          speed: "{{ states('input_number.fan_ctrl_02_override_speed') | int(255) }}"
```

Also required: `input_boolean.fan_ctrl_02_override`, `input_number.fan_ctrl_02_override_speed` (0–255), `input_number.fan_ctrl_02_tmin`, `input_number.fan_ctrl_02_tmax` helpers, and (optional, mentioned in an earlier session) `binary_sensor` template entities for fan fault status reading the `fan1_fault`/`fan2_fault` attributes already pushed by `fetchHomeAssistantTemperature()`.

**Where this lives:** per user confirmation, in `automations.yaml` (included via `automation: !include automations.yaml` in `configuration.yaml`), not inline in `configuration.yaml` — this was itself a whole diagnostic detour (duplicate top-level YAML key errors) documented in the session but not reproduced in full here since it's config, not code.

---

## 20. Open Questions

1. **NTP server hardcoded to a diagnostic IP.** `network.cpp`'s `ntpServerName` is currently `"162.159.200.1"` (Cloudflare, used to rule out DNS as a cause during the NTP debugging in section 2). DNS was conclusively ruled out. **Should this revert to `"pool.ntp.org"`, or is the hardcoded IP the preferred permanent state** (avoids a DNS dependency entirely, at the cost of relying on one specific server never changing)?

2. **HA "connected successfully" logging may still have a false-positive.** After the transition-based logging fix (section 4), the user separately reported seeing `"Connected to HA...successfully"` print even when the connection didn't seem to be fully working. This was **not root-caused** — flagged as needing investigation (likely: `client.connect()` succeeding at the TCP/socket level without the subsequent HTTP exchange actually completing, which the current code doesn't yet distinguish from genuine success).

3. **Touch coordinate transform is still unverified.** `touch.cpp`'s raw-to-display coordinate mapping is explicitly documented as a best guess. The user reports touch "working fine" for basic interaction since re-enabling it, but this specific accuracy claim (is the mapping exactly correct, or just close enough to hit large targets like the Manual Control button) has not been explicitly re-confirmed since the button grew much larger (section 16e) — a small, precise touch target elsewhere might reveal an offset that a big button wouldn't.

4. **Daily HA rollup push has not completed a real full-day test cycle.** Code-complete (section 15) but the day-rollover trigger only fires once per real calendar day; no confirmation yet that a real push has landed correctly in HA.

5. **Pin remap (section 18) not yet flash-and-verify tested** against actual rewired hardware.

6. **`isSpilloverNearFull()` exists but is unused.** Built as part of the SD status work (section 14) but never wired into either the LCD or web indicator — both are currently simple binary present/absent. Worth deciding whether a three-state indicator (OK / degraded-buffering / absent) is worth adding, or whether the binary state is sufficient.

7. **Large amount of planned-but-not-yet-built work exists only as verbal specification, not code**, and is explicitly out of scope for this handoff (which covers implemented changes only) — flagging so it isn't mistaken for "already decided and just needs coding" without a spec-review pass first: the full web UI restyle (tab framework, theme system with 4 presets, Home tab with editable notes block), Wi-Fi/BLE/DHCP-default/socket-health work, OTA, shared network backup, historical data viewer, and the fan-channel-checkbox redesign of the web dashboard.

8. **Screen redraw flicker is still unresolved.** Known cause (every redraw does a full clear-then-redraw with no dirty-checking), fix direction discussed (dirty-checking or an off-screen framebuffer) but not implemented.

9. **GitHub/local sketch-folder hygiene.** Multiple sessions were disrupted by stale/duplicated local files (causing linker errors, silently-reverted fixes, and at least one multi-message debugging detour that turned out to be testing against an outdated build). The user reported fixing their GitHub Desktop workflow partway through, but this is worth a final confirmation before treating any "already tested" claim in this document as validated against the *current* file set with full confidence.
