#include "web_server.h"
#include "config.h"
#include "pins.h"
#include "sensors.h"
#include "network.h"
#include "mqtt.h"
#include "sd_logger.h"
#include <TimeLib.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <base64.h>
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
static const size_t MAX_BODY = 4096;

// ============================================================================
// REQUEST / RESPONSE
// ============================================================================

struct Request {
    String method;
    String path;
    String auth;          // Authorization header
    size_t contentLength = 0;
};

static bool readRequest(EthernetClient &client, Request &req) {
    client.setTimeout(2000);
    String line = client.readStringUntil('\n');
    line.trim();
    int sp1 = line.indexOf(' ');
    int sp2 = line.indexOf(' ', sp1 + 1);
    if (sp1 < 0 || sp2 < 0) return false;
    req.method = line.substring(0, sp1);
    req.path = line.substring(sp1 + 1, sp2);
    int q = req.path.indexOf('?');
    if (q >= 0) req.path = req.path.substring(0, q);

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

static String readBody(EthernetClient &client, size_t len) {
    String body;
    if (len > MAX_BODY) return body;
    body.reserve(len);
    unsigned long start = millis();
    while (body.length() < len && millis() - start < 3000) {
        while (client.available() && body.length() < len) body += (char)client.read();
    }
    return body;
}

static void sendHead(EthernetClient &client, int code, const char* type, size_t len) {
    const char* text = code == 200 ? "OK" : code == 400 ? "Bad Request" : code == 401 ? "Unauthorized" :
                       code == 403 ? "Forbidden" : code == 404 ? "Not Found" : "Error";
    client.print("HTTP/1.1 "); client.print(code); client.print(' '); client.println(text);
    client.print("Content-Type: "); client.println(type);
    client.print("Content-Length: "); client.println(len);
    client.println("Cache-Control: no-store");
    client.println("Connection: close");
    client.println();
}

static void sendJson(EthernetClient &client, int code, JsonDocument &doc) {
    String out;
    serializeJson(doc, out);
    sendHead(client, code, "application/json", out.length());
    client.print(out);
}

// {"ok":false,"error":"..."} or {"ok":true}
static void sendResult(EthernetClient &client, int code, const char* error = nullptr) {
    JsonDocument doc;
    doc["ok"] = error == nullptr;
    if (error) doc["error"] = error;
    sendJson(client, code, doc);
}

static void sendPage(EthernetClient &client) {
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
static bool requireLogin(EthernetClient &client, const Request &req) {
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

static void handleStatus(EthernetClient &client) {
    JsonDocument doc;
    doc["node"] = config.nodeID;
    doc["board"] = BOARD_NAME;
    doc["sketch"] = SKETCH_FILENAME;
    doc["build"] = __DATE__ " " __TIME__;
    doc["ip"] = Ethernet.localIP().toString();
    doc["uptime"] = millis() / 1000;
    doc["heap"] = ESP.getFreeHeap();
    doc["timeSet"] = timeStatus() != timeNotSet;
    doc["time"] = (uint32_t)now(); // local time (NTP + tzOffset), seconds
    doc["fahrenheit"] = config.isFahrenheit;
    doc["clock24"] = config.is24Hour;
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
    doc["eth"] = isEthernetConnected();
    doc["mqtt"] = mqttStatusText();
    doc["mqttOk"] = mqttStatusText().startsWith("Connected");
    doc["loginSet"] = loginSet();
    sendJson(client, 200, doc);
}

static void handleGetConfig(EthernetClient &client) {
    JsonDocument doc;
    doc["fanCount"] = config.fanCount;
    doc["maxFans"] = 2; // pins exist for 2 fans on both boards (pins.h)
    doc["fahrenheit"] = config.isFahrenheit;
    doc["tMinC"] = serialized(String(config.tMin, 1));
    doc["tMaxC"] = serialized(String(config.tMax, 1));
    doc["tzOffset"] = config.tzOffset;
    doc["clock24"] = config.is24Hour;
    doc["ip"] = config.ip.toString();
    doc["subnet"] = config.subnet.toString();
    doc["gateway"] = config.gateway.toString();
    doc["dns"] = config.dns.toString();
    doc["nodeId"] = config.nodeID;
    doc["defaultNodeId"] = DEFAULT_NODE_ID;
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

// Applies only the keys present (each tab saves its own fields)
static void handlePostConfig(EthernetClient &client, const Request &req) {
    JsonDocument in;
    if (deserializeJson(in, readBody(client, req.contentLength))) {
        sendResult(client, 400, "Bad JSON.");
        return;
    }
    SystemConfig next = config;

    if (in["fanCount"].is<int>()) {
        int n = in["fanCount"];
        if (n < 1 || n > 2) { sendResult(client, 400, "Fan count must be 1 or 2."); return; }
        next.fanCount = n;
    }
    if (in["fahrenheit"].is<bool>()) next.isFahrenheit = in["fahrenheit"];
    if (in["tMinC"].is<float>() || in["tMaxC"].is<float>()) {
        float lo = in["tMinC"] | next.tMin;
        float hi = in["tMaxC"] | next.tMax;
        if (lo < 0 || lo > 100 || hi < 0 || hi > 100) { sendResult(client, 400, "Temperatures must be 0-100 C."); return; }
        if (hi <= lo) { sendResult(client, 400, "Fan curve top must be above the start."); return; }
        next.tMin = lo;
        next.tMax = hi;
    }
    if (in["tzOffset"].is<int>()) {
        int tz = in["tzOffset"];
        if (tz < -12 || tz > 14) { sendResult(client, 400, "Time zone offset must be -12 to 14."); return; }
        next.tzOffset = tz;
    }
    if (in["clock24"].is<bool>()) next.is24Hour = in["clock24"];

    const char* ipKeys[] = {"ip", "subnet", "gateway", "dns"};
    IPAddress* ipFields[] = {&next.ip, &next.subnet, &next.gateway, &next.dns};
    for (int i = 0; i < 4; i++) {
        if (in[ipKeys[i]].is<const char*>() && !parseIp(in[ipKeys[i]].as<String>(), *ipFields[i])) {
            sendResult(client, 400, "Invalid IP address.");
            return;
        }
    }

    if (in["nodeId"].is<const char*>()) {
        String id = in["nodeId"].as<String>();
        if (!isValidNodeId(id.c_str())) { sendResult(client, 400, "Node ID: letters, digits, _ and - only."); return; }
        strlcpy(next.nodeID, id.c_str(), sizeof(next.nodeID));
    }
    if (in["mqttBroker"].is<const char*>()) {
        String b = in["mqttBroker"].as<String>();
        b.trim();
        for (size_t i = 0; i < b.length(); i++) {
            if (!isalnum((unsigned char)b[i]) && b[i] != '.' && b[i] != '-') { sendResult(client, 400, "Broker: host name or IP."); return; }
        }
        if (b.length() >= sizeof(next.mqttBroker)) { sendResult(client, 400, "Broker name too long."); return; }
        strlcpy(next.mqttBroker, b.c_str(), sizeof(next.mqttBroker));
    }
    if (in["mqttPort"].is<int>()) {
        int p = in["mqttPort"];
        if (p < 1 || p > 65535) { sendResult(client, 400, "Port must be 1-65535."); return; }
        next.mqttPort = p;
    }
    if (in["mqttUser"].is<const char*>()) {
        String u = in["mqttUser"].as<String>();
        if (u.length() >= sizeof(next.mqttUser)) { sendResult(client, 400, "MQTT user too long."); return; }
        strlcpy(next.mqttUser, u.c_str(), sizeof(next.mqttUser));
    }
    if (in["mqttPass"].is<const char*>()) { // blank = keep
        String p = in["mqttPass"].as<String>();
        if (p.length() >= sizeof(next.mqttPass)) { sendResult(client, 400, "MQTT password too long."); return; }
        if (p.length()) strlcpy(next.mqttPass, p.c_str(), sizeof(next.mqttPass));
    }

    bool networkChanged = next.ip != config.ip || next.subnet != config.subnet ||
                          next.gateway != config.gateway || next.dns != config.dns;
    logThreshold("tMin", config.tMin, next.tMin);
    logThreshold("tMax", config.tMax, next.tMax);
    config = next;
    saveSettings();
    mqttReconfigure(); // node ID / broker / fan count may have changed

    JsonDocument out;
    out["ok"] = true;
    out["restart"] = networkChanged;
    sendJson(client, 200, out);
    if (networkChanged) {
        // Margin for the settings write to commit and LittleFS to unmount
        // before the reset (a restart too soon after a write once corrupted
        // LittleFS; see mountLittleFSWithRecovery()).
        client.flush();
        client.stop();
        delay(1500);
        LittleFS.end();
        delay(200);
        ESP.restart();
    }
}

// ============================================================================
// /api/override  {"active": bool} and/or {"pct": 0-100}
// ============================================================================
// A live action, never saved: override always boots to off.

static void handleOverride(EthernetClient &client, const Request &req) {
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

// ============================================================================
// /api/theme  {"preset": "...", "custom": {"bg": "#rrggbb", ...}}
// ============================================================================
// Stored on the board (LittleFS), so every browser gets the same look.

static const char* THEME_PRESETS[] = {"nut", "classic-dark", "classic-light", "custom"};
static const char* THEME_KEYS[] = {"bg", "bg2", "panel", "border", "text", "muted", "accent", "onAccent", "ok", "warn", "error"};

static bool isHexColor(const String &s) {
    if (s.length() != 7 || s[0] != '#') return false;
    for (int i = 1; i < 7; i++) if (!isxdigit((unsigned char)s[i])) return false;
    return true;
}

static void handleGetTheme(EthernetClient &client) {
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

static void handlePostTheme(EthernetClient &client, const Request &req) {
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
// /api/login  {"user": "...", "pass": "..."}: set or change the web login
// ============================================================================
// The first login needs no authorization; changing it needs the current one.
// Forgotten: erase the settings over USB (docs/BOARDS.md).

static void handleSetLogin(EthernetClient &client, const Request &req) {
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

static void handleOta(EthernetClient &client, const Request &req) {
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
// ROUTER
// ============================================================================

void handleNativeWebTraffic(EthernetClient &client) {
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
    else if (get && req.path == "/api/auth") {
        if (!loginSet()) sendResult(client, 403, "No login set.");
        else if (authorized(req)) sendResult(client, 200);
        else sendResult(client, 401, "Wrong user or password.");
    }
    else if (post && req.path == "/api/login") handleSetLogin(client, req);
    else if (post && req.path == "/api/config") { if (requireLogin(client, req)) handlePostConfig(client, req); }
    else if (post && req.path == "/api/override") { if (requireLogin(client, req)) handleOverride(client, req); }
    else if (post && req.path == "/api/theme") { if (requireLogin(client, req)) handlePostTheme(client, req); }
    else if (post && req.path == "/api/ota") { if (requireLogin(client, req)) handleOta(client, req); }
    else sendResult(client, 404, "Not found.");

    client.flush();
    delay(1);
    client.stop();
}
