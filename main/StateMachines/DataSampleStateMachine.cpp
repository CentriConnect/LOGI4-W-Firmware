#include "DataSampleStateMachine.h"
#include <iostream>
#include "ApplicationStateMachine.h"
#include "EspTimeKeeper.h"
#include "esp_log.h"

static const char *TAG = "DataSampleStateMachine";

DataSampleStateMachine::DataSampleStateMachine(ILogiHardwareDriver* driver, LogiSensorData& sensorData, EspTimeKeeper* timeKeeper):
    _driver(driver),
    _sensorData(sensorData),
    _timeKeeper(timeKeeper)
{
}

void DataSampleStateMachine::update() 
{
    switch (currentState) 
    {
        case DataSampleState::DataSampleState_SampleSensorData:
        {
            ESP_LOGI(TAG, "Current State: Sample Sensor Data");
            DataSampleStateSampleSensorData();
            break;
        }
        case DataSampleState::DataSampleState_UpdateLocalValues:
        {
            ESP_LOGI(TAG, "Current State: Update Locals");
            DataSampleStateUpdateLocalValues();
            break;
        }
    }
}

void DataSampleStateMachine::transitionTo(DataSampleState newState) 
{
    currentState = newState;
}

void DataSampleStateMachine::DataSampleStateSampleSensorData()
{
    ESP_LOGI(TAG, "Current State: DataSampleStateSampleSensorData");
    _driver->UpdateMeasurements();
    transitionTo(DataSampleState::DataSampleState_UpdateLocalValues);
}

void DataSampleStateMachine::DataSampleStateUpdateLocalValues()
{
    ESP_LOGI(TAG, "Current State: DataSampleStateSampleSensorData");
    _driver->GetLatestSensorData(_sensorData);

    // Set the timestamp from TimeKeeper (replaces placeholder value from driver)
    time_t currentTime = _timeKeeper->GetCurrentTime();
    _sensorData.elapsedTimeStampS = static_cast<uint32_t>(currentTime);

    ESP_LOGI(TAG, "  LogiSensorData:\n"
                      "    - Fuel:    Level=%u%%, AnalogV=%.2fV, SupplyV=%.2fV\n"
                      "    - Power:   BattLvl=%u%%, BattV=%.2fV, BattTemp=%.1fC, SolarV=%.2fV\n"
                      "    - Ambient: Temp=%.1fC, Hum=%.1f%%\n"
                      "    - System:  Timestamp=%lu s",
                 _sensorData.PublishedFuelLevel,
                 _sensorData.AnalogFuelVoltage,
                 _sensorData.SensorSupplyVoltage,
                 _sensorData.PublishedBatteryLevel,
                 _sensorData.AnalogBatteryVoltage,
                 _sensorData.BatteryTemperatureC,
                 _sensorData.SolarVoltage,
                 _sensorData.MeasuredTemperatureC,
                 _sensorData.MeasuredHumidityPercentage,
                 _sensorData.elapsedTimeStampS);

    if (_sensorData.GPSData.valid)
    {
        ESP_LOGI(TAG, "- GPS: Fix VALID, Lat=%.6f, Lon=%.6f, Alt=%.1fm, RSSI=%d",
                    _sensorData.GPSData.latitude,
                    _sensorData.GPSData.longitude,
                    _sensorData.GPSData.altitude,
                    _sensorData.GPSData.rssi);
    }
    else
    {
        ESP_LOGI(TAG, "- GPS: Fix INVALID");
    }
    _parentStateMachine->transitionTo(ApplicationState::ApplicationState_CheckFillDetect);
}

void DataSampleStateMachine::setParentStateMachine(ApplicationStateMachine* parentStateMachine)
{
    _parentStateMachine = parentStateMachine;
}