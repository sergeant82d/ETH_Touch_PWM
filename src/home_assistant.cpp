#include "home_assistant.h"
#include "config.h"
#include "sensors.h"
#include "sd_logger.h"
#include <Ethernet.h>
#include <ArduinoJson.h>
#include <math.h>

void sendHomeAssistantVoiceAlert(const String &alertMessage) {
    EthernetClient client;
    if (!client.connect(config.haHost, config.haPort)) return;

    JsonDocument doc;
    doc["title"] = "Hardware Fault Alert";
    doc["message"] = alertMessage;
    doc["notification_id"] = "FAN_CTRL_ERROR";
    String jsonPayload;
    serializeJson(doc, jsonPayload);

    client.println("POST /api/services/persistent_notification/create HTTP/1.1");
    client.print("Host: "); client.println(config.haHost);
    client.print("Authorization: "); client.println(config.haToken);
    client.println("Content-Type: application/json");
    client.print("Content-Length: "); client.println(jsonPayload.length());
    client.println("Connection: close\r\n");
    client.println(jsonPayload);
    client.stop();
    Serial.println("Voice alert posted to Home Assistant.");
}

void sendHomeAssistantVoiceClear(const String &sensorType) {
    EthernetClient client;
    if (!client.connect(config.haHost, config.haPort)) return;

    JsonDocument doc;
    doc["title"] = "Hardware Status Normal";
    doc["message"] = "The " + sensorType + " thermal sensor connection has recovered. System operating normally.";
    doc["notification_id"] = "FAN_CTRL_CLEAR";
    String jsonPayload;
    serializeJson(doc, jsonPayload);

    client.println("POST /api/services/persistent_notification/create HTTP/1.1");
    client.print("Host: "); client.println(config.haHost);
    client.print("Authorization: "); client.println(config.haToken);
    client.println("Content-Type: application/json");
    client.print("Content-Length: "); client.println(jsonPayload.length());
    client.println("Connection: close\r\n");
    client.println(jsonPayload);
    client.stop();
    Serial.println("Voice recovery update posted to Home Assistant.");
}

