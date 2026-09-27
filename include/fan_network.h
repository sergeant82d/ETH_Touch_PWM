#ifndef FAN_NETWORK_H
#define FAN_NETWORK_H

#include <Arduino.h>
#include <Network.h>
#include <NetworkClient.h>
#include <NetworkServer.h>
#include <ETH.h>

// Named fan_network.h, not network.h: on Windows (case-insensitive) the core's
// #include "Network.h" would find this file instead of its own.

// HTTP server (port 80). NetworkServer is lwIP-based: it answers on every
// network interface, so it will also serve WiFi once that is added.
extern NetworkServer server;

// W5500 through the ESP32 core's ETH driver (lwIP), static address from the
// settings, time from SNTP (pool.ntp.org), then the HTTP server.
void networkInit();

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

// True if ANY network path is up - currently just Ethernet. The single place
// to add `|| isWifiConnected()` when the WiFi backup lands.
bool isNetworkConnected();

// Address of the network in use (Ethernet for now)
IPAddress localIP();

#endif // FAN_NETWORK_H
