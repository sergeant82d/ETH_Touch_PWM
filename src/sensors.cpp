#include "sensors.h"
#include "pins.h"
#include "config.h"
#include <OneWire.h>
#include <DallasTemperature.h>

const int NUM_FANS = 4;
const int pwmPins[NUM_FANS]  = { PWM1_PIN,  PWM2_PIN,  -1, -1 };
const int tachPins[NUM_FANS] = { TACH1_PIN, TACH2_PIN, -1, -1 };

volatile unsigned long tachCounts[NUM_FANS] = {0, 0, 0, 0};
unsigned long currentRPMs[NUM_FANS] = {0, 0, 0, 0};
int currentDutyCycles[NUM_FANS] = {51, 51, 51, 51};

float localTempC = 0.0;
float networkTempC = 0.0;
float blendedAverageC = 0.0;
bool localSensorHealthy = false;
bool networkSensorHealthy = false;
bool localAlertSent = false;
bool networkAlertSent = false;
bool totalAlertSent = false;

bool manualOverrideActive = false;
int manualOverrideDutyCycle = 255; // defaults to full speed per spec, when engaged

static OneWire oneWire(ONEWIRE_PIN);
static DallasTemperature dallasSensors(&oneWire);

static void IRAM_ATTR tachISR0() { tachCounts[0]++; }
static void IRAM_ATTR tachISR1() { tachCounts[1]++; }

void sensorsInit() {
    dallasSensors.begin();

    for (int i = 0; i < NUM_FANS; i++) {
        if (pwmPins[i] < 0) continue; // channel not physically wired
        ledcAttach(pwmPins[i], PWM_FREQ_HZ, PWM_RESOLUTION_BITS);
        pinMode(tachPins[i], INPUT_PULLUP);
    }

    if (config.fanCount >= 1 && tachPins[0] >= 0)
        attachInterrupt(digitalPinToInterrupt(tachPins[0]), tachISR0, FALLING);
    if (config.fanCount >= 2 && tachPins[1] >= 0)
        attachInterrupt(digitalPinToInterrupt(tachPins[1]), tachISR1, FALLING);
    // Channels 2/3: no physical pins on this board revision; left disabled.
}

void sampleLocalTemperature() {
    dallasSensors.requestTemperatures();
    float raw = dallasSensors.getTempCByIndex(0);

    // Guard against disconnected-probe sentinel values
    if (raw > -50.0 && raw != 85.0 && raw != -127.0) {
        localTempC = raw;
        localSensorHealthy = true;
    } else {
        localSensorHealthy = false;
    }
}

void calculateFanCurve(float targetTemp) {
    int targetDuty = 0;

    // Manual override takes priority over the auto curve, per spec - but
    // NOT over the total-sensor-blackout failsafe, which forces full duty
    // directly in evaluateSensorFailsafes() without going through this
    // function at all, so that path is unaffected by override state.
    if (manualOverrideActive) {
        targetDuty = manualOverrideDutyCycle;
    } else if (config.tMax <= config.tMin) {
        targetDuty = 255; // corrupt thresholds -> fail safe to full power
    } else if (targetTemp < config.tMin) {
        targetDuty = 0;
    } else if (targetTemp >= config.tMax) {
        targetDuty = 255;
    } else {
        targetDuty = (int)(51.0 + ((targetTemp - config.tMin) / (config.tMax - config.tMin)) * 204.0);
    }

    for (int i = 0; i < NUM_FANS; i++) {
        if (i < config.fanCount && pwmPins[i] >= 0) {
            currentDutyCycles[i] = targetDuty;
            ledcWrite(pwmPins[i], targetDuty);
        } else {
            currentDutyCycles[i] = 0;
            if (pwmPins[i] >= 0) ledcWrite(pwmPins[i], 0);
        }
    }
}

void calculateRPMs(unsigned long timeElapsedMs) {
    for (int i = 0; i < NUM_FANS; i++) {
        if (i < config.fanCount && tachPins[i] >= 0) {
            noInterrupts();
            unsigned long pulses = tachCounts[i];
            tachCounts[i] = 0;
            interrupts();

            currentRPMs[i] = (pulses > 0 && timeElapsedMs > 0)
                                  ? (pulses * 60000UL) / (2 * timeElapsedMs)
                                  : 0;
        } else {
            tachCounts[i] = 0;
            currentRPMs[i] = 0;
        }
    }
}

void evaluateSensorFailsafes() {
    if (localSensorHealthy && networkSensorHealthy) {
        blendedAverageC = (localTempC + networkTempC) / 2.0;
        totalAlertSent = false;
    } else if (localSensorHealthy) {
        blendedAverageC = localTempC;
    } else if (networkSensorHealthy) {
        blendedAverageC = networkTempC;
    } else {
        blendedAverageC = 0.0;
        for (int i = 0; i < NUM_FANS; i++) {
            currentDutyCycles[i] = 255;
            if (pwmPins[i] >= 0) ledcWrite(pwmPins[i], 255);
        }
    }
}
