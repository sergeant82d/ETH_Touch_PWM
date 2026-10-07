#include "mqtt.h"
#include "config.h"
#include "sensors.h"
#include "fan_network.h"
#include "sd_logger.h"
#include "pins.h"
#if HAS_LCD
#include "display.h"
#endif
#include <math.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

// Topics: <nodeID>/<object>, e.g. fanController_02/local_temp (retained).
// Availability: <nodeID>/status = online | offline (Last Will, retained).
// Discovery: homeassistant/<component>/<nodeID>/<object>/config (retained).
// Commands: <nodeID>/<object>/set for the thresholds and manual override
// (docs/MQTT.md); state is published back after every command.
// Network temperature: HA sends it to <nodeID>/network_temp/set (an HA
// automation, docs/MQTT.md); older than 5 minutes = network probe failed.

static NetworkClient net;
static PubSubClient mqtt(net);
static String activeNode;              // nodeID of the current connection
static bool reconfigureRequested = false;
static bool attemptNow = true;
static unsigned long lastAttemptMs = 0;
static int lastConnectState = MQTT_DISCONNECTED;
static const unsigned long RETRY_MS = 15000; // connect attempts block briefly; space them out

bool isValidNodeId(const char* id) {
    size_t len = strlen(id);
    if (len == 0 || len > 63) return false;
    for (size_t i = 0; i < len; i++) {
        char c = id[i];
        if (!isalnum((unsigned char)c) && c != '_' && c != '-') return false;
    }
    return true;
}

static bool mqttWanted() {
    return config.mqttBroker[0] != '\0' && isValidNodeId(config.nodeID) &&
           strcmp(config.nodeID, DEFAULT_NODE_ID) != 0;
}

static String topic(const String &object) {
    return activeNode + "/" + object;
}

// ============================================================================
// HOME ASSISTANT DISCOVERY
// ============================================================================

struct EntityDef {
    const char* component;
    String object;
    String name;
    const char* deviceClass; // nullptr = none
    const char* unit;
    const char* icon;
    bool measurement;
    bool diagnostic;
    // number entities only
    float numMin = 0, numMax = 0, numStep = 0;
    const char* numMode = nullptr; // "box" | "slider"
};

static const char* DEG_C = "\xC2\xB0" "C"; // "°C"

// HA lists a device's entities alphabetically by name within each card, so
// the names group them. Sensors: "Air temperature ...", "Fan duty N",
// "Fan speed N", "Fault ...", "SD card", "Summary of the day". Controls: "Fan curve start/top" (tMin/tMax),
// "Manual override", "Manual override speed".
// Every entity the device can have (all NUM_FANS fans), so the ones no
// longer wanted can be removed from HA as well as the wanted ones added.
static int buildEntities(EntityDef *out) {
    int n = 0;
    out[n++] = {"sensor", "local_temp", "Air temperature local", "temperature", DEG_C, nullptr, true, false};
    out[n++] = {"sensor", "network_temp", "Air temperature network", "temperature", DEG_C, nullptr, true, false};
    out[n++] = {"sensor", "blended_temp", "Air temperature blended", "temperature", DEG_C, nullptr, true, false};
    out[n++] = {"binary_sensor", "local_probe_fault", "Fault local probe", "problem", nullptr, nullptr, false, false};
    out[n++] = {"binary_sensor", "network_probe_fault", "Fault network probe", "problem", nullptr, nullptr, false, false};
    out[n++] = {"sensor", "ip", "IP address", nullptr, nullptr, "mdi:ip-network", false, true};
    out[n++] = {"sensor", "uptime", "Uptime", "duration", "s", nullptr, false, true};
    out[n++] = {"sensor", "daily_summary", "Summary of the day", nullptr, nullptr, "mdi:calendar-today", false, false};
    out[n++] = {"sensor", "sd_card", "SD card", nullptr, nullptr, "mdi:sd", false, false};
    out[n++] = {"sensor", "sd_used", "SD card used", nullptr, "%", "mdi:sd", true, true};
    out[n++] = {"binary_sensor", "sd_fault", "Fault SD card", "problem", nullptr, nullptr, false, false};
    out[n++] = {"number", "t_min", "Fan curve start", "temperature", DEG_C, "mdi:thermometer-low", false, false, 0, 100, 0.1, "box"};
    out[n++] = {"number", "t_max", "Fan curve top", "temperature", DEG_C, "mdi:thermometer-high", false, false, 0, 100, 0.1, "box"};
    out[n++] = {"switch", "override", "Manual override", nullptr, nullptr, "mdi:hand-back-right", false, false};
    out[n++] = {"number", "override_speed", "Manual override speed", nullptr, "%", "mdi:fan", false, false, 0, 100, 1, "slider"};
#if HAS_LCD
    out[n++] = {"switch", "display", "LCD display", nullptr, nullptr, "mdi:monitor", false, false}; // standby
#endif
    for (int i = 1; i <= NUM_FANS; i++) {
        String f = "fan" + String(i);
        out[n++] = {"sensor", f + "_rpm", "Fan speed " + String(i), nullptr, "RPM", "mdi:fan", true, false};
        out[n++] = {"sensor", f + "_duty", "Fan duty " + String(i), nullptr, "%", "mdi:fan-chevron-up", true, false};
        out[n++] = {"binary_sensor", f + "_fault", "Fault fan " + String(i), "problem", nullptr, nullptr, false, false};
    }
    return n;
}

