#ifndef HOME_ASSISTANT_H
#define HOME_ASSISTANT_H

#include <Arduino.h>

void sendHomeAssistantVoiceAlert(const String &alertMessage);
void sendHomeAssistantVoiceClear(const String &sensorType);

// Posts local sensor + fan telemetry to HA, then pulls the configured
// network temperature sensor back and updates networkTempC/networkSensorHealthy.
void fetchHomeAssistantTemperature();

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