void fetchHomeAssistantTemperature() {
    // Only log state *transitions* (first success, connection lost, pull
    // failing even though the connection succeeded) plus an infrequent
    // heartbeat - not every single 2s cycle regardless of outcome. The
    // previous version printed "Connecting to HA..." unconditionally on
    // every call with no resolution ever printed on the success path,
    // which made a perfectly healthy connection look like it was stuck
    // retrying forever in the log.
    static bool lastConnectOk = false;
    static bool lastFullSyncOk = false;
    static unsigned long lastHeartbeatMs = 0;
    const unsigned long HEARTBEAT_PERIOD_MS = 300000; // 5 min

    EthernetClient client;

    if (!client.connect(config.haHost, config.haPort)) {
        if (lastConnectOk) {
            Serial.print("HA connection lost ("); Serial.print(config.haHost);
            Serial.print(":"); Serial.print(config.haPort); Serial.println(") - now failing.");
        }
        lastConnectOk = false;
        lastFullSyncOk = false;
        networkSensorHealthy = false;
        return;
    }

    if (!lastConnectOk) {
        Serial.print("Connected to HA ("); Serial.print(config.haHost);
        Serial.print(":"); Serial.print(config.haPort); Serial.println(") successfully.");
    }
    lastConnectOk = true;

    JsonDocument outboundDoc;
    outboundDoc["state"] = String(localTempC, 1);
    JsonObject attrs = outboundDoc["attributes"].to<JsonObject>();
    attrs["device_class"] = "temperature";
    attrs["unit_scale"] = config.isFahrenheit ? "Fahrenheit" : "Celsius";
    attrs["fans_configured"] = config.fanCount;
    attrs["unit_of_measurement"] = "\xC2\xB0\x43"; // "°C"
    attrs["local_hardware_probe"] = String(localTempC, 1);
    attrs["ha_network_probe"]    = String(networkTempC, 1);
    attrs["blended_average"]     = String(blendedAverageC, 1);

    for (int i = 0; i < 4; i++) {
        String rpmKey = "fan" + String(i + 1) + "_rpm";
        String faultKey = "fan" + String(i + 1) + "_fault";
        if (i < config.fanCount) {
            attrs[rpmKey] = currentRPMs[i];
            attrs[faultKey] = (currentDutyCycles[i] > 51 && currentRPMs[i] == 0);
        } else {
            attrs[rpmKey] = 0;
            attrs[faultKey] = false;
        }
    }

    String jsonPayload;
    serializeJson(outboundDoc, jsonPayload);

    String postRoute = "POST /api/states/sensor." + String(config.nodeID) + " HTTP/1.1";
    client.println(postRoute);
    client.print("Host: "); client.println(config.haHost);
    client.print("Authorization: "); client.println(config.haToken);
    client.println("Content-Type: application/json");
    client.print("Content-Length: "); client.println(jsonPayload.length());
    client.println("Connection: keep-alive");
    client.println();
    client.println(jsonPayload);

    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
    }

    String getRoute = "GET /api/states/sensor." + String(config.haSensor) + " HTTP/1.1";
    client.println(getRoute);
    client.print("Host: "); client.println(config.haHost);
    client.print("Authorization: "); client.println(config.haToken);
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();

    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
    }

    String payload = "";
    while (client.available()) {
        payload += (char)client.read();
    }
    client.stop();

    JsonDocument inboundDoc;
    DeserializationError error = deserializeJson(inboundDoc, payload);
    bool pullOk = false;

    if (!error) {
        String haStateStr = inboundDoc["state"].as<String>();
        if (haStateStr != "unknown" && haStateStr != "unavailable" && haStateStr.length() > 0) {
            float ha_val = haStateStr.toFloat();
            String haUnit = inboundDoc["attributes"]["unit_of_measurement"].as<String>();
            networkTempC = (haUnit.indexOf("F") != -1) ? (ha_val - 32.0) * 5.0 / 9.0 : ha_val;

            if (!networkSensorHealthy && networkAlertSent) sendHomeAssistantVoiceClear("network");
            networkSensorHealthy = true;
            networkAlertSent = false;
            pullOk = true;
        } else {
            networkSensorHealthy = false;
        }
    } else {
        networkSensorHealthy = false;
    }

    if (pullOk) {
        if (!lastFullSyncOk) {
            Serial.print("HA sync fully OK - network temp = "); Serial.print(networkTempC, 1); Serial.println("C");
        } else if (millis() - lastHeartbeatMs >= HEARTBEAT_PERIOD_MS) {
            lastHeartbeatMs = millis();
            Serial.print("HA sync heartbeat: OK, network temp = "); Serial.print(networkTempC, 1); Serial.println("C");
        }
        lastFullSyncOk = true;
    } else {
        if (lastFullSyncOk) {
            Serial.println("HA connection is fine, but the network-temp pull just started failing (bad entity/parse?).");
        }
        lastFullSyncOk = false;
    }
}

// --- Two-way tMin/tMax sync with HA input_number helpers ---

static bool fetchEntityFloatState(const char* entityId, float &outValue) {
    EthernetClient client;
    if (!client.connect(config.haHost, config.haPort)) return false;

    String route = "GET /api/states/" + String(entityId) + " HTTP/1.1";
    client.println(route);
    client.print("Host: "); client.println(config.haHost);
    client.print("Authorization: "); client.println(config.haToken);
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();

    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
    }
    String payload = "";
    while (client.available()) payload += (char)client.read();
    client.stop();

    JsonDocument doc;
    if (deserializeJson(doc, payload)) return false;

    String stateStr = doc["state"].as<String>();
    if (stateStr == "unknown" || stateStr == "unavailable" || stateStr.length() == 0) return false;

    outValue = stateStr.toFloat();
    return true;
}