static const int MAX_ENTITIES = 16 + 3 * 4;

// Fan entities beyond config.fanCount are unwanted
static bool entityWanted(const EntityDef &e) {
    if (!e.object.startsWith("fan")) return true;
    int fan = e.object.substring(3, 4).toInt();
    return fan >= 1 && fan <= config.fanCount;
}

static String configTopic(const String &node, const EntityDef &e) {
    return String("homeassistant/") + e.component + "/" + node + "/" + e.object + "/config";
}

// wanted = publish the entity's config; otherwise an empty retained
// message, which removes the entity from HA.
static void publishEntityConfig(const String &node, const EntityDef &e, bool wanted) {
    String t = configTopic(node, e);
    if (!wanted) {
        mqtt.publish(t.c_str(), "", true);
        return;
    }
    JsonDocument doc;
    doc["name"] = e.name;
    doc["unique_id"] = node + "_" + e.object;
    doc["state_topic"] = node + "/" + e.object;
    doc["availability_topic"] = node + "/status";
    if (e.deviceClass) doc["device_class"] = e.deviceClass;
    if (e.unit) doc["unit_of_measurement"] = e.unit;
    if (e.icon) doc["icon"] = e.icon;
    if (e.measurement) doc["state_class"] = "measurement";
    if (e.unit == DEG_C && strcmp(e.component, "sensor") == 0) doc["suggested_display_precision"] = 1;
    if (strcmp(e.component, "number") == 0 || strcmp(e.component, "switch") == 0) {
        doc["command_topic"] = node + "/" + e.object + "/set";
    }
    if (e.object == "daily_summary") doc["json_attributes_topic"] = node + "/daily_summary/attributes";
    if (e.numMode) {
        doc["min"] = e.numMin;
        doc["max"] = e.numMax;
        doc["step"] = e.numStep;
        doc["mode"] = e.numMode;
    }
    if (e.diagnostic) doc["entity_category"] = "diagnostic";
    JsonObject dev = doc["device"].to<JsonObject>();
    dev["identifiers"].to<JsonArray>().add(node);
    dev["name"] = node;
    dev["manufacturer"] = "DIY";
    dev["model"] = "ETH Touch PWM fan controller";
    dev["configuration_url"] = "http://" + localIP().toString() + "/";

    String payload;
    serializeJson(doc, payload);
    if (!mqtt.publish(t.c_str(), payload.c_str(), true)) {
        Serial.print("MQTT: discovery publish failed: "); Serial.println(t);
    }
}

// removeAll = the node is going away (nodeID changed / MQTT turned off)
static void publishDiscovery(const String &node, bool removeAll) {
    EntityDef entities[MAX_ENTITIES];
    int n = buildEntities(entities);
    for (int i = 0; i < n; i++) {
        publishEntityConfig(node, entities[i], !removeAll && entityWanted(entities[i]));
    }
}

