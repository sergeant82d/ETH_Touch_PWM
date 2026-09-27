#include "config.h"
#include <FS.h>
#include <LittleFS.h>

// NOTE: The original source had a live Home Assistant long-lived bearer token
// hardcoded here. That has been removed — hardcoding secrets in source you
// might share, commit, or lose track of is a real credential-leak risk.
// Set the token once via the web config form; it will persist in
// /settings.cfg on LittleFS from then on.
SystemConfig config = {
    CONFIG_STRUCT_VERSION,
    IPAddress(192, 168, 10, 54),
    IPAddress(255, 255, 255, 0),
    IPAddress(192, 168, 10, 1),
    IPAddress(192, 168, 10, 11),
    26.7,
    37.8,
    true,
    -5,
    true,
    2,
    DEFAULT_NODE_ID,
    "",   // haToken - set via web UI
    "living_room_probe_02_temperature",
    "192.168.10.85",
    8123,
    300, 2200,   // fan RPM gauge display range
    60.0, 110.0, // temperature gauge display range, in F
    "input_number.fan_ctrl_02_tmin",
    "input_number.fan_ctrl_02_tmax",
    "input_boolean.fan_ctrl_02_override",
    "input_number.fan_ctrl_02_override_speed",
    "192.168.10.85", // mqttBroker - Mosquitto add-on on the HA box
    1883,
    "",   // mqttUser - set via web UI
    ""    // mqttPass - set via web UI
};

// Version 4 settings = this struct without the MQTT fields at the end
// (offsetof, rounded up to the struct's 4-byte alignment) = 864 bytes;
// checked against the old struct when this was written (2026-09-27).
// offsetof warns because IPAddress has virtual functions; GCC supports it.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
static const size_t V4_FILE_SIZE = (offsetof(SystemConfig, mqttBroker) + 3) & ~(size_t)3;
#pragma GCC diagnostic pop

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

void loadSettings() {
    if (!LittleFS.exists("/settings.cfg")) return;

    File f = LittleFS.open("/settings.cfg", "r");
    if (!f) return;

    // Start from the compiled-in defaults: a version 4 file is shorter and
    // leaves the MQTT fields at their defaults.
    SystemConfig loaded = config;
    size_t fileSize = f.size();
    bool readOk = false;
    bool upgraded = false;
    if (fileSize == sizeof(loaded)) {
        readOk = (f.read((uint8_t*)&loaded, sizeof(loaded)) == sizeof(loaded)) &&
                 loaded.configVersion == CONFIG_STRUCT_VERSION;
    } else if (fileSize == V4_FILE_SIZE) {
        readOk = (f.read((uint8_t*)&loaded, fileSize) == fileSize) && loaded.configVersion == 4;
        if (readOk) {
            // The file's tail padding may have landed on the first MQTT bytes
            memcpy(loaded.mqttBroker, config.mqttBroker, sizeof(loaded.mqttBroker));
            loaded.configVersion = CONFIG_STRUCT_VERSION;
            upgraded = true;
        }
    }
    f.close();

    if (!readOk) {
        Serial.println("Settings file missing/version mismatch - using defaults.");
        saveSettings(); // persist current defaults so the next boot reads a valid file
        return;
    }

    config = loaded;
    if (upgraded) {
        Serial.println("Settings upgraded from version 4 (MQTT settings added, defaults).");
        saveSettings();
    }

    if (config.fanCount < 2 || config.fanCount > 4) {
        config.fanCount = 2;
        Serial.println("Config fanCount out of range, reset to 2.");
    }
}