static bool pushEntityFloatState(const char* entityId, float value) {
    EthernetClient client;
    if (!client.connect(config.haHost, config.haPort)) return false;

    JsonDocument doc;
    doc["entity_id"] = entityId;
    doc["value"] = value;
    String jsonPayload;
    serializeJson(doc, jsonPayload);

    client.println("POST /api/services/input_number/set_value HTTP/1.1");
    client.print("Host: "); client.println(config.haHost);
    client.print("Authorization: "); client.println(config.haToken);
    client.println("Content-Type: application/json");
    client.print("Content-Length: "); client.println(jsonPayload.length());
    client.println("Connection: close\r\n");
    client.println(jsonPayload);

    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
    }
    client.stop();
    return true;
}

void fetchThresholdsFromHA() {
    if (strlen(config.haTMinEntity) == 0 || strlen(config.haTMaxEntity) == 0) return;

    float haTMin, haTMax;
    bool gotMin = fetchEntityFloatState(config.haTMinEntity, haTMin);
    bool gotMax = fetchEntityFloatState(config.haTMaxEntity, haTMax);

    const float EPSILON = 0.05; // ignore float noise, only react to real changes
    bool changed = false;

    if (gotMin && fabs(haTMin - config.tMin) > EPSILON) {
        Serial.print("tMin changed via HA: "); Serial.print(config.tMin);
        Serial.print(" -> "); Serial.println(haTMin);
        sdLogEvent("CONFIG", "source=HA field=tMin old=" + String(config.tMin, 1) + "C new=" + String(haTMin, 1) + "C");
        config.tMin = haTMin;
        changed = true;
    }
    if (gotMax && fabs(haTMax - config.tMax) > EPSILON) {
        Serial.print("tMax changed via HA: "); Serial.print(config.tMax);
        Serial.print(" -> "); Serial.println(haTMax);
        sdLogEvent("CONFIG", "source=HA field=tMax old=" + String(config.tMax, 1) + "C new=" + String(haTMax, 1) + "C");
        config.tMax = haTMax;
        changed = true;
    }

    if (changed) saveSettings();
}

void pushThresholdsToHA() {
    if (strlen(config.haTMinEntity) > 0) pushEntityFloatState(config.haTMinEntity, config.tMin);
    if (strlen(config.haTMaxEntity) > 0) pushEntityFloatState(config.haTMaxEntity, config.tMax);
}

// --- Two-way manual-override sync with HA ---

static bool fetchEntityBoolState(const char* entityId, bool &outValue) {
    EthernetClient client;
    if (!client.connect(config.haHost, config.haPort)) return false;

    String route = "GET /api/states/" + String(entityId) + " HTTP/1.1";
    client.println(route);
    client.print("Host: "); client.println(config.haHost);
    client.print("Authorization: "); client.println(config.haToken);
    client.println("Content-Type: application/json");
    client.println("Connection: close");
    client.println();

    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
    }
    String payload = "";
    while (client.available()) payload += (char)client.read();
    client.stop();

    JsonDocument doc;
    if (deserializeJson(doc, payload)) return false;

    String stateStr = doc["state"].as<String>();
    if (stateStr != "on" && stateStr != "off") return false; // unknown/unavailable/empty

    outValue = (stateStr == "on");
    return true;
}

static bool pushEntityBoolState(const char* entityId, bool value) {
    EthernetClient client;
    if (!client.connect(config.haHost, config.haPort)) return false;

    JsonDocument doc;
    doc["entity_id"] = entityId;
    String jsonPayload;
    serializeJson(doc, jsonPayload);

    String route = String("POST /api/services/input_boolean/") + (value ? "turn_on" : "turn_off") + " HTTP/1.1";
    client.println(route);
    client.print("Host: "); client.println(config.haHost);
    client.print("Authorization: "); client.println(config.haToken);
    client.println("Content-Type: application/json");
    client.print("Content-Length: "); client.println(jsonPayload.length());
    client.println("Connection: close\r\n");
    client.println(jsonPayload);

    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
    }
    client.stop();
    return true;
}

