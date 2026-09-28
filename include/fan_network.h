#ifndef FAN_NETWORK_H
#define FAN_NETWORK_H

#include <Arduino.h>
#include <Network.h>
#include <NetworkClient.h>
#include <NetworkServer.h>
#include <ETH.h>
#include <ArduinoJson.h>

// Named fan_network.h, not network.h: on Windows (case-insensitive) the core's
// #include "Network.h" would find this file instead of its own.

// HTTP server (port 80). NetworkServer is lwIP-based: it answers on every
// interface (Ethernet, WiFi, hotspot).
extern NetworkServer server;

// W5500 through the ESP32 core's ETH driver (lwIP), static address from the
// settings, device name (mDNS), then the HTTP server. Time: SNTP
// (pool.ntp.org), started when a network gets its address.
void networkInit();

// Every loop() pass. WiFi is a backup: joined when the Ethernet link has been
// down for 30 s, left when Ethernet has been back for 60 s. The setup hotspot
// starts after 60 s without any network and stops once one is back.
void networkLoop();

// TimeLib sync provider: local time (time zone rule applied) from the
// system clock that SNTP keeps, or 0 until SNTP has set it.
time_t getNtpTime();

// Sets the clock's time zone rule from config.tzPosix (or the old hour
// offset). getNtpTime() then returns local time with daylight saving,
// re-checked at every sync. Call after loadSettings() and after a change.
void applyTimeZone();

// True if the W5500's physical Ethernet link is up (the link, not just an
// assigned address: with a static IP the address stays "assigned" with the
// cable unplugged).
bool isEthernetConnected();
bool isWifiConnected();       // backup WiFi joined
bool isHotspotActive();
bool isNetworkConnected();    // Ethernet or WiFi (the hotspot doesn't count)

// "Ethernet", "WiFi", "Hotspot" or "None", and its address
const char* activeNetwork();
// Name of that network when it's WiFi (joined SSID) or the hotspot; else ""
String activeNetworkName();
IPAddress localIP();

// Device name: config.hostname, or made from the node ID ("fancontroller-01")
String deviceHostname();

// WiFi settings saved: reconnect if WiFi is in use, re-register the name
void wifiReconfigure();

// Network scan for the web page (asynchronous): start, then poll. Returns
// false while running; true with the networks added to `out`.
void wifiScanStart();
bool wifiScanCollect(JsonArray out);

// Status of every interface, for the web page
void networkStatus(JsonObject out);

#endif // FAN_NETWORK_H