// ============================================================================
// STATE (retained; on change, RPM with a dead band, diagnostics every 60 s)
// ============================================================================

// Slots: 0-4 fixed sensors, 5-12 duty/fault per fan, 13-16 controls, 17-19 SD card
static const int SLOT_T_MIN = 13, SLOT_T_MAX = 14, SLOT_OVERRIDE = 15, SLOT_OVERRIDE_SPEED = 16;
static const int SLOT_SD = 17, SLOT_SD_USED = 18, SLOT_SD_FAULT = 19;
static const int SLOT_DISPLAY = 20;
static const int SLOT_COUNT = 21;
static String lastSent[SLOT_COUNT];
static long lastRpm[4] = {-1, -1, -1, -1};
static unsigned long lastRpmMs[4] = {0, 0, 0, 0};
static unsigned long lastDiagMs = 0;
static String summaryDate;  // last daily summary, re-sent on connect
static String summaryAttrs;

static void publishIfChanged(int slot, const String &object, const String &payload, bool force) {
    if (!force && lastSent[slot] == payload) return;
    if (mqtt.publish(topic(object).c_str(), payload.c_str(), true)) lastSent[slot] = payload;
}

static String tempPayload(bool healthy, float c) {
    return healthy ? String(c, 1) : String("None"); // "None" = unknown in HA
}

static void publishState(bool force) {
    publishIfChanged(0, "local_temp", tempPayload(localSensorHealthy, localTempC), force);
    publishIfChanged(1, "network_temp", tempPayload(networkSensorHealthy, networkTempC), force);
    publishIfChanged(2, "blended_temp", tempPayload(localSensorHealthy || networkSensorHealthy, blendedAverageC), force);
    publishIfChanged(3, "local_probe_fault", localSensorHealthy ? "OFF" : "ON", force);
    publishIfChanged(4, "network_probe_fault", networkSensorHealthy ? "OFF" : "ON", force);

    for (int i = 0; i < config.fanCount && i < NUM_FANS; i++) {
        String f = "fan" + String(i + 1);
        publishIfChanged(5 + i * 2, f + "_duty", String((currentDutyCycles[i] * 100 + 127) / 255), force);
        // Same fault rule the REST telemetry uses
        bool fault = currentDutyCycles[i] > 51 && currentRPMs[i] == 0;
        publishIfChanged(6 + i * 2, f + "_fault", fault ? "ON" : "OFF", force);

        // Tach RPM jitters by a pulse (30 RPM) every second: publish a change
        // of 60+, to/from stopped, or any change after 30 s.
        long rpm = (long)currentRPMs[i];
        bool stoppedChanged = (rpm == 0) != (lastRpm[i] == 0);
        if (force || labs(rpm - lastRpm[i]) >= 60 || stoppedChanged ||
            (rpm != lastRpm[i] && millis() - lastRpmMs[i] >= 30000)) {
            if (mqtt.publish(topic(f + "_rpm").c_str(), String(rpm).c_str(), true)) {
                lastRpm[i] = rpm;
                lastRpmMs[i] = millis();
            }
        }
    }

    // Controls: changed here by HA, or by the web page / LCD
    publishIfChanged(SLOT_T_MIN, "t_min", String(config.tMin, 1), force);
    publishIfChanged(SLOT_T_MAX, "t_max", String(config.tMax, 1), force);
    publishIfChanged(SLOT_OVERRIDE, "override", manualOverrideActive ? "ON" : "OFF", force);
    publishIfChanged(SLOT_OVERRIDE_SPEED, "override_speed", String((manualOverrideDutyCycle * 100 + 127) / 255), force);
#if HAS_LCD
    publishIfChanged(SLOT_DISPLAY, "display", isDisplayOn() ? "ON" : "OFF", force);
#endif

    // SD card health (sd_logger.cpp)
    publishIfChanged(SLOT_SD, "sd_card", sdStateText(), force);
    publishIfChanged(SLOT_SD_USED, "sd_used", sdUsedPercent() >= 0 ? String(sdUsedPercent()) : String("None"), force);
    publishIfChanged(SLOT_SD_FAULT, "sd_fault", sdState() == SD_STATE_OK ? "OFF" : "ON", force);

    if (force && summaryDate.length()) {
        mqtt.publish(topic("daily_summary").c_str(), summaryDate.c_str(), true);
        mqtt.publish(topic("daily_summary/attributes").c_str(), summaryAttrs.c_str(), true);
    }

    if (force || millis() - lastDiagMs >= 60000) {
        lastDiagMs = millis();
        mqtt.publish(topic("ip").c_str(), localIP().toString().c_str(), true);
        mqtt.publish(topic("uptime").c_str(), String(millis() / 1000).c_str(), true);
    }
}

