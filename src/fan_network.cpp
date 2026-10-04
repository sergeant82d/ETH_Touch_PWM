#include "fan_network.h"
#include "pins.h"
#include "config.h"
#include <SPI.h>
#include <TimeLib.h>
#include <time.h>
#include <esp_sntp.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <mdns.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <lwip/dns.h>
#include "mqtt.h"

// The W5500 runs on the core's ETH driver (lwIP) rather than the Arduino
// Ethernet library (its own socket stack, 8 sockets), so web server, MQTT
// and SNTP share one TCP/IP stack with WiFi. The MAC address is the chip's
// own (unique per board), not the old fixed DE:AD:BE:EF:FE:ED.
//
// WiFi is only a backup for Ethernet (user decision 2026-09-27): joined when
// the Ethernet link has been down for 30 s, left 60 s after it is back. The
// setup hotspot starts when no network has worked for 60 s.
// At boot (2026-10-04): no W5500 = join WiFi at once; W5500 but no address
// yet = wait 10 s, not 30. The hotspot waits while a WiFi join is under way
// (up to 45 s), so a slow join no longer ends up on the hotspot.

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

static unsigned long lastSyncMs = 0;   // 0 = not synced since boot

static void onSntpSync(struct timeval *tv) {
    lastSyncMs = millis();
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

// DNS servers as each network handed them out. lwIP has one global DNS
// setting, and starting/stopping the setup hotspot can overwrite it; then
// SNTP can't look up pool.ntp.org until the next address event (user's NTP
// problem after network switches, 2026-10-04). restoreDnsAndTime() puts the
// right one back.
static uint32_t ethDns = 0, wifiDns = 0;

static void onNetworkEvent(arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
        case ARDUINO_EVENT_ETH_CONNECTED:    Serial.println("Ethernet: link up"); break;
        case ARDUINO_EVENT_ETH_DISCONNECTED: Serial.println("Ethernet: link down"); break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            ethDns = (uint32_t)ETH.dnsIP();
            Serial.print("Ethernet: address "); Serial.print(ETH.localIP());
            Serial.print(", DNS "); Serial.println(ETH.dnsIP());
            startSntp();
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            wifiDns = (uint32_t)WiFi.dnsIP();
            Serial.print("WiFi: joined "); Serial.print(WiFi.SSID()); Serial.print(", address ");
            Serial.print(WiFi.localIP()); Serial.print(", signal "); Serial.print(WiFi.RSSI()); Serial.println(" dBm");
            startSntp();
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: Serial.println("WiFi: disconnected"); break;
        default: break;
    }
}

// ============================================================================
// DEVICE NAME (hostname, mDNS)
// ============================================================================

String deviceHostname() {
    if (config.hostname[0]) return config.hostname;
    // From the node ID: lower case, letters/digits/hyphens ("fanController_01" -> "fancontroller-01")
    String h;
    for (const char *c = config.nodeID; *c && h.length() < 31; c++) {
        if (isalnum((unsigned char)*c)) h += (char)tolower(*c);
        else if (h.length() && h[h.length() - 1] != '-') h += '-';
    }
    while (h.endsWith("-")) h.remove(h.length() - 1);
    return h.length() ? h : String("fancontroller");
}

// mDNS is started once and renamed in place. MDNS.end() crashed (2026-10-04):
// after the setup hotspot had been on and off, mDNS still held the deleted
// hotspot interface, and ending it dereferenced that (WiFi save -> panic).
static String mdnsName; // "" = mDNS not started yet

static void applyHostname() {
    String name = deviceHostname();
    ETH.setHostname(name.c_str());
    WiFi.setHostname(name.c_str());
    if (name == mdnsName) return;
    bool ok;
    if (mdnsName.length() == 0) {
        ok = MDNS.begin(name.c_str());
        if (ok) MDNS.addService("http", "tcp", 80);
    } else {
        ok = mdns_hostname_set(name.c_str()) == ESP_OK;
    }
    if (ok) {
        mdnsName = name;
        Serial.print("Device name: http://"); Serial.print(name); Serial.println(".local");
    }
}