void fetchOverrideFromHA() {
    if (strlen(config.haOverrideSwitchEntity) == 0) return;

    bool haOn;
    bool gotOn = fetchEntityBoolState(config.haOverrideSwitchEntity, haOn);
    if (!gotOn) return;

    float haSpeed;
    bool gotSpeed = (strlen(config.haOverrideSpeedEntity) > 0) &&
                    fetchEntityFloatState(config.haOverrideSpeedEntity, haSpeed);

    if (haOn != manualOverrideActive) {
        manualOverrideActive = haOn;
        if (haOn) {
            manualOverrideDutyCycle = gotSpeed ? constrain((int)haSpeed, 0, 255) : 255; // default full speed
        }
        int pct = (manualOverrideDutyCycle * 100) / 255;
        Serial.print("Override "); Serial.print(haOn ? "ACTIVATED" : "DEACTIVATED");
        Serial.print(" via HA - speed="); Serial.print(pct); Serial.println("%");
        sdLogEvent("OVERRIDE", String("source=HA action=") + (haOn ? "ON" : "OFF") + " speed=" + String(pct) + "%");
    } else if (haOn && gotSpeed) {
        // Already active - pick up a speed change made via HA's own slider
        int newDuty = constrain((int)haSpeed, 0, 255);
        if (abs(newDuty - manualOverrideDutyCycle) > 2) { // ignore float noise
            manualOverrideDutyCycle = newDuty;
            int pct = (manualOverrideDutyCycle * 100) / 255;
            Serial.print("Override SPEED changed via HA: "); Serial.print(pct); Serial.println("%");
            sdLogEvent("OVERRIDE", "source=HA action=SPEED speed=" + String(pct) + "%");
        }
    }
}

void pushOverrideSwitchToHA() {
    if (strlen(config.haOverrideSwitchEntity) > 0) pushEntityBoolState(config.haOverrideSwitchEntity, manualOverrideActive);
}

void pushOverrideSpeedToHA() {
    if (strlen(config.haOverrideSpeedEntity) > 0) pushEntityFloatState(config.haOverrideSpeedEntity, manualOverrideDutyCycle);
}

void pushDailyRollupToHA(const String &date,
                          float localMinC, float localMaxC,
                          float netMinC, float netMaxC,
                          float blendMinC, float blendMaxC,
                          long fan1MinRpm, long fan1MaxRpm,
                          long fan2MinRpm, long fan2MaxRpm) {
    EthernetClient client;
    if (!client.connect(config.haHost, config.haPort)) {
        Serial.println("Daily rollup push to HA failed - couldn't connect.");
        return;
    }

    JsonDocument doc;
    doc["state"] = date;
    JsonObject attrs = doc["attributes"].to<JsonObject>();
    attrs["local_min_c"] = String(localMinC, 1);
    attrs["local_max_c"] = String(localMaxC, 1);
    attrs["net_min_c"] = String(netMinC, 1);
    attrs["net_max_c"] = String(netMaxC, 1);
    attrs["blend_min_c"] = String(blendMinC, 1);
    attrs["blend_max_c"] = String(blendMaxC, 1);
    attrs["fan1_min_rpm"] = fan1MinRpm;
    attrs["fan1_max_rpm"] = fan1MaxRpm;
    attrs["fan2_min_rpm"] = fan2MinRpm;
    attrs["fan2_max_rpm"] = fan2MaxRpm;

    String jsonPayload;
    serializeJson(doc, jsonPayload);

    String route = "POST /api/states/sensor." + String(config.nodeID) + "_daily_summary HTTP/1.1";
    client.println(route);
    client.print("Host: "); client.println(config.haHost);
    client.print("Authorization: "); client.println(config.haToken);
    client.println("Content-Type: application/json");
    client.print("Content-Length: "); client.println(jsonPayload.length());
    client.println("Connection: close\r\n");
    client.println(jsonPayload);

    while (client.connected()) {
        String line = client.readStringUntil('\n');
        if (line == "\r") break;
    }
    client.stop();
    Serial.println("Daily rollup summary pushed to HA.");
}
