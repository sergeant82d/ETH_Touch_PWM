#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <IPAddress.h>

// Bump this whenever the SystemConfig struct's fields/layout change.
// loadSettings() checks this and falls back to defaults on a mismatch,
// so a firmware update never reads a stale/misaligned raw-byte blob.
#define CONFIG_STRUCT_VERSION 4

struct SystemConfig {
    uint32_t configVersion;
    IPAddress ip;
    IPAddress subnet;
    IPAddress gateway;
    IPAddress dns;
    float tMin;
    float tMax;
    bool isFahrenheit;
    int tzOffset;
    bool is24Hour;
    int fanCount;
    char nodeID[64];
    char haToken[256];
    char haSensor[64];
    char haHost[64];
    int haPort;
    long fanRpmGaugeMin;   // LCD/web gauge display scale, not the fan curve itself
    long fanRpmGaugeMax;
    float tempGaugeMinF;   // always stored in F, converted for display as needed
    float tempGaugeMaxF;
    char haTMinEntity[64]; // HA input_number entity ID, e.g. "input_number.fan_ctrl_01_tmin"
    char haTMaxEntity[64]; // stores/polls in Celsius, matching tMin/tMax's internal units
    char haOverrideSwitchEntity[64]; // input_boolean entity - on/off mirrors manualOverrideActive
    char haOverrideSpeedEntity[64];  // input_number entity (0-255) - mirrors manualOverrideDutyCycle
    // NOTE: only the *entity IDs* live here. manualOverrideActive/DutyCycle
    // themselves (in sensors.h) are never persisted - override always boots
    // back to auto per the locked-in boot-behavior rule.
};

extern SystemConfig config;

// Loads settings from LittleFS (/settings.cfg). Falls back to defaults if
// the file is missing or the stored struct size doesn't match.
void loadSettings();

// Writes the current config struct to LittleFS (/settings.cfg).
void saveSettings();

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

#endif // CONFIG_H
