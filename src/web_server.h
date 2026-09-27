#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <Ethernet.h>

// Defined in the main .ino - see the comment there for why this exists
// instead of just using __FILE__ inside web_server.cpp.
extern const char* SKETCH_FILENAME;

// Reads and responds to one HTTP request on an already-accepted client.
void handleNativeWebTraffic(EthernetClient& client);

#endif // WEB_SERVER_H