// ============================================================================
// COMMANDS FROM HOME ASSISTANT
// ============================================================================
// Called from mqtt.loop(), i.e. from loop(): same thread as the web page and
// LCD, so the shared settings/override globals need no locking. Logged to
// Serial and SD like the web page and LCD changes.

static void setThreshold(float &field, const char* name, const String &msg) {
    float v = msg.toFloat();
    if (msg.length() == 0 || v < 0 || v > 100) {
        Serial.print("MQTT: "); Serial.print(name); Serial.print(" ignored, out of range: "); Serial.println(msg);
        return;
    }
    if (fabs(v - field) <= 0.05) return; // same value (float noise): no flash write
    Serial.print(name); Serial.print(" changed via HA: "); Serial.print(field, 1);
    Serial.print(" -> "); Serial.println(v, 1);
    sdLogEvent("CONFIG", String("source=HA field=") + name + " old=" + String(field, 1) + "C new=" + String(v, 1) + "C");
    field = v;
    saveSettings();
}

// ============================================================================
// NETWORK TEMPERATURE FROM HOME ASSISTANT
// ============================================================================
// Payload {"value": "24.1", "unit": "°F"} from the HA automation (every
// change + every minute). Anything that isn't a number ("unavailable",
// "unknown", bad JSON) = network probe failed, as does 5 minutes of silence.

static const unsigned long NETWORK_STALE_MS = 300000;
static unsigned long lastNetworkMs = 0;

static void setNetworkHealthy(bool healthy, const char* why) {
    if (healthy != networkSensorHealthy) {
        Serial.print("Network temperature: "); Serial.println(why);
        sdLogEvent("MQTT", String("network temperature: ") + why);
    }
    networkSensorHealthy = healthy;
}

static void onNetworkTemp(const String &msg) {
    JsonDocument doc;
    if (deserializeJson(doc, msg)) {
        setNetworkHealthy(false, "bad message from HA, marked failed");
        return;
    }
    String value = doc["value"].as<String>();
    String unit = doc["unit"].as<String>();
    char *end = nullptr;
    float v = strtof(value.c_str(), &end);
    if (value.length() == 0 || end == value.c_str() || *end != '\0' || isnan(v)) {
        setNetworkHealthy(false, "HA reports it unavailable, marked failed");
        return;
    }
    networkTempC = (unit.indexOf('F') != -1) ? (v - 32.0) * 5.0 / 9.0 : v;
    lastNetworkMs = millis();
    setNetworkHealthy(true, "receiving from HA");
}

// Called every loop() pass, connected or not
static void checkNetworkStale() {
    if (networkSensorHealthy && millis() - lastNetworkMs > NETWORK_STALE_MS) {
        setNetworkHealthy(false, "no update from HA for 5 minutes, marked failed");
    }
}

