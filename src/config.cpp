#include "config.h"
#include "pins.h"
#include <FS.h>
#include <LittleFS.h>

// No secrets in the source: the MQTT login is set on the web page and kept in
// /settings.cfg on LittleFS. (An old HA token once lived here; revoked.)
SystemConfig config = {
    CONFIG_STRUCT_VERSION,
    IPAddress(192, 168, 10, DEFAULT_IP_LAST_OCTET), // per board, pins.h
    IPAddress(255, 255, 255, 0),
    IPAddress(192, 168, 10, 1),
    IPAddress(192, 168, 10, 11),
    26.7,
    37.8,
    true,
    -5,
    true,
    1,  // fanCount: fan 1 only; more are ticked on the Fan Control tab
    DEFAULT_NODE_ID,
    "", "", "", 0, // unused REST-era HA token/sensor/host/port
    300, 2200,   // fan RPM gauge display range
    60.0, 110.0, // temperature gauge display range, in F
    "", "", "", "", // unused HA helper entity IDs
    "192.168.10.85", // mqttBroker - Mosquitto add-on on the HA box
    1883,
    "",   // mqttUser - set via web UI
    "",   // mqttPass - set via web UI
    "",   // webUser - set via web UI
    "",   // webPass - set via web UI
    "America/Chicago",
    "CST6CDT,M3.2.0,M11.1.0",
    "", "",       // wifiSsid, wifiPass - set via web UI
    false,        // wifiStatic: DHCP
    0, 0, 0, 0,   // wifiIp, wifiGateway, wifiSubnet, wifiDns
    "",           // hostname: derived from the node ID
    "12345678",   // apPass (as on Wifi_Fan_Knob; change it on the WiFi tab)
    true          // ethDhcp: new boards take an address from the router
};

// Older settings files = this struct cut before the fields added since
// (offsetof, rounded up to the struct's 4-byte alignment): version 4 ends
// before the MQTT fields (864 bytes, checked against the old struct
// 2026-09-27), version 5 before the web login (1028 bytes), version 6
// before the time zone (1124 bytes), version 7 before WiFi (1236 bytes),
// version 8 before Ethernet DHCP.
// offsetof warns because IPAddress has virtual functions; GCC supports it.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
static const size_t V4_NEW_FROM = offsetof(SystemConfig, mqttBroker);
static const size_t V5_NEW_FROM = offsetof(SystemConfig, webUser);
static const size_t V6_NEW_FROM = offsetof(SystemConfig, tzName);
static const size_t V7_NEW_FROM = offsetof(SystemConfig, wifiSsid);
static const size_t V8_NEW_FROM = offsetof(SystemConfig, ethDhcp);
#pragma GCC diagnostic pop
static const size_t V4_FILE_SIZE = (V4_NEW_FROM + 3) & ~(size_t)3;
static const size_t V5_FILE_SIZE = (V5_NEW_FROM + 3) & ~(size_t)3;
static const size_t V6_FILE_SIZE = (V6_NEW_FROM + 3) & ~(size_t)3;
static const size_t V7_FILE_SIZE = (V7_NEW_FROM + 3) & ~(size_t)3;
static const size_t V8_FILE_SIZE = (V8_NEW_FROM + 3) & ~(size_t)3;

// LittleFS.begin()'s formatOnFail reliably reformats a *missing* filesystem,
// but doesn't always catch genuine on-disk *corruption* (e.g. littlefs
// error -84, "corrupted dir pair") - that failure mode was showing up as a
// permanent mount failure on every subsequent boot instead of self-healing.
// This wraps begin() with an explicit force-format-and-retry fallback so a
// corrupted partition recovers on its own (falling back to config defaults)
// instead of staying broken until someone notices and re-flashes/erases.
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

void saveSettings() {
    File f = LittleFS.open("/settings.cfg", "w");
    if (f) {
        f.write((uint8_t*)&config, sizeof(config));
        f.close();
        Serial.println("Settings saved.");
    } else {
        Serial.println("ERROR: could not open /settings.cfg for writing.");
    }
}

