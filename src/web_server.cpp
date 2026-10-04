#include "web_server.h"
#include "config.h"
#include "pins.h"
#include "sensors.h"
#include "fan_network.h"
#include "mqtt.h"
#include "sd_logger.h"
#if HAS_LCD
#include "display.h"
#endif
#include <TimeLib.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <base64.h>
#include <SD.h>
#include <math.h>

// The page is web/index.html, compiled in (platformio.ini embed_txtfiles);
// everything else is JSON under /api. Viewing is open; every change needs
// the web login (HTTP Basic), and changes are refused until one is set.

extern const uint8_t index_html_start[] asm("_binary_web_index_html_start");
extern const uint8_t index_html_end[] asm("_binary_web_index_html_end");

// Board name inside every firmware image, checked on OTA so the Touch-LCD-2
// build can't be flashed onto the ESP32-S3-ETH or vice versa (their W5500
// pins differ: the wrong one loses the network until reflashed over USB).
const char OTA_BOARD_MARKER[] = "@@BOARD=" BOARD_NAME "@@";

static const char* THEME_PATH = "/theme.json";
static const size_t MAX_BODY = 12288;  // largest JSON body: the notes (4000 bytes + escaping)

// ============================================================================
// REQUEST / RESPONSE
// ============================================================================

struct Request {
    String method;
    String path;
    String auth;          // Authorization header
    String query;         // after '?' in the path
    size_t contentLength = 0;
};

static bool readRequest(NetworkClient &client, Request &req) {
    client.setTimeout(2000);
    String line = client.readStringUntil('\n');
    line.trim();
    int sp1 = line.indexOf(' ');
    int sp2 = line.indexOf(' ', sp1 + 1);
    if (sp1 < 0 || sp2 < 0) return false;
    req.method = line.substring(0, sp1);
    req.path = line.substring(sp1 + 1, sp2);
    int q = req.path.indexOf('?');
    if (q >= 0) {
        req.query = req.path.substring(q + 1);
        req.path = req.path.substring(0, q);
    }

    for (int i = 0; i < 40; i++) { // headers, until the blank line
        String h = client.readStringUntil('\n');
        h.trim();
        if (h.length() == 0) break;
        int colon = h.indexOf(':');
        if (colon < 0) continue;
        String name = h.substring(0, colon);
        name.toLowerCase();
        String value = h.substring(colon + 1);
        value.trim();
        if (name == "content-length") req.contentLength = value.toInt();
        else if (name == "authorization") req.auth = value;
    }
    return true;
}

static String readBody(NetworkClient &client, size_t len) {
    String body;
    if (len > MAX_BODY) return body;
    body.reserve(len);
    unsigned long start = millis();
    while (body.length() < len && millis() - start < 3000) {
        while (client.available() && body.length() < len) body += (char)client.read();
    }
    return body;
}

static void sendHead(NetworkClient &client, int code, const char* type, size_t len) {
    const char* text = code == 200 ? "OK" : code == 202 ? "Accepted" : code == 400 ? "Bad Request" : code == 401 ? "Unauthorized" :
                       code == 403 ? "Forbidden" : code == 404 ? "Not Found" : "Error";
    client.print("HTTP/1.1 "); client.print(code); client.print(' '); client.println(text);
    client.print("Content-Type: "); client.println(type);
    client.print("Content-Length: "); client.println(len);
    client.println("Cache-Control: no-store");
    client.println("Connection: close");
    client.println();
}

static void sendJson(NetworkClient &client, int code, JsonDocument &doc) {
    String out;
    serializeJson(doc, out);
    sendHead(client, code, "application/json", out.length());
    client.print(out);
}

// {"ok":false,"error":"..."} or {"ok":true}
static void sendResult(NetworkClient &client, int code, const char* error = nullptr) {
    JsonDocument doc;
    doc["ok"] = error == nullptr;
    if (error) doc["error"] = error;
    sendJson(client, code, doc);
}

static void sendPage(NetworkClient &client) {
    size_t len = index_html_end - index_html_start - 1; // embed_txtfiles adds a trailing NUL
    sendHead(client, 200, "text/html; charset=utf-8", len);
    for (size_t sent = 0; sent < len; ) {
        size_t n = min((size_t)1024, len - sent);
        size_t w = client.write(index_html_start + sent, n);
        if (w == 0) break;
        sent += w;
    }
}

// ============================================================================
// LOGIN
// ============================================================================

static bool loginSet() { return config.webPass[0] != '\0'; }

static bool authorized(const Request &req) {
    if (!loginSet()) return false;
    String expected = "Basic " + base64::encode(String(config.webUser) + ":" + config.webPass);
    return req.auth == expected;
}

// Every change goes through here. Sends the refusal itself.
static bool requireLogin(NetworkClient &client, const Request &req) {
    if (!loginSet()) {
        sendResult(client, 403, "Set a web login first (System tab).");
        return false;
    }
    if (!authorized(req)) {
        sendResult(client, 401, "Login required.");
        return false;
    }
    return true;
}

static bool validUser(const String &u) {
    if (u.length() < 1 || u.length() >= sizeof(config.webUser)) return false;
    for (size_t i = 0; i < u.length(); i++) {
        char c = u[i];
        if (!isalnum((unsigned char)c) && c != '.' && c != '_' && c != '-') return false;
    }
    return true;
}

// ============================================================================
// JSON HELPERS
// ============================================================================

static float toDisplay(float c) { return config.isFahrenheit ? c * 9.0 / 5.0 + 32.0 : c; }

static void addTemp(JsonObject o, bool ok, float c) {
    o["ok"] = ok;
    if (ok) o["c"] = serialized(String(c, 1));
    else o["c"] = nullptr;
}