// ============================================================================
// WIFI BACKUP + HOTSPOT
// ============================================================================

static bool staActive = false;     // backup WiFi wanted (joining or joined)
static bool apActive = false;      // setup hotspot on
static bool scanning = false;
static unsigned long ethDownSince = 0;  // boot counts as "down" until the link comes up
static unsigned long ethUpSince = 0;
static unsigned long lastAnyUpMs = 0;   // last moment Ethernet or WiFi worked
static unsigned long anyUpSince = 0;    // start of the current stretch with a network
static bool lastEthUp = false;
static bool lastAnyUp = false;
static const char* lastActive = "";
static bool ethPresent = false;         // W5500 answered at boot
static bool ethEverUp = false;          // Ethernet has had an address since boot
static unsigned long staStartedMs = 0;  // when the current WiFi join began

static const unsigned long WIFI_AFTER_ETH_DOWN_MS = 30000;
static const unsigned long WIFI_AFTER_BOOT_MS = 10000;  // Ethernet normally has its address within a few s
static const unsigned long WIFI_JOIN_GRACE_MS = 45000;  // hotspot holds off this long for a WiFi join
static const unsigned long WIFI_OFF_AFTER_ETH_UP_MS = 60000;
static const unsigned long HOTSPOT_AFTER_MS = 60000;
static const unsigned long HOTSPOT_OFF_AFTER_MS = 30000;

