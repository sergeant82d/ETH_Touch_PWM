#include "fan_network.h"
#include "pins.h"
#include "config.h"
#include <SPI.h>
#include <TimeLib.h>
#include <time.h>
#include <esp_sntp.h>

// The W5500 runs on the core's ETH driver (lwIP) rather than the Arduino
// Ethernet library (its own socket stack, 8 sockets), so web server, MQTT
// and SNTP share one TCP/IP stack with WiFi later. The MAC address is the
// chip's own (unique per board), not the old fixed DE:AD:BE:EF:FE:ED.

NetworkServer server(80);

static const char* NTP_SERVER = "pool.ntp.org";
static const time_t VALID_TIME = 1700000000; // 2023-11: anything earlier = not synced yet

static void tzRule(char *rule, size_t len) {
    if (config.tzPosix[0]) {
        strlcpy(rule, config.tzPosix, len);
    } else {
        // Old fixed offset: POSIX counts the other way (UTC-5 is "UTC5")
        snprintf(rule, len, "UTC%d", -config.tzOffset);
    }
}

void applyTimeZone() {
    char rule[64];
    tzRule(rule, sizeof(rule));
    setenv("TZ", rule, 1);
    tzset();
    Serial.print("Time zone: "); Serial.print(config.tzName); Serial.print(" ("); Serial.print(rule); Serial.println(")");
}

// UTC -> local wall-clock time as a TimeLib time (the rest of the firmware
// uses local time: LCD, SD log, day change, web page)
static time_t toLocal(time_t utc) {
    struct tm lt;
    localtime_r(&utc, &lt);
    tmElements_t te;
    te.Second = lt.tm_sec;
    te.Minute = lt.tm_min;
    te.Hour = lt.tm_hour;
    te.Wday = lt.tm_wday + 1;
    te.Day = lt.tm_mday;
    te.Month = lt.tm_mon + 1;
    te.Year = lt.tm_year + 1900 - 1970;
    return makeTime(te);
}

static void onSntpSync(struct timeval *tv) {
    Serial.println("SNTP: time synchronized.");
}

// (Re)starts SNTP. Called when the network gets its address (link up): SNTP
// started before that fails its first lookup of pool.ntp.org and then backs
// off for a long time. SNTP keeps the system clock in UTC and re-syncs hourly.
static void startSntp() {
    char rule[64];
    tzRule(rule, sizeof(rule));
    sntp_set_time_sync_notification_cb(onSntpSync);
    configTzTime(rule, NTP_SERVER);
    Serial.println("SNTP started.");
}

time_t getNtpTime() {
    time_t utc = time(nullptr);   // survives a reset (not a power cut); SNTP corrects it
    if (utc < VALID_TIME) {
        setSyncInterval(5);   // SNTP not there yet: ask again soon, not in 5 minutes
        return 0;
    }
    setSyncInterval(300);     // then re-read every 5 minutes (daylight saving check)
    return toLocal(utc);
}

static void onNetworkEvent(arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
        case ARDUINO_EVENT_ETH_CONNECTED:    Serial.println("Ethernet: link up"); break;
        case ARDUINO_EVENT_ETH_DISCONNECTED: Serial.println("Ethernet: link down"); break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            Serial.print("Ethernet: address "); Serial.print(ETH.localIP());
            Serial.print(", DNS "); Serial.println(ETH.dnsIP());
            startSntp();
            break;
        default: break;
    }
}

void networkInit() {
    Network.onEvent(onNetworkEvent);
    Serial.println("Initializing W5500 Ethernet (ESP32 ETH driver)...");
    SPI.begin(W5500_SCK, W5500_MISO, W5500_MOSI);
    // The driver pulses W5500_RST itself
    if (!ETH.begin(ETH_PHY_W5500, 1, W5500_CS, W5500_INT, W5500_RST, SPI)) {
        Serial.println("ERROR: W5500 not found - check wiring / pins.h.");
    }
    ETH.config(config.ip, config.gateway, config.subnet, config.dns);
    server.begin();

    Serial.print("Dashboard URL: http://"); Serial.println(ETH.localIP());
    Serial.print("MAC: "); Serial.println(ETH.macAddress());
}

bool isEthernetConnected() {
    return ETH.linkUp();
}

bool isNetworkConnected() {
    return isEthernetConnected();
}

IPAddress localIP() {
    return ETH.localIP();
}