static bool parseIp(const String &s, IPAddress &out) {
    int a, b, c, d;
    char extra;
    if (sscanf(s.c_str(), "%d.%d.%d.%d%c", &a, &b, &c, &d, &extra) != 4) return false;
    if (a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255 || d < 0 || d > 255) return false;
    out = IPAddress(a, b, c, d);
    return true;
}

// ============================================================================
// /api/status, /api/config
// ============================================================================

static void handleStatus(NetworkClient &client) {
    JsonDocument doc;
    doc["node"] = config.nodeID;
    doc["board"] = BOARD_NAME;
    doc["sketch"] = SKETCH_FILENAME;
    doc["build"] = __DATE__ " " __TIME__;
    doc["ip"] = localIP().toString();
    doc["uptime"] = millis() / 1000;
    doc["heap"] = ESP.getFreeHeap();
    doc["timeSet"] = timeStatus() != timeNotSet;
    doc["time"] = (uint32_t)now(); // local time (NTP + time zone rule), seconds
    doc["tz"] = config.tzName;
    doc["fahrenheit"] = config.isFahrenheit;
    doc["clock24"] = config.is24Hour;
#if HAS_LCD
    doc["display"] = isDisplayOn(); // LCD standby; absent on a board without an LCD
#endif
    addTemp(doc["local"].to<JsonObject>(), localSensorHealthy, localTempC);
    addTemp(doc["network"].to<JsonObject>(), networkSensorHealthy, networkTempC);
    addTemp(doc["blended"].to<JsonObject>(), localSensorHealthy || networkSensorHealthy, blendedAverageC);
    JsonArray fans = doc["fans"].to<JsonArray>();
    for (int i = 0; i < config.fanCount && i < NUM_FANS; i++) {
        JsonObject f = fans.add<JsonObject>();
        f["rpm"] = currentRPMs[i];
        f["duty"] = (currentDutyCycles[i] * 100 + 127) / 255;
        f["fault"] = currentDutyCycles[i] > 51 && currentRPMs[i] == 0;
    }
    doc["override"] = manualOverrideActive;
    doc["overridePct"] = (manualOverrideDutyCycle * 100 + 127) / 255;
    doc["sd"] = isSdCardPresent();
    doc["sdState"] = sdStateText();            // "OK", "Getting full", "Missing"
    if (sdUsedPercent() >= 0) doc["sdUsed"] = sdUsedPercent();
    else doc["sdUsed"] = nullptr;
    doc["eth"] = isEthernetConnected();
    doc["net"] = activeNetwork();
    doc["netName"] = activeNetworkName(); // WiFi SSID or hotspot name; "" on Ethernet
    doc["mqtt"] = mqttStatusText();
    doc["mqttOk"] = mqttStatusText().startsWith("Connected");
    doc["loginSet"] = loginSet();
    sendJson(client, 200, doc);
}

static void handleGetConfig(NetworkClient &client) {
    JsonDocument doc;
    doc["fanCount"] = config.fanCount;
    // Every channel with its pins; -1 = not wired on this board (can't be ticked)
    JsonArray chans = doc["channels"].to<JsonArray>();
    for (int i = 0; i < NUM_FANS; i++) {
        JsonObject c = chans.add<JsonObject>();
        c["pwm"] = pwmPins[i];
        c["tach"] = tachPins[i];
        c["offset"] = i < 4 ? config.fanOffsetRpm[i] : 0; // RPM vs fan 1 (fan 1: always 0)
    }
    doc["fahrenheit"] = config.isFahrenheit;
    doc["tMinC"] = serialized(String(config.tMin, 1));
    doc["tMaxC"] = serialized(String(config.tMax, 1));
    JsonObject gauge = doc["gauge"].to<JsonObject>(); // LCD bar scales (the web page's LCD view)
    gauge["rpmMin"] = config.fanRpmGaugeMin;
    gauge["rpmMax"] = config.fanRpmGaugeMax;
    gauge["tMinF"] = config.tempGaugeMinF;
    gauge["tMaxF"] = config.tempGaugeMaxF;
    doc["tzName"] = config.tzName;
    doc["clock24"] = config.is24Hour;
    doc["ethDhcp"] = config.ethDhcp;
    doc["ip"] = config.ip.toString();
    doc["subnet"] = config.subnet.toString();
    doc["gateway"] = config.gateway.toString();
    doc["dns"] = config.dns.toString();
    doc["nodeId"] = config.nodeID;
    doc["defaultNodeId"] = DEFAULT_NODE_ID;
    doc["suggestedNodeId"] = "fanController_" + macSuffix(); // setup page: unique per board
    doc["mqttBroker"] = config.mqttBroker;
    doc["mqttPort"] = config.mqttPort;
    doc["mqttUser"] = config.mqttUser;
    doc["mqttPassSet"] = config.mqttPass[0] != '\0'; // the password itself is never sent
    doc["loginSet"] = loginSet();
    doc["webUser"] = config.webUser;
    sendJson(client, 200, doc);
}

static void logThreshold(const char* name, float oldC, float newC) {
    if (fabs(newC - oldC) > 0.05) {
        sdLogEvent("CONFIG", String("source=web field=") + name + " old=" + String(oldC, 1) + "C new=" + String(newC, 1) + "C");
    }
}

// Restart after a settings save that needs one. Margin for the settings write to
// commit and LittleFS to unmount before the reset (a restart too soon after a
// write once corrupted LittleFS; see mountLittleFSWithRecovery()).
static void restartAfterSave(NetworkClient &client) {
    client.flush();
    client.stop();
    delay(1500);
    LittleFS.end();
    delay(200);
    ESP.restart();
}

