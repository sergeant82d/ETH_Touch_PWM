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

bool manualOverrideActive = false;
int manualOverrideDutyCycle = 255; // defaults to full speed per spec, when engaged

static OneWire oneWire(ONEWIRE_PIN);
static DallasTemperature dallasSensors(&oneWire);

static void IRAM_ATTR tachISR0() { tachCounts[0]++; }
static void IRAM_ATTR tachISR1() { tachCounts[1]++; }

void sensorsInit() {
    dallasSensors.begin();
    dallasSensors.setWaitForConversion(false); // see sampleLocalTemperature()

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

// Called every second. Reads the conversion started on the previous call
// (12-bit takes up to 750 ms), then starts the next one, so loop() never
// waits for the probe (it did, ~0.6 s each time; 2026-10-04).
void sampleLocalTemperature() {
    static bool converting = false;
    if (!converting) {           // first call: nothing to read yet
        dallasSensors.requestTemperatures();
        converting = true;
        return;
    }
    float raw = dallasSensors.getTempCByIndex(0);
    dallasSensors.requestTemperatures();

    // Guard against disconnected-probe sentinel values
    if (raw > -50.0 && raw != 85.0 && raw != -127.0) {
        localTempC = raw;
        localSensorHealthy = true;
    } else {
        localSensorHealthy = false;
    }
}

// Steadier fans (user, 2026-10-04), for the automatic curve only - the
// both-probes-down failsafe and the manual override still act at once:
// - the curve follows the blended temperature averaged over ~30 s
//   (exponential, called once a second), not each reading;
// - hysteresis at the start: on at tMin, off only below tMin - 1.1 C (2 F);
// - the duty changes at most 2 %/s up (0 -> 100 % in < 1 min) and 0.5 %/s
//   down, so the fans ease off. Off -> 20 % is still a single step.
static const float SMOOTH_SAMPLES = 30.0;           // ~30 s time constant
static const float HYSTERESIS_C = 1.1;
static const float DUTY_UP_PER_CALL = 255 * 0.02;   // calls are 1 s apart
static const float DUTY_DOWN_PER_CALL = 255 * 0.005;

void calculateFanCurve(float targetTemp) {
    static bool smoothReady = false;
    static float smoothC = 0;
    static bool curveOn = false;
    static float autoDuty = 0;      // the duty actually applied, with fractions
    int targetDuty = 0;

    // Manual override takes priority over the auto curve, per spec - but
    // NOT over the total-sensor-blackout failsafe. Checked here because
    // loop() calls this right after evaluateSensorFailsafes(): with both
    // probes down blendedAverageC is 0, which is below tMin, so the curve
    // alone would switch the fans off every second instead of full duty.
    if (!localSensorHealthy && !networkSensorHealthy) {
        targetDuty = 255;
        autoDuty = 0;   // afterwards the curve starts again from 20 % (also the first second after boot)
    } else if (manualOverrideActive) {
        targetDuty = manualOverrideDutyCycle;
        autoDuty = targetDuty; // afterwards the curve eases on from the speed chosen
    } else if (config.tMax <= config.tMin) {
        targetDuty = 255; // corrupt thresholds -> fail safe to full power
    } else {
        if (!smoothReady) { smoothC = targetTemp; smoothReady = true; }
        smoothC += (targetTemp - smoothC) / SMOOTH_SAMPLES;
        if (smoothC >= config.tMin) curveOn = true;
        else if (smoothC < config.tMin - HYSTERESIS_C) curveOn = false;

        float want;
        if (!curveOn) want = 0;
        else if (smoothC >= config.tMax) want = 255;
        else want = 51.0 + max(0.0f, (smoothC - config.tMin) / (config.tMax - config.tMin)) * 204.0;

        if (want == 0) {
            autoDuty = 0;                                  // off: at once
        } else {
            if (autoDuty < 51) autoDuty = 51;              // off -> on: 20 %, then ramp
            if (want > autoDuty) autoDuty = min(want, autoDuty + DUTY_UP_PER_CALL);
            else autoDuty = max(want, autoDuty - DUTY_DOWN_PER_CALL);
        }
        targetDuty = (int)lroundf(autoDuty);
    }

    // Per-fan offset (user, 2026-10-04): fan N is held at fan 1's measured RPM
    // + config.fanOffsetRpm[N] by a slow integral trim on its duty, once a
    // second (this function's rate), on RPMs averaged over ~10 s (below). Not in the failsafe (all at full speed),
    // nor while fan 1 is off; no trim without a tach reading (fan stopped or
    // no tach wire), so a missing tach can't drive a fan to full speed.
    static float trim[4] = {0, 0, 0, 0};          // duty units, per fan
    const float TRIM_PER_RPM = 0.02;              // duty per RPM of error per second
    bool failsafe = !localSensorHealthy && !networkSensorHealthy;

    // Each fan's RPM averaged over ~10 s for the comparison: a tach read
    // once a second jumps by 60-120 RPM at a steady duty (fan 1 read
    // 595-720 RPM at 24-25 %, 2026-10-04), so the offset wandered with it.
    static float avgRpm[4] = {0, 0, 0, 0};
    const float RPM_AVG_SAMPLES = 10.0;
    for (int i = 0; i < 4 && i < NUM_FANS; i++) {
        float r = (float)currentRPMs[i];
        if (r <= 0 || avgRpm[i] <= 0) avgRpm[i] = r;   // stopped / just started: follow at once
        else avgRpm[i] += (r - avgRpm[i]) / RPM_AVG_SAMPLES;
    }

    for (int i = 0; i < NUM_FANS; i++) {
        if (i < config.fanCount && pwmPins[i] >= 0) {
            int duty = targetDuty;
            if (i > 0 && i < 4 && config.fanOffsetRpm[i] != 0 && !failsafe && targetDuty > 0) {
                if (avgRpm[0] > 0 && avgRpm[i] > 0) {
                    float error = avgRpm[0] + config.fanOffsetRpm[i] - avgRpm[i];
                    trim[i] += TRIM_PER_RPM * error;
                }
                trim[i] = constrain(trim[i], (float)(51 - targetDuty), (float)(255 - targetDuty)); // stay 20-100 %
                duty = constrain(targetDuty + (int)lroundf(trim[i]), 0, 255);
            } else if (i < 4 && config.fanOffsetRpm[i] == 0) {
                trim[i] = 0;
            }
            currentDutyCycles[i] = duty;
            ledcWrite(pwmPins[i], duty);
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
