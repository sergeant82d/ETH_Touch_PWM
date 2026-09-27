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
    "fan_controller_02",
    "",   // haToken - set via web UI
    "living_room_probe_02_temperature",
    "192.168.10.85",
    8123,
    300, 2200,   // fan RPM gauge display range
    60.0, 110.0, // temperature gauge display range, in F
    "input_number.fan_ctrl_02_tmin",
    "input_number.fan_ctrl_02_tmax",
    "input_boolean.fan_ctrl_02_override",
    "input_number.fan_ctrl_02_override_speed"
};

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

    SystemConfig loaded;
    bool readOk = (f.available() >= (int)sizeof(loaded)) &&
                  (f.read((uint8_t*)&loaded, sizeof(loaded)) == (int)sizeof(loaded));
    f.close();

    if (!readOk || loaded.configVersion != CONFIG_STRUCT_VERSION) {
        Serial.println("Settings file missing/version mismatch - using defaults.");
        saveSettings(); // persist current defaults so the next boot reads a valid file
        return;
    }

    config = loaded;

    if (config.fanCount < 2 || config.fanCount > 4) {
        config.fanCount = 2;
        Serial.println("Config fanCount out of range, reset to 2.");
    }
}