// Checks the keys present in `in` and applies them to `next` (each tab saves its own
// fields; the setup page sends them all). Returns an error message, or nullptr.
static const char* applyConfigJson(JsonDocument &in, SystemConfig &next) {
    if (in["fanCount"].is<int>()) {
        int n = in["fanCount"];
        int wired = 0;
        while (wired < NUM_FANS && pwmPins[wired] >= 0) wired++;
        if (n < 1 || n > wired) return "That fan channel isn't wired on this board.";
        next.fanCount = n;
    }
    if (in["fanOffsets"].is<JsonArrayConst>()) { // RPM vs fan 1, +/-500 in 10 RPM steps
        JsonArrayConst a = in["fanOffsets"];
        for (int i = 1; i < 4 && i < (int)a.size(); i++) {
            if (!a[i].is<int>()) return "Fan offsets must be numbers.";
            int v = a[i];
            if (v < -500 || v > 500) return "Fan offsets: -500 to +500 RPM.";
            next.fanOffsetRpm[i] = (v / 10) * 10;
        }
    }
    if (in["fahrenheit"].is<bool>()) next.isFahrenheit = in["fahrenheit"];
    if (in["tMinC"].is<float>() || in["tMaxC"].is<float>()) {
        float lo = in["tMinC"] | next.tMin;
        float hi = in["tMaxC"] | next.tMax;
        if (lo < 0 || lo > 100 || hi < 0 || hi > 100) return "Temperatures must be 0-100 C.";
        if (hi <= lo) return "Fan curve top must be above the start.";
        next.tMin = lo;
        next.tMax = hi;
    }
    if (in["tzName"].is<const char*>() || in["tzPosix"].is<const char*>()) {
        // Name for the page, rule for the clock (the page's zone table has both)
        String name = in["tzName"] | "";
        String rule = in["tzPosix"] | "";
        bool ok = name.length() > 0 && name.length() < sizeof(next.tzName) &&
                  rule.length() > 0 && rule.length() < sizeof(next.tzPosix);
        for (size_t i = 0; ok && i < name.length(); i++) ok = isalnum((unsigned char)name[i]) || strchr("_/+-", name[i]);
        for (size_t i = 0; ok && i < rule.length(); i++) ok = isalnum((unsigned char)rule[i]) || strchr("<>+-,.:/", rule[i]);
        if (!ok) return "Invalid time zone.";
        strlcpy(next.tzName, name.c_str(), sizeof(next.tzName));
        strlcpy(next.tzPosix, rule.c_str(), sizeof(next.tzPosix));
    }
    if (in["clock24"].is<bool>()) next.is24Hour = in["clock24"];

    if (in["ethDhcp"].is<bool>()) next.ethDhcp = in["ethDhcp"];
    const char* ipKeys[] = {"ip", "subnet", "gateway", "dns"};
    IPAddress* ipFields[] = {&next.ip, &next.subnet, &next.gateway, &next.dns};
    for (int i = 0; i < 4; i++) {
        if (in[ipKeys[i]].is<const char*>() && !parseIp(in[ipKeys[i]].as<String>(), *ipFields[i])) return "Invalid IP address.";
    }

    if (in["nodeId"].is<const char*>()) {
        String id = in["nodeId"].as<String>();
        if (!isValidNodeId(id.c_str())) return "Node ID: letters, digits, _ and - only.";
        strlcpy(next.nodeID, id.c_str(), sizeof(next.nodeID));
    }
    if (in["mqttBroker"].is<const char*>()) {
        String b = in["mqttBroker"].as<String>();
        b.trim();
        for (size_t i = 0; i < b.length(); i++) {
            if (!isalnum((unsigned char)b[i]) && b[i] != '.' && b[i] != '-') return "Broker: host name or IP.";
        }
        if (b.length() >= sizeof(next.mqttBroker)) return "Broker name too long.";
        strlcpy(next.mqttBroker, b.c_str(), sizeof(next.mqttBroker));
    }
    if (in["mqttPort"].is<int>()) {
        int p = in["mqttPort"];
        if (p < 1 || p > 65535) return "Port must be 1-65535.";
        next.mqttPort = p;
    }
    if (in["mqttUser"].is<const char*>()) {
        String u = in["mqttUser"].as<String>();
        if (u.length() >= sizeof(next.mqttUser)) return "MQTT user too long.";
        strlcpy(next.mqttUser, u.c_str(), sizeof(next.mqttUser));
    }
    if (in["mqttPass"].is<const char*>()) { // blank = keep
        String p = in["mqttPass"].as<String>();
        if (p.length() >= sizeof(next.mqttPass)) return "MQTT password too long.";
        if (p.length()) strlcpy(next.mqttPass, p.c_str(), sizeof(next.mqttPass));
    }
    return nullptr;
}

static void handlePostConfig(NetworkClient &client, const Request &req) {
    JsonDocument in;
    if (deserializeJson(in, readBody(client, req.contentLength))) {
        sendResult(client, 400, "Bad JSON.");
        return;
    }
    SystemConfig next = config;
    const char* error = applyConfigJson(in, next);
    if (error) { sendResult(client, 400, error); return; }

    bool tzChanged = strcmp(next.tzPosix, config.tzPosix) != 0;
    bool networkChanged = next.ethDhcp != config.ethDhcp ||
                          (!next.ethDhcp && (next.ip != config.ip || next.subnet != config.subnet ||
                                             next.gateway != config.gateway || next.dns != config.dns));
    logThreshold("tMin", config.tMin, next.tMin);
    logThreshold("tMax", config.tMax, next.tMax);
    config = next;
    saveSettings();
    mqttReconfigure(); // node ID / broker / fan count may have changed

    JsonDocument out;
    out["ok"] = true;
    out["restart"] = networkChanged;
    out["hostname"] = deviceHostname(); // where to find the page after a switch to DHCP
    sendJson(client, 200, out);
    if (tzChanged && !networkChanged) {
        client.flush();
        applyTimeZone();
        setSyncProvider(getNtpTime); // re-syncs now (up to ~5 s) with the new rule
    }
    if (networkChanged) restartAfterSave(client);
}