// Empties the REST-era fields (config.h). Returns true if any held data,
// e.g. an HA token from an older firmware, so the caller saves the wipe.
static bool clearUnusedFields() {
    bool had = config.unusedHaToken[0] || config.unusedHaSensor[0] || config.unusedHaHost[0] ||
               config.unusedHaPort != 0 || config.unusedHaTMinEntity[0] || config.unusedHaTMaxEntity[0] ||
               config.unusedHaOverrideSwitchEntity[0] || config.unusedHaOverrideSpeedEntity[0];
    memset(config.unusedHaToken, 0, sizeof(config.unusedHaToken));
    memset(config.unusedHaSensor, 0, sizeof(config.unusedHaSensor));
    memset(config.unusedHaHost, 0, sizeof(config.unusedHaHost));
    config.unusedHaPort = 0;
    memset(config.unusedHaTMinEntity, 0, sizeof(config.unusedHaTMinEntity));
    memset(config.unusedHaTMaxEntity, 0, sizeof(config.unusedHaTMaxEntity));
    memset(config.unusedHaOverrideSwitchEntity, 0, sizeof(config.unusedHaOverrideSwitchEntity));
    memset(config.unusedHaOverrideSpeedEntity, 0, sizeof(config.unusedHaOverrideSpeedEntity));
    return had;
}

void loadSettings() {
    if (!LittleFS.exists("/settings.cfg")) return;

    File f = LittleFS.open("/settings.cfg", "r");
    if (!f) return;

    // Start from the compiled-in defaults: an older file is shorter and
    // leaves the newer fields at their defaults.
    SystemConfig loaded = config;
    size_t fileSize = f.size();
    uint32_t fileVersion = fileSize == sizeof(loaded) ? CONFIG_STRUCT_VERSION
                         : fileSize == V8_FILE_SIZE ? 8
                         : fileSize == V7_FILE_SIZE ? 7
                         : fileSize == V6_FILE_SIZE ? 6
                         : fileSize == V5_FILE_SIZE ? 5
                         : fileSize == V4_FILE_SIZE ? 4 : 0;
    bool readOk = fileVersion != 0 && (f.read((uint8_t*)&loaded, fileSize) == fileSize) &&
                  loaded.configVersion == fileVersion;
    bool upgraded = false;
    if (readOk && fileVersion != CONFIG_STRUCT_VERSION) {
        // The file's tail padding may have landed on the first new bytes:
        // put the defaults of everything newer back (no IPAddress in there)
        size_t newFrom = fileVersion == 4 ? V4_NEW_FROM : fileVersion == 5 ? V5_NEW_FROM
                       : fileVersion == 6 ? V6_NEW_FROM : fileVersion == 7 ? V7_NEW_FROM : V8_NEW_FROM;
        memcpy((uint8_t*)&loaded + newFrom, (const uint8_t*)&config + newFrom, sizeof(loaded) - newFrom);
        loaded.ethDhcp = false; // a board set up before DHCP existed keeps its static address
        loaded.configVersion = CONFIG_STRUCT_VERSION;
        upgraded = true;
    }
    f.close();

    if (!readOk) {
        Serial.println("Settings file missing/version mismatch - using defaults.");
        saveSettings(); // persist current defaults so the next boot reads a valid file
        return;
    }

    config = loaded;
    if (clearUnusedFields() && !upgraded) {
        Serial.println("Old REST-era HA settings (incl. token) cleared.");
        saveSettings();
    }
    if (upgraded) {
        Serial.print("Settings upgraded from version "); Serial.print(fileVersion);
        Serial.println(" (newer fields at their defaults).");
        saveSettings();
    }

    if (config.fanCount < 1 || config.fanCount > 2) { // pins exist for 2 fans (pins.h)
        config.fanCount = 1;
        Serial.println("Config fanCount out of range, reset to 1.");
    }
}