static void onMessage(char *t, byte *payload, unsigned int len) {
    // Copy out first: publishing below reuses the client's buffer
    String tp(t);
    String msg;
    msg.reserve(len);
    for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
    msg.trim();

    if (tp == "homeassistant/status") { // HA (re)started: announce everything again
        sdLogEvent("MQTT", "HA status: " + msg);
        if (msg == "online") {
            publishDiscovery(activeNode, false);
            publishState(true);
        }
        return;
    }
    String prefix = activeNode + "/";
    if (!tp.startsWith(prefix) || !tp.endsWith("/set")) return;
    String object = tp.substring(prefix.length(), tp.length() - 4);

    if (object == "network_temp") {
        onNetworkTemp(msg); // regular data, not a control: no state echo
        return;
    }
    if (object == "t_min") {
        setThreshold(config.tMin, "tMin", msg);
    } else if (object == "t_max") {
        setThreshold(config.tMax, "tMax", msg);
    } else if (object == "override") {
        // ON starts at full speed, like the web page and LCD
        if (msg == "ON" && !manualOverrideActive) {
            manualOverrideActive = true;
            manualOverrideDutyCycle = 255;
            Serial.println("Override ACTIVATED via HA - speed=100%");
            sdLogEvent("OVERRIDE", "source=HA action=ON speed=100%");
        } else if (msg == "OFF" && manualOverrideActive) {
            manualOverrideActive = false;
            Serial.println("Override DEACTIVATED via HA");
            sdLogEvent("OVERRIDE", "source=HA action=OFF");
        }
#if HAS_LCD
    } else if (object == "display") {
        if (msg == "ON" || msg == "OFF") setDisplayOn(msg == "ON", "HA");
#endif
    } else if (object == "override_speed") {
        // Ignored while the override is off (as before); the state
        // republished below snaps HA's slider back.
        if (manualOverrideActive) {
            int pct = constrain((int)lroundf(msg.toFloat()), 0, 100);
            int duty = (pct * 255 + 50) / 100;
            if (duty != manualOverrideDutyCycle) {
                manualOverrideDutyCycle = duty;
                Serial.print("Override SPEED changed via HA: "); Serial.print(pct); Serial.println("%");
                sdLogEvent("OVERRIDE", "source=HA action=SPEED speed=" + String(pct) + "%");
            }
        }
    }
    publishState(true); // confirm the real state, including ignored commands
}

// ============================================================================
// CONNECTION
// ============================================================================

static String announcedFor;   // node/fanCount whose discovery was last sent

// Text for a PubSubClient state, for the event log
static const char* mqttStateText(int st) {
    switch (st) {
        case MQTT_CONNECTION_TIMEOUT:      return "broker did not answer";
        case MQTT_CONNECTION_LOST:         return "connection lost";
        case MQTT_CONNECT_FAILED:          return "broker unreachable";
        case MQTT_DISCONNECTED:            return "disconnected";
        case MQTT_CONNECT_BAD_PROTOCOL:    return "bad protocol";
        case MQTT_CONNECT_BAD_CLIENT_ID:   return "client ID refused";
        case MQTT_CONNECT_UNAVAILABLE:     return "broker unavailable";
        case MQTT_CONNECT_BAD_CREDENTIALS:
        case MQTT_CONNECT_UNAUTHORIZED:    return "login refused";
        default:                           return "other";
    }
}

static void tryConnect() {
    activeNode = config.nodeID;
    mqtt.setServer(config.mqttBroker, config.mqttPort);
    const char* user = config.mqttUser[0] ? config.mqttUser : nullptr;
    const char* pass = config.mqttUser[0] ? config.mqttPass : nullptr;
    String will = topic("status");

    if (!mqtt.connect(activeNode.c_str(), user, pass, will.c_str(), 1, true, "offline")) {
        // Log changes only, not every 15 s retry
        if (mqtt.state() != lastConnectState) {
            Serial.print("MQTT: connect to "); Serial.print(config.mqttBroker);
            Serial.print(":"); Serial.print(config.mqttPort);
            Serial.print(" failed (state "); Serial.print(mqtt.state()); Serial.println("), retrying every 15 s.");
            sdLogEvent("MQTT", String("connect failed: ") + mqttStateText(mqtt.state()) + " (state " + String(mqtt.state()) + "), retrying every 15 s");
        }
        lastConnectState = mqtt.state();
        return;
    }
    lastConnectState = MQTT_CONNECTED;
    // Send each small message at once, not held back until the last one is
    // acknowledged (Nagle). The first connect after boot sends ~30 discovery
    // messages and held loop() 8.9 s on 2026-10-07, 21 s on 2026-10-06 (audit 4.1)
    net.setNoDelay(true);
    Serial.print("MQTT: connected to "); Serial.print(config.mqttBroker);
    Serial.print(" as "); Serial.println(activeNode);
    // The board's address actually used (both networks share one subnet, audit 2.1)
    sdLogEvent("MQTT", String("connected to ") + config.mqttBroker + " from " + net.localIP().toString() + " (" + activeNetwork() + ")");

    mqtt.publish(will.c_str(), "online", true);
    // Discovery is retained on the broker: resend it only when it changed
    // (node or fan count) or HA asks (its birth message, below). Every
    // reconnect used to resend ~30 messages, holding loop() up ~2 s.
    String announce = activeNode + "/" + String(config.fanCount);
    if (announce != announcedFor) {
        unsigned long t0 = millis();
        publishDiscovery(activeNode, false);
        announcedFor = announce;
        sdLogEvent("MQTT", "discovery sent in " + String((millis() - t0) / 1000.0, 1) + " s");
    }
    mqtt.subscribe((activeNode + "/+/set").c_str());
    mqtt.subscribe("homeassistant/status");
    publishState(true);
}