String macSuffix() {
    uint64_t mac = ESP.getEfuseMac();
    char buf[8];
    snprintf(buf, sizeof(buf), "%02X%02X", (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
    return buf;
}

static String hotspotSsid() { return "FanController-" + macSuffix(); }

// Radio mode from what is wanted right now
static void applyWifiMode() {
    bool sta = staActive || scanning;
    wifi_mode_t mode = sta ? (apActive ? WIFI_AP_STA : WIFI_STA) : (apActive ? WIFI_AP : WIFI_OFF);
    if (WiFi.getMode() != mode) WiFi.mode(mode);
}

static void startSta() {
    staActive = true;
    staStartedMs = millis();
    applyWifiMode();
    if (config.wifiStatic) {
        WiFi.config(IPAddress(config.wifiIp), IPAddress(config.wifiGateway), IPAddress(config.wifiSubnet), IPAddress(config.wifiDns));
    } else {
        WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE); // DHCP
    }
    WiFi.begin(config.wifiSsid, config.wifiPass);
    Serial.print("WiFi: joining "); Serial.println(config.wifiSsid);
}

static void stopSta(const char* why) {
    WiFi.disconnect();
    staActive = false;
    applyWifiMode();
    Serial.print("WiFi: backup off ("); Serial.print(why); Serial.println(")");
}

static String currentDns() {
    const ip_addr_t *d = dns_getserver(0);
    return d ? IPAddress(ip_addr_get_ip4_u32(d)).toString() : String("none");
}

String networkDiagText() {
    String t = String("net=") + activeNetwork() + " dns=" + currentDns() + " time=";
    if (lastSyncMs) t += "synced " + String((millis() - lastSyncMs) / 1000) + " s ago";
    else t += time(nullptr) >= VALID_TIME ? "set, no sync since boot" : "not set";
    return t;
}

// Puts back the DNS server of the network in use (Ethernet or WiFi) and
// restarts SNTP, after anything that may have overwritten the global DNS
static void restoreDnsAndTime(const char* why) {
    uint32_t ip = 0;
    esp_netif_t *nif = nullptr;
    if (strcmp(activeNetwork(), "Ethernet") == 0) { ip = ethDns; nif = ETH.netif(); }
    else if (strcmp(activeNetwork(), "WiFi") == 0) { ip = wifiDns; nif = WiFi.STA.netif(); }
    if (!ip || !nif) return;
    esp_netif_dns_info_t d = {};
    d.ip.type = ESP_IPADDR_TYPE_V4;
    d.ip.u_addr.ip4.addr = ip;
    esp_netif_set_dns_info(nif, ESP_NETIF_DNS_MAIN, &d);
    Serial.print("Network: DNS "); Serial.print(IPAddress(ip)); Serial.print(" set again ("); Serial.print(why); Serial.println(")");
    startSntp();
}

static void startAp() {
    apActive = true;
    applyWifiMode();
    WiFi.softAP(hotspotSsid().c_str(), config.apPass);
    Serial.print("Hotspot: "); Serial.print(hotspotSsid()); Serial.print(" on, page at http://"); Serial.println(WiFi.softAPIP());
    restoreDnsAndTime("hotspot on");
}

static void stopAp() {
    WiFi.softAPdisconnect(false);
    apActive = false;
    applyWifiMode();
    Serial.println("Hotspot: off (a network is back)");
    restoreDnsAndTime("hotspot off");
}

bool isEthernetConnected() { return ETH.linkUp(); }
bool isWifiConnected() { return staActive && WiFi.status() == WL_CONNECTED; }
bool isHotspotActive() { return apActive; }
// With an address, not just a link (the LCD dot, MQTT and the web page agree; 2026-10-04)
bool isNetworkConnected() { return (isEthernetConnected() && ETH.hasIP()) || isWifiConnected(); }

// Time left before the hotspot may start: a minute without a network, and not
// while a WiFi join is still within its grace time
static long hotspotWaitMs(unsigned long now) {
    long wait = (long)HOTSPOT_AFTER_MS - (long)(now - lastAnyUpMs);
    if (staActive && !isWifiConnected()) wait = max(wait, (long)WIFI_JOIN_GRACE_MS - (long)(now - staStartedMs));
    return wait > 0 ? wait : 0;
}

long hotspotStartsInMs() {
    if (apActive || isNetworkConnected()) return -1;
    return hotspotWaitMs(millis());
}

const char* activeNetwork() {
    if (isEthernetConnected() && ETH.hasIP()) return "Ethernet";
    if (isWifiConnected()) return "WiFi";
    if (apActive) return "Hotspot";
    return "None";
}

String activeNetworkName() {
    const char* a = activeNetwork();
    if (strcmp(a, "WiFi") == 0) return WiFi.SSID();
    if (strcmp(a, "Hotspot") == 0) return hotspotSsid();
    return "";
}

IPAddress localIP() {
    if (isEthernetConnected() && ETH.hasIP()) return ETH.localIP();
    if (isWifiConnected()) return WiFi.localIP();
    if (apActive) return WiFi.softAPIP();
    return ETH.localIP(); // the static address it will have
}

void networkLoop() {
    static unsigned long lastRun = 0;
    if (millis() - lastRun < 500) return;
    lastRun = millis();
    unsigned long now = millis();

    bool ethUp = isEthernetConnected() && ETH.hasIP();
    if (ethUp && !lastEthUp) ethUpSince = now;
    if (ethUp) ethEverUp = true;
    if (!ethUp && lastEthUp) ethDownSince = now;
    lastEthUp = ethUp;
    bool wifiUp = isWifiConnected();
    bool anyUp = ethUp || wifiUp;
    if (anyUp && !lastAnyUp) anyUpSince = now;
    if (anyUp) lastAnyUpMs = now;
    lastAnyUp = anyUp;

    // WiFi backup: only while Ethernet is down
    unsigned long wifiWait = !ethPresent ? 0 : ethEverUp ? WIFI_AFTER_ETH_DOWN_MS : WIFI_AFTER_BOOT_MS;
    if (!ethUp && !staActive && config.wifiSsid[0] && now - ethDownSince >= wifiWait) startSta();
    if (ethUp && staActive && now - ethUpSince >= WIFI_OFF_AFTER_ETH_UP_MS) stopSta("Ethernet is back");

    // Setup hotspot: after a minute without any network, off once one has held for 30 s
    if (!apActive && !anyUp && hotspotWaitMs(now) == 0) startAp();
    if (apActive && anyUp && now - anyUpSince >= HOTSPOT_OFF_AFTER_MS) stopAp();

    // Switched network: MQTT reconnects at once over the new one
    const char* active = activeNetwork();
    if (strcmp(active, lastActive) != 0) {
        Serial.print("Network in use: "); Serial.println(active);
        if (lastActive[0]) {
            restoreDnsAndTime("network switch");
            mqttReconfigure();
        }
        Serial.print("Network: "); Serial.println(networkDiagText());
        lastActive = active;
    }
}

void wifiReconfigure() {
    applyHostname();
    if (staActive) {
        WiFi.disconnect();
        staActive = false;
        applyWifiMode();
        // networkLoop() joins again (new settings) if Ethernet is still down
    }
}

void wifiScanStart() {
    if (scanning) return;
    scanning = true;
    applyWifiMode();
    WiFi.scanNetworks(true);
}

bool wifiScanCollect(JsonArray out) {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) return false;
    for (int i = 0; i < n; i++) {
        if (WiFi.SSID(i).length() == 0) continue; // hidden networks
        JsonObject o = out.add<JsonObject>();
        o["ssid"] = WiFi.SSID(i);
        o["rssi"] = WiFi.RSSI(i);
        o["secure"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();
    scanning = false;
    applyWifiMode(); // radio off again unless WiFi or the hotspot is in use
    return true;
}

void networkStatus(JsonObject out) {
    out["active"] = activeNetwork();
    out["ip"] = localIP().toString();
    out["hostname"] = deviceHostname();
    JsonObject e = out["ethernet"].to<JsonObject>();
    e["link"] = isEthernetConnected();
    e["ip"] = ETH.localIP().toString();
    e["mac"] = ETH.macAddress();
    JsonObject w = out["wifi"].to<JsonObject>();
    w["configured"] = config.wifiSsid[0] != '\0';
    w["active"] = staActive;
    w["connected"] = isWifiConnected();
    w["ssid"] = config.wifiSsid;
    if (isWifiConnected()) {
        w["ip"] = WiFi.localIP().toString();
        w["rssi"] = WiFi.RSSI();
    }
    uint8_t m[6];
    esp_read_mac(m, ESP_MAC_WIFI_STA); // WiFi.macAddress() reads zeros while the radio is off
    char mac[18];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    w["mac"] = mac;
    JsonObject h = out["hotspot"].to<JsonObject>();
    h["active"] = apActive;
    h["ssid"] = hotspotSsid();
    h["ip"] = apActive ? WiFi.softAPIP().toString() : String("192.168.4.1");
}

void networkInit() {
    Network.onEvent(onNetworkEvent);
    Serial.println("Initializing W5500 Ethernet (ESP32 ETH driver)...");
    SPI.begin(W5500_SCK, W5500_MISO, W5500_MOSI);
    // The driver pulses W5500_RST itself
    ethPresent = ETH.begin(ETH_PHY_W5500, 1, W5500_CS, W5500_INT, W5500_RST, SPI);
    if (!ethPresent) {
        Serial.println("ERROR: W5500 not found - check wiring / pins.h. Using WiFi only.");
    }
    if (config.ethDhcp) {
        ETH.config();  // DHCP: address from the router (find the board by its device name)
        Serial.println("Ethernet: DHCP");
    } else {
        ETH.config(config.ip, config.gateway, config.subnet, config.dns);
    }
    WiFi.persistent(false);    // WiFi settings live in our config, not the WiFi driver's flash
    WiFi.mode(WIFI_OFF);       // on only when needed (backup, hotspot, scan)
    applyHostname();
    server.begin();

    Serial.print("Dashboard URL: http://"); Serial.println(ETH.localIP());
    Serial.print("MAC: "); Serial.println(ETH.macAddress());
}
