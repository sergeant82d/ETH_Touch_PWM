#include "network.h"
#include "pins.h"
#include "config.h"
#include <SPI.h>
#include <TimeLib.h>
#include <time.h>

static byte mac[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED };

EthernetServer server(80);
EthernetUDP Udp;

static const unsigned int localPortUDP = 8888;
// A hard-coded Cloudflare IP was used while diagnosing the NTP first-packet
// drop; DNS was ruled out, so this is back to the pool (resolved via config.dns).
static const char* ntpServerName = "pool.ntp.org";
static const int NTP_PACKET_SIZE = 48;
static byte packetBuffer[NTP_PACKET_SIZE];

void applyTimeZone() {
    char rule[64];
    if (config.tzPosix[0]) {
        strlcpy(rule, config.tzPosix, sizeof(rule));
    } else {
        // Old fixed offset: POSIX counts the other way (UTC-5 is "UTC5")
        snprintf(rule, sizeof(rule), "UTC%d", -config.tzOffset);
    }
    setenv("TZ", rule, 1);
    tzset();
    Serial.print("Time zone: "); Serial.print(config.tzName); Serial.print(" ("); Serial.print(rule); Serial.println(")");
}

// UTC -> local wall-clock time as a TimeLib time (the rest of the firmware
// uses local time: LCD, SD log, day change, web page)
static time_t toLocal(time_t utc) {
    struct tm lt;
    localtime_r(&utc, &lt);
    tmElements_t te;
    te.Second = lt.tm_sec;
    te.Minute = lt.tm_min;
    te.Hour = lt.tm_hour;
    te.Wday = lt.tm_wday + 1;
    te.Day = lt.tm_mday;
    te.Month = lt.tm_mon + 1;
    te.Year = lt.tm_year + 1900 - 1970;
    return makeTime(te);
}

static void sendNTPpacket(const char* address) {
    memset(packetBuffer, 0, NTP_PACKET_SIZE);
    packetBuffer[0]  = 0b11100011; // LI, Version, Mode
    packetBuffer[1]  = 0;          // Stratum
    packetBuffer[2]  = 6;          // Polling interval
    packetBuffer[3]  = 0xEC;       // Peer clock precision
    packetBuffer[12] = 49;
    packetBuffer[13] = 0x4E;
    packetBuffer[14] = 49;
    packetBuffer[15] = 52;

    // beginPacket() returns 0 if it couldn't allocate/ready a socket for
    // this send (e.g. W5500 socket pool exhausted) - worth knowing before
    // blaming the network path for an NTP timeout.
    if (!Udp.beginPacket(address, 123)) {
        Serial.println("NTP: beginPacket() failed - no free UDP socket?");
        return;
    }
    Udp.write(packetBuffer, NTP_PACKET_SIZE);
    Udp.endPacket();
}

time_t getNtpTime() {
    const int MAX_ATTEMPTS = 3;

    for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
        while (Udp.parsePacket() > 0) ; // flush stale packets

        Serial.print("Requesting NTP time (attempt ");
        Serial.print(attempt); Serial.print("/"); Serial.print(MAX_ATTEMPTS); Serial.println(")...");
        sendNTPpacket(ntpServerName);

        uint32_t beginWait = millis();
        while (millis() - beginWait < 1500) {
            int size = Udp.parsePacket();
            if (size >= NTP_PACKET_SIZE) {
                Udp.read(packetBuffer, NTP_PACKET_SIZE);

                unsigned long secsSince1900 =
                    ((unsigned long)packetBuffer[40] << 24) |
                    ((unsigned long)packetBuffer[41] << 16) |
                    ((unsigned long)packetBuffer[42] << 8)  |
                     (unsigned long)packetBuffer[43];

                unsigned long secsSince1970 = secsSince1900 - 2208988800UL;
                Serial.print("NTP sync successful on attempt "); Serial.println(attempt);
                return toLocal((time_t)secsSince1970);
            }
        }

        // A dropped first UDP packet (e.g. the W5500 needing to resolve the
        // gateway's MAC via ARP before it can actually send) is a known
        // failure mode on WizNet-chip Ethernet libraries - a short pause
        // before retrying gives that time to settle rather than failing
        // outright on attempt 1.
        if (attempt < MAX_ATTEMPTS) delay(300);
    }

    Serial.println("NTP sync timed out after all attempts.");
    return 0;
}

void networkInit() {
    // Hardware reset pulse for the external W5500 module
    pinMode(W5500_RST, OUTPUT);
    digitalWrite(W5500_RST, LOW);
    delay(50);
    digitalWrite(W5500_RST, HIGH);
    delay(50);

    SPI.begin(W5500_SCK, W5500_MISO, W5500_MOSI, W5500_CS);
    Ethernet.init(W5500_CS);

    Serial.println("Initializing external W5500 Ethernet module...");
    Ethernet.begin(mac, config.ip, config.dns, config.gateway, config.subnet);

    if (Udp.begin(localPortUDP)) {
        Serial.println("UDP socket opened for NTP.");
    } else {
        Serial.println("WARNING: Udp.begin() failed - no free W5500 socket for NTP.");
    }
    server.begin();

    Serial.print("Dashboard URL: http://"); Serial.println(Ethernet.localIP());
    Serial.print("Hardware status: ");       Serial.println((int)Ethernet.hardwareStatus());
}

bool isEthernetConnected() {
    return Ethernet.linkStatus() == LinkON;
}

bool isNetworkConnected() {
    // Wi-Fi fallback (bookmark #5) isn't built yet - this is the single
    // call site that should become `isEthernetConnected() || isWifiConnected()`
    // once it is, rather than every caller needing to know about both paths.
    return isEthernetConnected();
}
