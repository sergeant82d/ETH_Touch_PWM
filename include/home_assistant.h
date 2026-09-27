#ifndef HOME_ASSISTANT_H
#define HOME_ASSISTANT_H

#include <Arduino.h>

void sendHomeAssistantVoiceAlert(const String &alertMessage);
void sendHomeAssistantVoiceClear(const String &sensorType);

// Posts local sensor + fan telemetry to HA, then pulls the configured
// network temperature sensor back and updates networkTempC/networkSensorHealthy.
void fetchHomeAssistantTemperature();

// Two-way tMin/tMax sync with HA input_number helpers (see config.h's
// haTMinEntity/haTMaxEntity). Poll fetchThresholdsFromHA() periodically
// (e.g. every 15s - deliberately slower than the telemetry cadence, since
// thresholds change rarely). Call pushThresholdsToHA() immediately whenever
// the web form changes tMin/tMax, so HA's stored value doesn't silently
// revert the web change on the next poll.
void fetchThresholdsFromHA();
void pushThresholdsToHA();

// Two-way manual-override sync with HA (input_boolean for on/off, input_number
// for speed 0-255 - see config.h's haOverrideSwitchEntity/haOverrideSpeedEntity).
// Poll fetchOverrideFromHA() periodically as a slow safety-net fallback (HA
// pushes changes to the device immediately via a rest_command automation -
// see the HA YAML) - this poll just catches a missed push.
//
// Push functions are deliberately split rather than one combined push:
// pushing switch-state and speed together let HA's own automations "bounce"
// a stale speed value back on every switch change, since HA processes the
// two entity updates asynchronously and its automations can't be assumed to
// see them in send order. Splitting removes the race entirely rather than
// trying to out-time it:
//   - Engage/disengage: push ONLY the switch state. HA's own automation
//     resets its input_number to 255 itself when the switch turns on
//     (see the updated automation YAML), so the device never needs to
//     (and shouldn't) also push a speed value at that exact moment.
//   - Genuine slider adjustment while already active: push ONLY speed -
//     the switch state hasn't changed, so there's nothing to push there.
void fetchOverrideFromHA();
void pushOverrideSwitchToHA(); // call on engage/disengage only
void pushOverrideSpeedToHA();  // call on a genuine speed-only adjustment only

// Pushes the day's finalized hi/lo summary to HA as a dedicated entity
// (sensor.<nodeID>_daily_summary), separate from the continuous 2s live
// telemetry push - this is the once-a-day rollup, not the raw log. Call
// this from sd_logger.cpp right after a day's extremes are finalized.
// All temperature values are Celsius, matching internal storage.
void pushDailyRollupToHA(const String &date,
                          float localMinC, float localMaxC,
                          float netMinC, float netMaxC,
                          float blendMinC, float blendMaxC,
                          long fan1MinRpm, long fan1MaxRpm,
                          long fan2MinRpm, long fan2MaxRpm);

#endif // HOME_ASSISTANT_H
