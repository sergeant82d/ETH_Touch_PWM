#ifndef MQTT_H
#define MQTT_H

#include <Arduino.h>

// MQTT link to Home Assistant (discovery + state), replacing the REST link in
// home_assistant.cpp step by step - topics and entities in docs/MQTT.md.
// Runs from loop(), not its own task: the Arduino Ethernet library isn't
// thread-safe, and the web server and NTP use the W5500 from loop() too.
void mqttInit();
void mqttLoop();         // call on every loop() pass
void mqttReconfigure();  // settings saved: drop the connection, reconnect with the new ones

// nodeID becomes part of MQTT topics: letters, digits, '_' and '-' only.
bool isValidNodeId(const char* id);

// One line for the web page: connected, off (and why), or the last error.
String mqttStatusText();

#endif // MQTT_H