// ============================================================================
// /api/factory-reset  {"confirm": "RESET"}: settings back to factory, restart
// ============================================================================
// Deletes /settings.cfg (no file = the factory values at boot) and restarts
// into the Setup page. Theme, notes and the SD card logs are kept.

static void handleFactoryReset(NetworkClient &client, const Request &req) {
    JsonDocument in;
    if (deserializeJson(in, readBody(client, req.contentLength)) || String(in["confirm"] | "") != "RESET") {
        sendResult(client, 400, "Type RESET to confirm.");
        return;
    }
    sdLogEvent("CONFIG", "source=web field=factory-reset user=" + String(config.webUser));
    Serial.println("Factory reset from the web page: settings deleted, restarting.");
    LittleFS.remove("/settings.cfg");
    JsonDocument out;
    out["ok"] = true;
    sendJson(client, 200, out);
    restartAfterSave(client);
}

// ============================================================================
// /api/setup: first-time setup, one form and one restart
// ============================================================================
// Only while no web login is set (a new or erased board); needs no login itself,
// like setting the first login. Everything is checked before anything is saved.

static void handleSetup(NetworkClient &client, const Request &req) {
    if (loginSet()) { sendResult(client, 403, "Setup is already done: log in and use the tabs."); return; }
    JsonDocument in;
    if (deserializeJson(in, readBody(client, req.contentLength))) { sendResult(client, 400, "Bad JSON."); return; }

    String user = in["user"] | "";
    String pass = in["pass"] | "";
    if (!validUser(user)) { sendResult(client, 400, "User: 1-31 letters, digits, . _ -"); return; }
    if (pass.length() < 8 || pass.length() >= sizeof(config.webPass)) { sendResult(client, 400, "Password: 8-63 characters."); return; }

    SystemConfig next = config;
    const char* error = applyConfigJson(in, next);
    if (error) { sendResult(client, 400, error); return; }
    if (strcmp(next.nodeID, DEFAULT_NODE_ID) == 0) { sendResult(client, 400, "Choose a controller name other than " DEFAULT_NODE_ID "."); return; }

    if (in["wifiSsid"].is<const char*>()) { // optional WiFi backup; empty = none
        String ssid = in["wifiSsid"].as<String>();
        String wpass = in["wifiPass"] | "";
        if (ssid.length() > 32) { sendResult(client, 400, "WiFi name: up to 32 characters."); return; }
        if (ssid.length() && wpass.length() && (wpass.length() < 8 || wpass.length() > 63)) { sendResult(client, 400, "WiFi password: 8-63 characters."); return; }
        strlcpy(next.wifiSsid, ssid.c_str(), sizeof(next.wifiSsid));
        strlcpy(next.wifiPass, ssid.length() ? wpass.c_str() : "", sizeof(next.wifiPass));
    }

    strlcpy(next.webUser, user.c_str(), sizeof(next.webUser));
    strlcpy(next.webPass, pass.c_str(), sizeof(next.webPass));
    config = next;
    saveSettings();
    Serial.println("Setup page: settings saved, restarting.");
    sdLogEvent("CONFIG", "source=web field=setup user=" + user + " node=" + String(config.nodeID));

    JsonDocument out;
    out["ok"] = true;
    out["hostname"] = deviceHostname(); // from the new node ID
    out["ip"] = config.ethDhcp ? String("") : config.ip.toString();
    sendJson(client, 200, out);
    restartAfterSave(client);
}

// ============================================================================
// /api/override  {"active": bool} and/or {"pct": 0-100}
// ============================================================================
// A live action, never saved: override always boots to off.

static void handleOverride(NetworkClient &client, const Request &req) {
    JsonDocument in;
    if (deserializeJson(in, readBody(client, req.contentLength))) {
        sendResult(client, 400, "Bad JSON.");
        return;
    }
    bool wasActive = manualOverrideActive;
    int wasDuty = manualOverrideDutyCycle;
    if (in["active"].is<bool>()) {
        manualOverrideActive = in["active"];
        if (manualOverrideActive && !wasActive) manualOverrideDutyCycle = 255; // starts at full speed
    }
    if (in["pct"].is<int>() && manualOverrideActive) {
        manualOverrideDutyCycle = (constrain((int)in["pct"], 0, 100) * 255 + 50) / 100;
    }
    int pct = (manualOverrideDutyCycle * 100 + 127) / 255;
    if (manualOverrideActive != wasActive) {
        Serial.print("Override "); Serial.print(manualOverrideActive ? "ACTIVATED" : "DEACTIVATED");
        Serial.print(" via web - speed="); Serial.print(pct); Serial.println("%");
        sdLogEvent("OVERRIDE", String("source=web action=") + (manualOverrideActive ? "ON" : "OFF") + " speed=" + String(pct) + "%");
    } else if (manualOverrideDutyCycle != wasDuty) {
        Serial.print("Override SPEED changed via web: "); Serial.print(pct); Serial.println("%");
        sdLogEvent("OVERRIDE", "source=web action=SPEED speed=" + String(pct) + "%");
    }
    sendResult(client, 200);
}

#if HAS_LCD
// /api/display  {"on": bool}: LCD standby (backlight)
static void handleDisplay(NetworkClient &client, const Request &req) {
    JsonDocument in;
    if (deserializeJson(in, readBody(client, req.contentLength)) || !in["on"].is<bool>()) {
        sendResult(client, 400, "Bad JSON.");
        return;
    }
    setDisplayOn(in["on"], "web");
    sendResult(client, 200);
}
#endif

// ============================================================================
// /api/theme  {"preset": "...", "custom": {"bg": "#rrggbb", ...}}
// ============================================================================
// Stored on the board (LittleFS), so every browser gets the same look.

static const char* THEME_PRESETS[] = {"nut", "navy-gold", "classic-dark", "classic-light", "custom"};
static const char* THEME_KEYS[] = {"bg", "bg2", "panel", "border", "text", "muted", "accent", "onAccent", "ok", "warn", "error"};