static void disconnectCleanly(bool removeFromHA, const char* why) {
    if (!mqtt.connected()) return;
    sdLogEvent("MQTT", String("disconnected: ") + why);
    if (removeFromHA) {
        publishDiscovery(activeNode, true);
        announcedFor = ""; // removed from HA: announce again on the next connect
    }
    mqtt.publish(topic("status").c_str(), "offline", true);
    mqtt.disconnect();
    lastConnectState = MQTT_DISCONNECTED;
}

void mqttInit() {
    mqtt.setBufferSize(1024); // discovery payloads exceed the 256-byte default
    mqtt.setSocketTimeout(5); // seconds to wait for the broker's reply
    net.setConnectionTimeout(2000); // a dead broker blocks loop() at most this long (LAN broker)
    mqtt.setCallback(onMessage);
}

// Self-heal (user, 2026-10-06): MQTT wanted, a network up, but not connected
// for 15 minutes restarts the board. Without a network a restart doesn't help
// (Ethernet without an address has its own check, fan_network.cpp). Backs off
// 15, 30, 60, 120, 240 min in a row, so a broker down for maintenance doesn't
// restart the board every 15 min (audit 2.3). The events before it say why.
static const unsigned long SELF_HEAL_MS = 15UL * 60 * 1000;
RTC_NOINIT_ATTR static uint32_t healMagic;
RTC_NOINIT_ATTR static uint32_t healCount;   // self-heal restarts in a row

static void selfHealCheck() {
    static unsigned long okSinceMs = 0;   // last moment MQTT was connected, not wanted, or had no network
    if (healMagic != 0x5E1F4EA1) { healMagic = 0x5E1F4EA1; healCount = 0; } // power-up
    if (mqtt.connected()) healCount = 0;
    if (!mqttWanted() || mqtt.connected() || !isNetworkConnected()) { okSinceMs = millis(); return; }
    unsigned long limit = SELF_HEAL_MS << min(healCount, (uint32_t)4);
    if (millis() - okSinceMs < limit) return;
    healCount++;
    String why = "MQTT not connected for " + String(limit / 60000) + " min (" + mqttStatusText() + ", network " +
                 activeNetwork() + ", " + String(healCount) + " in a row)";
    Serial.println("Self-heal restart: " + why);
    sdLogEvent("RESTART", "source=self-heal " + why);
    delay(500);
    LittleFS.end();
    delay(200);
    ESP.restart();
}

