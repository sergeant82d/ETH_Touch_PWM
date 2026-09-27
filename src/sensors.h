#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>

// Data struct supports up to 4 fan channels for config compatibility, but
// this board revision only breaks out 2 physical PWM+tach pin pairs
// (GPIO budget: W5500 SPI + LCD/touch/SD reserved pins leave 11 free GPIOs,
// consumed by 6 for W5500, 1 for OneWire, and 4 for fan1/fan2 PWM+tach).
// Channels 2 and 3 are placeholders (-1 pin) for a future pin expander.
extern const int NUM_FANS;
extern const int pwmPins[];
extern const int tachPins[];

extern volatile unsigned long tachCounts[];
extern unsigned long currentRPMs[];
extern int currentDutyCycles[];

extern float localTempC;
extern float networkTempC;
extern float blendedAverageC;
extern bool localSensorHealthy;
extern bool networkSensorHealthy;
extern bool localAlertSent;
extern bool networkAlertSent;
extern bool totalAlertSent;

// Manual fan-speed override (bookmarked feature: touch UI framework).
// Per the boot-behavior rule, this always starts false - override never
// persists across reboot, and the sensor-loss failsafe in
// evaluateSensorFailsafes() takes priority over it regardless of state.
extern bool manualOverrideActive;
extern int manualOverrideDutyCycle; // 0-255, only meaningful when active

void sensorsInit();
void sampleLocalTemperature();               // reads DS18B20, updates localTempC/localSensorHealthy
void calculateFanCurve(float targetTempC);   // drives PWM outputs from a target temperature
void calculateRPMs(unsigned long timeElapsedMs);
void evaluateSensorFailsafes();              // recomputes blendedAverageC from local+network health

#endif // SENSORS_H