static bool isHexColor(const String &s) {
    if (s.length() != 7 || s[0] != '#') return false;
    for (int i = 1; i < 7; i++) if (!isxdigit((unsigned char)s[i])) return false;
    return true;
}

static void handleGetTheme(NetworkClient &client) {
    JsonDocument doc;
    if (isLittleFsMounted() && LittleFS.exists(THEME_PATH)) {
        File f = LittleFS.open(THEME_PATH, "r");
        if (f) {
            if (deserializeJson(doc, f)) doc.clear();
            f.close();
        }
    }
    if (!doc["preset"].is<const char*>()) doc["preset"] = "nut";
    sendJson(client, 200, doc);
}

static void handlePostTheme(NetworkClient &client, const Request &req) {
    JsonDocument in;
    if (deserializeJson(in, readBody(client, req.contentLength))) {
        sendResult(client, 400, "Bad JSON.");
        return;
    }
    // Rebuild from known keys only
    JsonDocument out;
    String preset = in["preset"] | "";
    bool known = false;
    for (const char* p : THEME_PRESETS) known |= preset == p;
    if (!known) { sendResult(client, 400, "Unknown theme."); return; }
    out["preset"] = preset;
    JsonObject custom = out["custom"].to<JsonObject>();
    for (const char* k : THEME_KEYS) {
        String v = in["custom"][k] | "";
        if (v.length()) {
            if (!isHexColor(v)) { sendResult(client, 400, "Colours must be #rrggbb."); return; }
            custom[k] = v;
        }
    }
    if (!isLittleFsMounted()) { sendResult(client, 500, "Storage not available."); return; }
    File f = LittleFS.open(THEME_PATH, "w");
    if (!f) { sendResult(client, 500, "Could not save the theme."); return; }
    serializeJson(out, f);
    f.close();
    sendResult(client, 200);
}

// ============================================================================
// /api/notes  {"text": "..."}: free-text notes on the Dashboard
// ============================================================================
// Stored on the board (LittleFS /notes.json) with when and by whom they were
// last saved. UTF-8, so emojis are fine; up to NOTES_MAX bytes.

static const char* NOTES_PATH = "/notes.json";
static const size_t NOTES_MAX = 4000;

static void handleGetNotes(NetworkClient &client) {
    JsonDocument doc;
    if (isLittleFsMounted() && LittleFS.exists(NOTES_PATH)) {
        File f = LittleFS.open(NOTES_PATH, "r");
        if (f) {
            if (deserializeJson(doc, f)) doc.clear();
            f.close();
        }
    }
    if (!doc["text"].is<const char*>()) doc["text"] = "";
    doc["max"] = NOTES_MAX;
    sendJson(client, 200, doc);
}

static void handlePostNotes(NetworkClient &client, const Request &req) {
    JsonDocument in;
    if (deserializeJson(in, readBody(client, req.contentLength))) { sendResult(client, 400, "Bad JSON."); return; }
    String text = in["text"] | "";
    if (text.length() > NOTES_MAX) { sendResult(client, 400, "Notes are too long (4000 bytes max)."); return; }
    if (!isLittleFsMounted()) { sendResult(client, 500, "Storage not available."); return; }

    JsonDocument out;
    out["text"] = text;
    char saved[20] = "";
    if (timeStatus() != timeNotSet) {
        snprintf(saved, sizeof(saved), "%04d-%02d-%02d %02d:%02d", year(), month(), day(), hour(), minute());
    }
    out["saved"] = saved;
    out["by"] = config.webUser;
    File f = LittleFS.open(NOTES_PATH, "w");
    if (!f) { sendResult(client, 500, "Could not save the notes."); return; }
    serializeJson(out, f);
    f.close();

    JsonDocument res;
    res["ok"] = true;
    res["saved"] = saved;
    res["by"] = config.webUser;
    sendJson(client, 200, res);
}

// ============================================================================
// /api/login  {"user": "...", "pass": "..."}: set or change the web login
// ============================================================================
// The first login needs no authorization; changing it needs the current one.
// Forgotten: erase the settings over USB (docs/BOARDS.md).

static void handleSetLogin(NetworkClient &client, const Request &req) {
    if (loginSet() && !authorized(req)) {
        sendResult(client, 401, "Login required.");
        return;
    }
    JsonDocument in;
    if (deserializeJson(in, readBody(client, req.contentLength))) {
        sendResult(client, 400, "Bad JSON.");
        return;
    }
    String user = in["user"] | "";
    String pass = in["pass"] | "";
    if (!validUser(user)) { sendResult(client, 400, "User: 1-31 letters, digits, . _ -"); return; }
    if (pass.length() < 8 || pass.length() >= sizeof(config.webPass)) { sendResult(client, 400, "Password: 8-63 characters."); return; }
    strlcpy(config.webUser, user.c_str(), sizeof(config.webUser));
    strlcpy(config.webPass, pass.c_str(), sizeof(config.webPass));
    saveSettings();
    Serial.println("Web login changed.");
    sdLogEvent("CONFIG", "source=web field=login user=" + user);
    sendResult(client, 200);
}

// ============================================================================
// /api/ota  raw firmware.bin body
// ============================================================================