void mqttLoop() {
    checkNetworkStale();
    selfHealCheck();

    if (reconfigureRequested) {
        reconfigureRequested = false;
        // nodeID changed (or MQTT turned off): take the old device out of HA
        bool nodeGone = activeNode != config.nodeID || !mqttWanted();
        disconnectCleanly(nodeGone, nodeGone ? "node ID changed or MQTT off" : "settings or network changed");
        attemptNow = true;
    }

    if (!mqttWanted() || !isNetworkConnected()) {
        disconnectCleanly(false, !mqttWanted() ? "MQTT off" : "no network");
        return;
    }

    // A connection that dropped by itself (broker gone, network stalled)
    if (lastConnectState == MQTT_CONNECTED && !mqtt.connected()) {
        lastConnectState = mqtt.state();
        Serial.print("MQTT: connection lost (state "); Serial.print(lastConnectState); Serial.println(")");
        sdLogEvent("MQTT", String("connection lost: ") + mqttStateText(lastConnectState) + " (state " + String(lastConnectState) + ")");
    }

    if (!mqtt.connected()) {
        if (attemptNow || millis() - lastAttemptMs >= RETRY_MS) {
            attemptNow = false;
            lastAttemptMs = millis();
            sdLoggerMarkStage("mqtt:conn");
            tryConnect();
        }
        return;
    }

    sdLoggerMarkStage("mqtt:loop");
    mqtt.loop();
    static unsigned long lastStateMs = 0;
    if (millis() - lastStateMs >= 1000) { // sensors update once a second
        lastStateMs = millis();
        sdLoggerMarkStage("mqtt:pub");
        publishState(false);
    }
}

// Range as [min, max] in the attributes; null when no samples that day
static void addRange(JsonDocument &doc, const char* minKey, const char* maxKey, float lo, float hi, bool asInt) {
    if (lo > hi) {
        doc[minKey] = nullptr;
        doc[maxKey] = nullptr;
    } else if (asInt) {
        doc[minKey] = (long)lo;
        doc[maxKey] = (long)hi;
    } else {
        doc[minKey] = serialized(String(lo, 1));
        doc[maxKey] = serialized(String(hi, 1));
    }
}

void mqttPublishDailySummary(const String &date,
                             float localMinC, float localMaxC,
                             float netMinC, float netMaxC,
                             float blendMinC, float blendMaxC,
                             long fan1MinRpm, long fan1MaxRpm,
                             long fan2MinRpm, long fan2MaxRpm) {
    JsonDocument doc;
    addRange(doc, "local_min_c", "local_max_c", localMinC, localMaxC, false);
    addRange(doc, "net_min_c", "net_max_c", netMinC, netMaxC, false);
    addRange(doc, "blend_min_c", "blend_max_c", blendMinC, blendMaxC, false);
    addRange(doc, "fan1_min_rpm", "fan1_max_rpm", fan1MinRpm, fan1MaxRpm, true);
    addRange(doc, "fan2_min_rpm", "fan2_max_rpm", fan2MinRpm, fan2MaxRpm, true);
    summaryAttrs = "";
    serializeJson(doc, summaryAttrs);
    summaryDate = date;
    if (mqtt.connected()) {
        mqtt.publish(topic("daily_summary").c_str(), summaryDate.c_str(), true);
        mqtt.publish(topic("daily_summary/attributes").c_str(), summaryAttrs.c_str(), true);
        Serial.println("Daily summary published to MQTT.");
    } else {
        Serial.println("Daily summary kept; MQTT not connected, sent on reconnect.");
    }
}

void mqttReconfigure() {
    reconfigureRequested = true;
}

String mqttStatusText() {
    if (!isValidNodeId(config.nodeID)) return "Off: Node ID may only use letters, digits, _ and -";
    if (strcmp(config.nodeID, DEFAULT_NODE_ID) == 0) return String("Off: change the Node ID from ") + DEFAULT_NODE_ID;
    if (config.mqttBroker[0] == '\0') return "Off: no broker set";
    if (!isNetworkConnected()) return "Waiting for a network (Ethernet or WiFi)";
    if (mqtt.connected()) return "Connected as " + activeNode;
    switch (lastConnectState) {
        case MQTT_CONNECT_BAD_CREDENTIALS:
        case MQTT_CONNECT_UNAUTHORIZED:   return "Not connected: broker refused the login";
        case MQTT_CONNECT_FAILED:         return "Not connected: broker unreachable";
        case MQTT_CONNECTION_TIMEOUT:     return "Not connected: broker did not answer";
        default:                          return "Not connected (state " + String(lastConnectState) + "), retrying";
    }
}
