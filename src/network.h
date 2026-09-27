#ifndef NETWORK_H
#define NETWORK_H

#include <Arduino.h>
#include <Ethernet.h>
#include <EthernetUdp.h>

extern EthernetServer server;
extern EthernetUDP Udp;

// Resets the W5500, brings up SPI + Ethernet with the static config, starts
// the UDP socket (for NTP) and the HTTP server.
void networkInit();

// Blocking NTP time fetch (used as the TimeLib sync provider).
time_t getNtpTime();

// True if the W5500's physical Ethernet link is up (checks the actual
// hardware link-detect state, not just whether an IP is assigned - with
// a static IP config, the IP would still show as "assigned" even with
// the cable unplugged, so link status is the more honest signal).
bool isEthernetConnected();

// True if ANY network path is up - currently just Ethernet, since Wi-Fi
// fallback (bookmark #5) isn't implemented in this codebase yet. Once it
// is, this should become `isEthernetConnected() || isWifiConnected()` -
// this function is the intended single call site for that, so nothing
// else needs to change when Wi-Fi support lands.
bool isNetworkConnected();

#endif // NETWORK_H