static void handleOta(NetworkClient &client, const Request &req) {
    // A real read, so the linker keeps OTA_BOARD_MARKER in this image too
    if (*(volatile const char*)OTA_BOARD_MARKER != '@') return;
    if (req.contentLength < 4096) { sendResult(client, 400, "No firmware file."); return; }
    if (!Update.begin(req.contentLength, U_FLASH)) {
        sendResult(client, 400, Update.errorString());
        return;
    }
    Serial.print("OTA: receiving "); Serial.print(req.contentLength); Serial.println(" bytes");

    // Built at run time so the search pattern itself isn't a match in our image
    String pattern = String("@@BO") + "ARD=";
    String tail;               // end of the previous chunk, for markers split across chunks
    String foundBoard;         // board name found in the image
    uint8_t buf[1460];
    size_t received = 0;
    unsigned long lastData = millis();
    const char* error = nullptr;

    while (received < req.contentLength) {
        int avail = client.available();
        if (avail <= 0) {
            if (!client.connected() || millis() - lastData > 10000) { error = "Upload stalled."; break; }
            delay(1);
            continue;
        }
        int n = client.read(buf, min((size_t)avail, min(sizeof(buf), req.contentLength - received)));
        if (n <= 0) continue;
        lastData = millis();
        if (received == 0 && buf[0] != 0xE9) { error = "Not a firmware file (.bin)."; break; } // ESP image magic
        received += n;

        if (foundBoard.length() == 0) {
            String window = tail;
            for (int i = 0; i < n; i++) window += buf[i] ? (char)buf[i] : '\x01';
            int at = window.indexOf(pattern);
            int end = at >= 0 ? window.indexOf("@@", at + pattern.length()) : -1;
            if (end > 0) {
                foundBoard = window.substring(at + pattern.length(), end);
                if (foundBoard != BOARD_NAME) { error = "This firmware is for another board."; break; }
            }
            tail = window.substring(max(0, (int)window.length() - 48));
        }
        if (Update.write(buf, n) != (size_t)n) { error = Update.errorString(); break; }
    }

    if (!error && foundBoard.length() == 0) error = "Not a firmware for this project (no board name found).";
    if (error) {
        Update.abort();
        Serial.print("OTA aborted: "); Serial.println(error);
        JsonDocument out;
        out["ok"] = false;
        out["error"] = error;
        if (foundBoard.length() && foundBoard != BOARD_NAME) out["error"] = "This firmware is for the " + foundBoard + ", this board is the " BOARD_NAME ".";
        sendJson(client, 400, out);
        return;
    }
    if (!Update.end(true)) {
        Serial.print("OTA failed: "); Serial.println(Update.errorString());
        sendResult(client, 400, Update.errorString());
        return;
    }
    Serial.println("OTA: done, restarting.");
    sdLogEvent("OTA", "source=web bytes=" + String(received));
    sendResult(client, 200);
    client.flush();
    client.stop();
    delay(1000);
    LittleFS.end();
    delay(200);
    ESP.restart();
}


// ============================================================================
// /api/wifi, /api/wifi/scan, /api/wifi/forget, /api/hotspot
// ============================================================================
// WiFi is a backup for Ethernet (fan_network.cpp). Passwords are never sent.

static void addIp(JsonObject o, const char* key, uint32_t v) {
    o[key] = v ? IPAddress(v).toString() : String("");
}

static void handleGetWifi(NetworkClient &client) {
    JsonDocument doc;
    JsonObject s = doc["status"].to<JsonObject>();
    networkStatus(s);
    doc["ssid"] = config.wifiSsid;
    doc["passSet"] = config.wifiPass[0] != '\0';
    doc["static"] = config.wifiStatic;
    JsonObject o = doc.as<JsonObject>();
    addIp(o, "ip", config.wifiIp);
    addIp(o, "gateway", config.wifiGateway);
    addIp(o, "subnet", config.wifiSubnet);
    addIp(o, "dns", config.wifiDns);
    doc["hostname"] = config.hostname;
    doc["hostnameShown"] = deviceHostname();
    sendJson(client, 200, doc);
}

static bool validHostname(const String &h) {
    if (h.length() == 0) return true; // empty = from the node ID
    if (h.length() >= sizeof(config.hostname) || h[0] == '-' || h[h.length() - 1] == '-') return false;
    for (size_t i = 0; i < h.length(); i++) {
        if (!isalnum((unsigned char)h[i]) && h[i] != '-') return false;
    }
    return true;
}

// Applies only the keys present
static void handlePostWifi(NetworkClient &client, const Request &req) {
    JsonDocument in;
    if (deserializeJson(in, readBody(client, req.contentLength))) { sendResult(client, 400, "Bad JSON."); return; }
    SystemConfig next = config;
    if (in["ssid"].is<const char*>()) {
        String ssid = in["ssid"].as<String>();
        if (ssid.length() < 1 || ssid.length() > 32) { sendResult(client, 400, "SSID: 1-32 characters."); return; }
        strlcpy(next.wifiSsid, ssid.c_str(), sizeof(next.wifiSsid));
    }
    if (in["pass"].is<const char*>()) { // blank = keep
        String pass = in["pass"].as<String>();
        if (pass.length() && (pass.length() < 8 || pass.length() > 63)) { sendResult(client, 400, "WiFi password: 8-63 characters."); return; }
        if (pass.length()) strlcpy(next.wifiPass, pass.c_str(), sizeof(next.wifiPass));
    }
    if (in["static"].is<bool>()) next.wifiStatic = in["static"];
    const char* keys[] = {"ip", "gateway", "subnet", "dns"};
    uint32_t* fields[] = {&next.wifiIp, &next.wifiGateway, &next.wifiSubnet, &next.wifiDns};
    for (int i = 0; i < 4; i++) {
        if (!in[keys[i]].is<const char*>()) continue;
        String v = in[keys[i]].as<String>();
        IPAddress a;
        if (v.length() == 0) { *fields[i] = 0; continue; }
        if (!parseIp(v, a)) { sendResult(client, 400, "Invalid IP address."); return; }
        *fields[i] = (uint32_t)a;
    }
    if (next.wifiStatic && (!next.wifiIp || !next.wifiGateway || !next.wifiSubnet)) {
        sendResult(client, 400, "Static WiFi address needs IP, gateway and subnet.");
        return;
    }
    if (in["hostname"].is<const char*>()) {
        String h = in["hostname"].as<String>();
        h.toLowerCase();
        if (!validHostname(h)) { sendResult(client, 400, "Device name: letters, digits and hyphens."); return; }
        strlcpy(next.hostname, h.c_str(), sizeof(next.hostname));
    }
    config = next;
    saveSettings();
    wifiReconfigure();
    sendResult(client, 200);
}

