#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <IPAddress.h>

// Bump this whenever the SystemConfig struct's fields/layout change.
// loadSettings() checks this and falls back to defaults on a mismatch,
// so a firmware update never reads a stale/misaligned raw-byte blob.
#define CONFIG_STRUCT_VERSION 9

// Factory nodeID. MQTT stays off until the user changes it on the web page,
// so two unconfigured boards can't share one name on the broker / in HA.
#define DEFAULT_NODE_ID "fanController_xx"

struct SystemConfig {
    uint32_t configVersion;
    IPAddress ip;
    IPAddress subnet;
    IPAddress gateway;
    IPAddress dns;
    float tMin;
    float tMax;
    bool isFahrenheit;
    int tzOffset;          // hours; only used if tzPosix is empty (before version 7)
    bool is24Hour;
    int fanCount;
    char nodeID[64];
    // REST-era Home Assistant fields, UNUSED since MQTT Phase 3 (HA talks MQTT
    // only). Kept so the layout, and every saved settings file, stays valid;
    // loadSettings() empties them, which wipes a stored HA token.
    char unusedHaToken[256];
    char unusedHaSensor[64];
    char unusedHaHost[64];
    int unusedHaPort;
    long fanRpmGaugeMin;   // LCD/web gauge display scale, not the fan curve itself
    long fanRpmGaugeMax;
    float tempGaugeMinF;   // always stored in F, converted for display as needed
    float tempGaugeMaxF;
    // Old HA helper entity IDs, UNUSED since MQTT Phase 2; emptied like the above.
    // (The override state itself was never stored: it always boots to auto.)
    char unusedHaTMinEntity[64];
    char unusedHaTMaxEntity[64];
    char unusedHaOverrideSwitchEntity[64];
    char unusedHaOverrideSpeedEntity[64];

    // MQTT (version 5). Appended after the version 4 fields on purpose:
    // loadSettings() upgrades a version 4 file by reading it as a prefix.
    char mqttBroker[64];   // IP or host name; empty = MQTT off
    int mqttPort;
    char mqttUser[32];     // empty = no login
    char mqttPass[64];     // never sent to the web page

    // Web login (version 6), appended like the MQTT fields. Empty password =
    // no login set: the web page refuses all changes until one is set.
    char webUser[32];
    char webPass[64];      // never sent to the web page

    // Time zone (version 7): IANA name for the web page, POSIX rule for the
    // clock (daylight saving included), e.g. "America/Chicago",
    // "CST6CDT,M3.2.0,M11.1.0". Rules: posix_tz_db, in web/index.html.
    char tzName[48];
    char tzPosix[64];

    // WiFi backup + setup hotspot (version 8). WiFi is used only while the
    // Ethernet link is down (fan_network.cpp). Addresses as uint32_t (not
    // IPAddress) so an older file's upgrade can copy these defaults as bytes.
    char wifiSsid[33];     // empty = no WiFi backup
    char wifiPass[64];     // never sent to the web page
    bool wifiStatic;       // false = DHCP
    uint32_t wifiIp, wifiGateway, wifiSubnet, wifiDns;
    char hostname[32];     // name.local and in the router; empty = from the node ID
    char apPass[64];       // setup hotspot password, 8-63 characters

    // Ethernet address by DHCP (version 9). Factory default true; a board
    // upgraded from an older version keeps its static address (loadSettings).
    bool ethDhcp;
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
