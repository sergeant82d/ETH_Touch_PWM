#ifndef MQTT_H
#define MQTT_H

#include <Arduino.h>

// The only link to Home Assistant (MQTT discovery, state, commands; the
// REST link is gone since Phase 3) - topics and entities in docs/MQTT.md.
// Runs from loop(): simplest, and loop() also serves the web page and reads
// the sensors whose values it publishes.
void mqttInit();
void mqttLoop();         // call on every loop() pass
void mqttReconfigure();  // settings saved: drop the connection, reconnect with the new ones

// nodeID becomes part of MQTT topics: letters, digits, '_' and '-' only.
bool isValidNodeId(const char* id);

// One line for the web page: connected, off (and why), or the last error.
String mqttStatusText();

// The day's hi/lo summary (sd_logger.cpp, at the day change), as the retained
// "Summary of the day" sensor: state = date, values as attributes. Kept and
// re-sent on the next connect if MQTT is down at that moment. Celsius / RPM;
// a min above its max means no samples that day (sent as null).
void mqttPublishDailySummary(const String &date,
                             float localMinC, float localMaxC,
                             float netMinC, float netMaxC,
                             float blendMinC, float blendMaxC,
                             long fan1MinRpm, long fan1MaxRpm,
                             long fan2MinRpm, long fan2MaxRpm);

#endif // MQTT_H