static void handleForgetWifi(NetworkClient &client) {
    memset(config.wifiSsid, 0, sizeof(config.wifiSsid));
    memset(config.wifiPass, 0, sizeof(config.wifiPass));
    saveSettings();
    wifiReconfigure();
    Serial.println("WiFi: saved network forgotten.");
    sendResult(client, 200);
}

// First call starts a scan (202); later calls return the networks (200)
static void handleScan(NetworkClient &client) {
    static bool started = false;
    JsonDocument doc;
    if (!started) {
        wifiScanStart();
        started = true;
        doc["scanning"] = true;
        sendJson(client, 202, doc);
        return;
    }
    JsonArray list = doc["networks"].to<JsonArray>();
    if (!wifiScanCollect(list)) {
        doc.remove("networks");
        doc["scanning"] = true;
        sendJson(client, 202, doc);
        return;
    }
    started = false;
    sendJson(client, 200, doc);
}

static void handleHotspot(NetworkClient &client, const Request &req) {
    JsonDocument in;
    if (deserializeJson(in, readBody(client, req.contentLength))) { sendResult(client, 400, "Bad JSON."); return; }
    String pass = in["pass"] | "";
    if (pass.length() < 8 || pass.length() > 63) { sendResult(client, 400, "Hotspot password: 8-63 characters."); return; }
    strlcpy(config.apPass, pass.c_str(), sizeof(config.apPass));
    saveSettings();
    sendResult(client, 200);
}


// ============================================================================
// /api/history/day, /api/history/files, /api/history/file  (SD card, read-only)
// ============================================================================
// Per-minute rows live in /logs/YYYY-MM.csv in time order (sd_logger.cpp), so
// a day is found by bisecting the file instead of reading up to ~1.7 MB.
// Rows written back from the internal buffer after a card-out sit at the end
// of the month file, out of order; the day view can miss those (rare).

static String queryParam(const String &query, const char* name) {
    String key = String(name) + "=";
    int start = 0;
    while (start < (int)query.length()) {
        int end = query.indexOf('&', start);
        if (end < 0) end = query.length();
        if (query.substring(start, start + key.length()) == key) return query.substring(start + key.length(), end);
        start = end + 1;
    }
    return "";
}

static bool validDate(const String &d) {
    if (d.length() != 10 || d[4] != '-' || d[7] != '-') return false;
    for (int i : {0, 1, 2, 3, 5, 6, 8, 9}) if (!isdigit((unsigned char)d[i])) return false;
    return true;
}

// Response without Content-Length: the connection closes at the end
static void sendStreamHead(NetworkClient &client, const char* type, const String &extra = "") {
    client.println("HTTP/1.1 200 OK");
    client.print("Content-Type: "); client.println(type);
    client.println("Cache-Control: no-store");
    if (extra.length()) client.println(extra);
    client.println("Connection: close");
    client.println();
}

// Offset of the first line dated >= date (lines start "YYYY-MM-DD ...")
static size_t findDayStart(File &f, const String &date) {
    size_t lo = 0, hi = f.size();
    while (hi - lo > 512) {
        size_t mid = lo + (hi - lo) / 2;
        f.seek(mid);
        f.readStringUntil('\n');              // rest of a partial line
        String line = f.readStringUntil('\n');
        if (line.length() < 10 || line.substring(0, 10) >= date) hi = mid;
        else lo = mid;
    }
    f.seek(lo);
    if (lo > 0) f.readStringUntil('\n');      // everything before here is an earlier day
    return f.position();
}

static void handleHistoryDay(NetworkClient &client, const Request &req) {
    String date = queryParam(req.query, "date");
    if (!validDate(date)) { sendResult(client, 400, "date=YYYY-MM-DD needed."); return; }
    if (!isSdCardPresent()) { sendResult(client, 404, "No SD card."); return; }
    String path = "/logs/" + date.substring(0, 7) + ".csv";
    sendStreamHead(client, "text/csv; charset=utf-8");
    if (!SD.exists(path)) return;             // no data that month: empty answer
    File f = SD.open(path, FILE_READ);
    if (!f) return;
    f.seek(findDayStart(f, date));
    String out;
    out.reserve(1500);
    while (f.available()) {
        String line = f.readStringUntil('\n');
        if (line.length() < 10 || !isdigit((unsigned char)line[0])) continue; // column names
        String d = line.substring(0, 10);
        if (d < date) continue;
        if (d > date) break;
        out += line;
        out += '\n';
        if (out.length() > 1200) { client.print(out); out = ""; }
    }
    if (out.length()) client.print(out);
    f.close();
}

// The downloadable files: month logs, daily rollups, all-time record, events
static bool downloadable(const String &path) {
    if (path.indexOf("..") >= 0) return false;
    if (path == "/rollups/daily.csv" || path == "/rollups/alltime.csv" || path == "/events.csv") return true;
    return path.startsWith("/logs/") && path.endsWith(".csv") && path.indexOf('/', 6) < 0;
}

static void handleHistoryFiles(NetworkClient &client) {
    JsonDocument doc;
    doc["sd"] = isSdCardPresent();
    JsonArray files = doc["files"].to<JsonArray>();
    if (isSdCardPresent()) {
        auto add = [&](const String &path) {
            File f = SD.open(path, FILE_READ);
            if (!f) return;
            JsonObject o = files.add<JsonObject>();
            o["path"] = path;
            o["size"] = (uint32_t)f.size();
            f.close();
        };
        File dir = SD.open("/logs");
        if (dir) {
            for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
                String name = e.name();
                bool isDir = e.isDirectory();
                e.close();
                if (!isDir && name.endsWith(".csv")) add("/logs/" + name);
            }
            dir.close();
        }
        add("/rollups/daily.csv");
        add("/rollups/alltime.csv");
        add("/events.csv");
    }
    sendJson(client, 200, doc);
}

