#include "mqtt.h"
#include "config.h"
#include "sensors.h"
#include "network.h"
#include <Ethernet.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// Topics: <nodeID>/<object>, e.g. fanController_02/local_temp (retained).
// Availability: <nodeID>/status = online | offline (Last Will, retained).
// Discovery: homeassistant/<component>/<nodeID>/<object>/config (retained).
// Phase 1: read-only sensors. Commands come in Phase 2 (docs/MQTT.md).

static EthernetClient net;
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
};

static const char* DEG_C = "\xC2\xB0" "C"; // "°C"

// HA lists a device's entities alphabetically by name, so the names group
// them: "Air temperature ...", "Fan duty N", "Fan speed N", "Fault ...".
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
    for (int i = 1; i <= NUM_FANS; i++) {
        String f = "fan" + String(i);
        out[n++] = {"sensor", f + "_rpm", "Fan speed " + String(i), nullptr, "RPM", "mdi:fan", true, false};
        out[n++] = {"sensor", f + "_duty", "Fan duty " + String(i), nullptr, "%", "mdi:fan-chevron-up", true, false};
        out[n++] = {"binary_sensor", f + "_fault", "Fault fan " + String(i), "problem", nullptr, nullptr, false, false};
    }
    return n;
}

static const int MAX_ENTITIES = 7 + 3 * 4;

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
    if (e.unit == DEG_C) doc["suggested_display_precision"] = 1;
    if (e.diagnostic) doc["entity_category"] = "diagnostic";
    JsonObject dev = doc["device"].to<JsonObject>();
    dev["identifiers"].to<JsonArray>().add(node);
    dev["name"] = node;
    dev["manufacturer"] = "DIY";
    dev["model"] = "ETH Touch PWM fan controller";
    dev["configuration_url"] = "http://" + Ethernet.localIP().toString() + "/";

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

static const int SLOT_COUNT = 5 + 2 * 4; // fixed states + duty/fault per fan
static String lastSent[SLOT_COUNT];
static long lastRpm[4] = {-1, -1, -1, -1};
static unsigned long lastRpmMs[4] = {0, 0, 0, 0};
static unsigned long lastDiagMs = 0;

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

    if (force || millis() - lastDiagMs >= 60000) {
        lastDiagMs = millis();
        mqtt.publish(topic("ip").c_str(), Ethernet.localIP().toString().c_str(), true);
        mqtt.publish(topic("uptime").c_str(), String(millis() / 1000).c_str(), true);
    }
}

// ============================================================================
// CONNECTION
// ============================================================================

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
        }
        lastConnectState = mqtt.state();
        return;
    }
    lastConnectState = MQTT_CONNECTED;
    Serial.print("MQTT: connected to "); Serial.print(config.mqttBroker);
    Serial.print(" as "); Serial.println(activeNode);

    mqtt.publish(will.c_str(), "online", true);
    publishDiscovery(activeNode, false);
    publishState(true);
}

static void disconnectCleanly(bool removeFromHA) {
    if (!mqtt.connected()) return;
    if (removeFromHA) publishDiscovery(activeNode, true);
    mqtt.publish(topic("status").c_str(), "offline", true);
    mqtt.disconnect();
    lastConnectState = MQTT_DISCONNECTED;
}

void mqttInit() {
    mqtt.setBufferSize(1024); // discovery payloads exceed the 256-byte default
    mqtt.setSocketTimeout(5); // seconds to wait for the broker's reply
}

void mqttLoop() {
    if (reconfigureRequested) {
        reconfigureRequested = false;
        // nodeID changed (or MQTT turned off): take the old device out of HA
        bool nodeGone = activeNode != config.nodeID || !mqttWanted();
        disconnectCleanly(nodeGone);
        attemptNow = true;
    }

    if (!mqttWanted() || !isEthernetConnected()) {
        disconnectCleanly(false);
        return;
    }

    if (!mqtt.connected()) {
        if (attemptNow || millis() - lastAttemptMs >= RETRY_MS) {
            attemptNow = false;
            lastAttemptMs = millis();
            tryConnect();
        }
        return;
    }

    mqtt.loop();
    static unsigned long lastStateMs = 0;
    if (millis() - lastStateMs >= 1000) { // sensors update once a second
        lastStateMs = millis();
        publishState(false);
    }
}

void mqttReconfigure() {
    reconfigureRequested = true;
}

String mqttStatusText() {
    if (!isValidNodeId(config.nodeID)) return "Off: Node ID may only use letters, digits, _ and -";
    if (strcmp(config.nodeID, DEFAULT_NODE_ID) == 0) return String("Off: change the Node ID from ") + DEFAULT_NODE_ID;
    if (config.mqttBroker[0] == '\0') return "Off: no broker set";
    if (!isEthernetConnected()) return "Waiting for the Ethernet link";
    if (mqtt.connected()) return "Connected as " + activeNode;
    switch (lastConnectState) {
        case MQTT_CONNECT_BAD_CREDENTIALS:
        case MQTT_CONNECT_UNAUTHORIZED:   return "Not connected: broker refused the login";
        case MQTT_CONNECT_FAILED:         return "Not connected: broker unreachable";
        case MQTT_CONNECTION_TIMEOUT:     return "Not connected: broker did not answer";
        default:                          return "Not connected (state " + String(lastConnectState) + "), retrying";
    }
}
