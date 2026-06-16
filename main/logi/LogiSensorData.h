#ifndef LOGI_SENSOR_DATA_H_
#define LOGI_SENSOR_DATA_H_

#include <stdint.h>
#include "GPSModuleYIC51009EBGGB.h"

class LogiSensorData
{
    public:
        uint32_t elapsedTimeStampS;

        // Fuel levels values
        uint8_t PublishedFuelLevel;
        float AnalogFuelVoltage;
        float SensorSupplyVoltage;

        // Battery values
        uint8_t PublishedBatteryLevel;
        float AnalogBatteryVoltage;
        float BatteryTemperatureC;

        // Solar values
        float SolarVoltage;

        float MeasuredTemperatureC;
        float MeasuredHumidityPercentage;

        // GPS Coordinate
        GpsData_t GPSData;
};

/**
 * @brief Extended telemetry context for LOGI4-compatible JSON format
 *
 * Contains all fields needed to generate telemetry matching the LOGI4 format.
 * Populate this from DeviceSettings and sensor data before calling PublishTelemetry.
 */
struct TelemetryContext
{
    // Identification
    char deviceId[37];          // UUID string
    char imei[16];              // "WIFI" for WiFi variant
    char iccid[24];             // "WIFI" for WiFi variant

    // Timestamps & Versioning
    char dateTimeIso[32];       // ISO 8601 timestamp
    char mqttSchema[16];        // e.g., "3.1.1"
    char deviceVersion[64];     // "HW:4.1.0,SW:1.0.4"
    char modemFirmwareVersion[32]; // Empty for WiFi variant

    // Network & Status
    int32_t lteSignalQuality;   // WiFi RSSI for WiFi variant
    int32_t chargerStatus;
    int32_t bleStatus;
    int32_t deviceStatus;
    char errorLog[256];
    int16_t resetCounter;

    // Configuration Echo
    char postingSchedule[256];  // JSON array of schedules
    uint32_t fillDwellTime;
    uint8_t fillPostDeltaValue;
    uint32_t lteAttemptTimeout;
    uint32_t postDwellTime;

    // Validity flags (only fields marked valid are included in JSON)
    bool deviceIdValid;
    bool imeiValid;
    bool iccidValid;
    bool dateTimeIsoValid;
    bool mqttSchemaValid;
    bool deviceVersionValid;
    bool modemFirmwareVersionValid;
    bool lteSignalQualityValid;
    bool chargerStatusValid;
    bool bleStatusValid;
    bool deviceStatusValid;
    bool errorLogValid;
    bool resetCounterValid;
    bool postingScheduleValid;
    bool fillDwellTimeValid;
    bool fillPostDeltaValueValid;
    bool lteAttemptTimeoutValid;
    bool postDwellTimeValid;
};

#endif // LOGI_SENSOR_DATA_H_