static void handleHistoryFile(NetworkClient &client, const Request &req) {
    String path = queryParam(req.query, "path");
    path.replace("%2F", "/");
    path.replace("%2f", "/");
    if (!downloadable(path)) { sendResult(client, 400, "Not a downloadable file."); return; }
    if (!isSdCardPresent()) { sendResult(client, 404, "No SD card."); return; }
    File f = SD.open(path, FILE_READ);
    if (!f) { sendResult(client, 404, "File not found."); return; }

    // ?tail=N: only the last ~N bytes, from a line start (the event list)
    long tail = queryParam(req.query, "tail").toInt();
    if (tail > 0) {
        sendStreamHead(client, "text/csv; charset=utf-8");
        if ((size_t)tail < f.size()) {
            f.seek(f.size() - tail);
            f.readStringUntil('\n');          // partial line
        }
        uint8_t buf[1024];
        while (f.available()) {
            int n = f.read(buf, sizeof(buf));
            if (n <= 0 || client.write(buf, n) == 0) break;
        }
        f.close();
        return;
    }

    String name = path.substring(path.lastIndexOf('/') + 1);
    // Files from before 2026-09-28 have no column-name line: add it
    String header = isdigit(f.peek()) || f.peek() == 'A' ? csvHeaderFor(path) : String("");
    if (header.length()) header += "\n";
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/csv; charset=utf-8");
    client.print("Content-Length: "); client.println((uint32_t)f.size() + header.length());
    client.print("Content-Disposition: attachment; filename=\""); client.print(name); client.println("\"");
    client.println("Cache-Control: no-store");
    client.println("Connection: close");
    client.println();
    if (header.length()) client.print(header);
    uint8_t buf[1024];
    while (f.available()) {
        int n = f.read(buf, sizeof(buf));
        if (n <= 0 || client.write(buf, n) == 0) break;
    }
    f.close();
}

// ============================================================================
// ROUTER
// ============================================================================

static const int WEB_SLOTS = 4;
static const unsigned long WEB_IDLE_MS = 5000;  // a parked connection that sends nothing is closed

void webServerLoop() {
    static NetworkClient slot[WEB_SLOTS];
    static unsigned long since[WEB_SLOTS];
    for (int i = 0; i < WEB_SLOTS; i++) {         // park new connections in free slots
        if (slot[i]) continue;
        NetworkClient c = server.accept();
        if (!c) break;
        slot[i] = c;
        since[i] = millis();
    }
    bool served = false;                         // at most one request per pass
    for (int i = 0; i < WEB_SLOTS; i++) {
        if (!slot[i]) continue;
        if (!served && slot[i].available()) {
            NetworkClient c = slot[i];
            slot[i] = NetworkClient();
            handleNativeWebTraffic(c);
            served = true;
        } else if (millis() - since[i] > WEB_IDLE_MS) {
            slot[i].stop();
            slot[i] = NetworkClient();
        }
    }
}

void handleNativeWebTraffic(NetworkClient &client) {
    Request req;
    if (!readRequest(client, req)) {
        client.stop();
        return;
    }
    bool get = req.method == "GET";
    bool post = req.method == "POST";

    if (get && (req.path == "/" || req.path == "/index.html")) sendPage(client);
    else if (get && req.path == "/api/status") handleStatus(client);
    else if (get && req.path == "/api/config") handleGetConfig(client);
    else if (get && req.path == "/api/theme") handleGetTheme(client);
    else if (get && req.path == "/api/wifi") handleGetWifi(client);
    else if (get && req.path == "/api/notes") handleGetNotes(client);
    else if (get && req.path == "/api/history/day") handleHistoryDay(client, req);
    else if (get && req.path == "/api/history/files") handleHistoryFiles(client);
    else if (get && req.path == "/api/history/file") handleHistoryFile(client, req);
    else if (get && req.path == "/api/wifi/scan") handleScan(client);
    else if (get && req.path == "/api/auth") {
        if (!loginSet()) sendResult(client, 403, "No login set.");
        else if (authorized(req)) sendResult(client, 200);
        else sendResult(client, 401, "Wrong user or password.");
    }
    else if (post && req.path == "/api/login") handleSetLogin(client, req);
    else if (post && req.path == "/api/setup") handleSetup(client, req);
    else if (post && req.path == "/api/factory-reset") { if (requireLogin(client, req)) handleFactoryReset(client, req); }
    else if (post && req.path == "/api/config") { if (requireLogin(client, req)) handlePostConfig(client, req); }
    else if (post && req.path == "/api/override") { if (requireLogin(client, req)) handleOverride(client, req); }
#if HAS_LCD
    else if (post && req.path == "/api/display") { if (requireLogin(client, req)) handleDisplay(client, req); }
#endif
    else if (post && req.path == "/api/theme") { if (requireLogin(client, req)) handlePostTheme(client, req); }
    else if (post && req.path == "/api/ota") { if (requireLogin(client, req)) handleOta(client, req); }
    else if (post && req.path == "/api/wifi") { if (requireLogin(client, req)) handlePostWifi(client, req); }
    else if (post && req.path == "/api/wifi/forget") { if (requireLogin(client, req)) handleForgetWifi(client); }
    else if (post && req.path == "/api/hotspot") { if (requireLogin(client, req)) handleHotspot(client, req); }
    else if (post && req.path == "/api/notes") { if (requireLogin(client, req)) handlePostNotes(client, req); }
    else sendResult(client, 404, "Not found.");

    client.flush();
    delay(1);
    client.stop();
}
