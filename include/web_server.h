#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <Arduino.h>
#include <NetworkClient.h>

// Defined in the main .ino - see the comment there for why this exists
// instead of just using __FILE__ inside web_server.cpp.
extern const char* SKETCH_FILENAME;

// Reads and responds to one HTTP request on an already-accepted client.
void handleNativeWebTraffic(NetworkClient& client);

// Call every loop() pass: accepts connections and serves each once its
// request has arrived (browsers open spare connections and send nothing on
// them; waiting on those held loop() up for a second each).
void webServerLoop();

#endif // WEB_SERVER_H
