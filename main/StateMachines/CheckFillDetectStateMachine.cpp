#include "CheckFillDetectStateMachine.h"
#include <iostream>
#include "ApplicationStateMachine.h"
#include "EspTimeKeeper.h"
#include "esp_log.h"
#include "esp_attr.h"  // For RTC_DATA_ATTR

static const char *TAG = "CheckFillDetectStateMachine";

// RTC_DATA_ATTR variables survive deep sleep (stored in RTC slow memory)
// These track fill detection state across wake cycles
RTC_DATA_ATTR static bool s_inDwell = false;
RTC_DATA_ATTR static float s_lastStableLevel = 0.0f;
RTC_DATA_ATTR static float s_lastPotentialFillLevel = 0.0f;
RTC_DATA_ATTR static uint32_t s_dwellMinutes = 0;


CheckFillDetectStateMachine::CheckFillDetectStateMachine(EspTimeKeeper* timeKeeper, const DeviceSettings& deviceSettings, LogiSensorData& sensorData):
    _timeKeeper(timeKeeper),
    _deviceSettings(deviceSettings),
    _sensorData(sensorData)
{
    // Restore state from RTC memory
    _inDwell = s_inDwell;
    _lastStableLevel = s_lastStableLevel;
    _dwellMinutes = s_dwellMinutes;
}

void CheckFillDetectStateMachine::update() 
{
    switch (currentState) 
    {
        case CheckFillDetectState::CheckFillDetectState_CheckInDwellMode:
        {
            ESP_LOGI(TAG, "Current State: Check In Dwell");
            CheckFillDetectStateCheckInDwellMode();
            break;
        }
        case CheckFillDetectState::CheckFillDetectState_CompareFuelLevels:
        {
            ESP_LOGI(TAG, "Current State: Compare Fuel Levels");
            CheckFillDetectStateCompareFuelLevels();
            break;
        }
        case CheckFillDetectState::CheckFillDetectState_CompareFuelLevelsWithDelta:
        {
            ESP_LOGI(TAG, "Current State: Compare Fuel with Delta");
            CheckFillDetectStateCompareFuelLevelsWithDelta();
            break;
        }
        case CheckFillDetectState::CheckFillDetectState_CheckDwellTime:
        {
            ESP_LOGI(TAG, "Current State: Check Dwell Time");
            CheckFillDetectStateCheckDwellTime();
            break;
        }
    }
}

void CheckFillDetectStateMachine::transitionTo(CheckFillDetectState newState) 
{
    currentState = newState;
}

void CheckFillDetectStateMachine::setParentStateMachine(ApplicationStateMachine* parentStateMachine)
{
    _parentStateMachine = parentStateMachine;
}

void CheckFillDetectStateMachine::CheckFillDetectStateCheckInDwellMode()
{
    ESP_LOGI(TAG, "Entered State CheckFillDetectStateCheckInDwellMode");

    // CRITICAL: Always read current fuel level first before any comparisons
    _currentFillLevel = _sensorData.PublishedFuelLevel;

    ESP_LOGI(TAG, "Fill Detection - Current: %.1f%%, LastStable: %.1f%%, InDwell: %s, DwellMinutes: %lu",
             _currentFillLevel, _lastStableLevel, _inDwell ? "YES" : "NO", (unsigned long)_dwellMinutes);

    if (_inDwell)
    {
        // Get and validate fill dwell time (in seconds, convert to minutes for comparison)
        _fillDwellTime = _deviceSettings.getFillDwellTime();
        if (_fillDwellTime < CONFIG_LOGI_MIN_FILL_DWELL_TIME_S)
        {
            _fillDwellTime = CONFIG_LOGI_MIN_FILL_DWELL_TIME_S;
        }

        transitionTo(CheckFillDetectState::CheckFillDetectState_CompareFuelLevels);
    }
    else
    {
        // Get fill alarm delta setting
        _fillAlarmDelta = _deviceSettings.getFillAlarmDelta();

        // Apply minimum delta if configured
        if (_fillAlarmDelta < CONFIG_LOGI_MIN_FILL_PERCENT_DELTA)
        {
            _fillAlarmDelta = CONFIG_LOGI_MIN_FILL_PERCENT_DELTA;
        }

        transitionTo(CheckFillDetectState::CheckFillDetectState_CompareFuelLevelsWithDelta);
    }
}

void CheckFillDetectStateMachine::CheckFillDetectStateCompareFuelLevels()
{
    ESP_LOGI(TAG, "Entered State CheckFillDetectStateCompareFuelLevels");
    // _currentFillLevel already set in CheckInDwellMode

    // Check for level DROP during dwell - this cancels fill detection (per spec)
    // Use a small threshold (2%) to account for sensor noise
    if (_currentFillLevel < (s_lastPotentialFillLevel - 2.0f))
    {
        ESP_LOGI(TAG, "Level dropped during dwell: %.1f%% (was %.1f%%) - Canceling fill detection",
                    _currentFillLevel, s_lastPotentialFillLevel);

        // Cancel fill detection - false alarm or tank usage during fill
        _inDwell = false;
        _dwellMinutes = 0;
        _lastStableLevel = _currentFillLevel;

        // Save to RTC memory
        s_inDwell = _inDwell;
        s_dwellMinutes = _dwellMinutes;
        s_lastStableLevel = _lastStableLevel;
        s_lastPotentialFillLevel = _currentFillLevel;

        // Exit to schedule check - no post
        _parentStateMachine->transitionTo(ApplicationState::ApplicationState_ScheduleCheck);
        return;
    }

    // Check if level is still RISING during dwell
    if (_currentFillLevel > s_lastPotentialFillLevel)
    {
        ESP_LOGI(TAG, "Level still rising during dwell: %.1f%% (was %.1f%%) - Resetting dwell timer",
                    _currentFillLevel, s_lastPotentialFillLevel);

        s_lastPotentialFillLevel = _currentFillLevel;
        _dwellMinutes = 0;  // Restart dwell counter

        // Save to RTC memory
        s_dwellMinutes = _dwellMinutes;

        // Stay in dwell state, go back to sleep
        _parentStateMachine->transitionTo(ApplicationState::ApplicationState_ScheduleCheck);
        return;
    }

    // Level is stable - increment dwell counter and check if dwell complete
    _dwellMinutes++;
    s_dwellMinutes = _dwellMinutes;

    transitionTo(CheckFillDetectState::CheckFillDetectState_CheckDwellTime);
}
void CheckFillDetectStateMachine::CheckFillDetectStateCompareFuelLevelsWithDelta()
{
    ESP_LOGI(TAG, "Entered State CheckFillDetectStateCompareFuelLevelsWithDelta");
    // _currentFillLevel already set in CheckInDwellMode

    if (_currentFillLevel > (_lastStableLevel + _fillAlarmDelta))
    {
        ESP_LOGI(TAG, "Fill rise detected: Level rose from %.1f%% to %.1f%% (delta: %u%%)",
                    _lastStableLevel, _currentFillLevel, _fillAlarmDelta);

        // Start dwell period - enter fill detection mode
        _inDwell = true;
        _dwellMinutes = 0;

        // Save to RTC memory - track both the stable level and the potential fill level
        s_inDwell = _inDwell;
        s_dwellMinutes = _dwellMinutes;
        s_lastPotentialFillLevel = _currentFillLevel;
        // Note: Don't update s_lastStableLevel yet - we need to keep the baseline
        // until the fill is confirmed or cancelled

        // Go back to sleep - dwell period starts
        _parentStateMachine->transitionTo(ApplicationState::ApplicationState_ScheduleCheck);
    }
    else
    {
        // No fill detected - update last stable level if current is lower or equal
        if (_currentFillLevel <= _lastStableLevel)
        {
            _lastStableLevel = _currentFillLevel;
            s_lastStableLevel = _lastStableLevel;
        }

        // DO NOT PERFORM POST - transition to schedule check
        _parentStateMachine->transitionTo(ApplicationState::ApplicationState_ScheduleCheck);
    }
}

void CheckFillDetectStateMachine::CheckFillDetectStateCheckDwellTime()
{
    ESP_LOGI(TAG, "Entered State CheckFillDetectStateCheckDwellTime");

    // Convert fill dwell time from seconds to minutes for comparison
    // _fillDwellTime is already validated and set in CheckInDwellMode
    uint32_t dwellTimeMinutes = _fillDwellTime / 60;
    if (dwellTimeMinutes == 0)
    {
        dwellTimeMinutes = 1;  // Minimum 1 minute
    }

    ESP_LOGI(TAG, "Dwell check: %lu minutes elapsed, %lu minutes required",
             (unsigned long)_dwellMinutes, (unsigned long)dwellTimeMinutes);

    if (_dwellMinutes >= dwellTimeMinutes)
    {
        ESP_LOGI(TAG, "Fill event CONFIRMED after %lu minute dwell: Final level %.1f%%",
                 (unsigned long)_dwellMinutes, _currentFillLevel);

        // Reset tracking - fill is confirmed
        _inDwell = false;
        _dwellMinutes = 0;
        _lastStableLevel = _currentFillLevel;

        // Save to RTC memory
        s_inDwell = _inDwell;
        s_dwellMinutes = _dwellMinutes;
        s_lastStableLevel = _lastStableLevel;
        s_lastPotentialFillLevel = _currentFillLevel;

        // PERFORM POST - fill event triggers posting
        if (!_deviceSettings.getEventPosts())
        {
            ESP_LOGI(TAG, "Fill event confirmed but event_posts is false; not posting event telemetry");
            _parentStateMachine->transitionTo(ApplicationState::ApplicationState_ScheduleCheck);
            return;
        }

        ESP_LOGI(TAG, "Enqueuing sensor data for fill-triggered post");
        _parentStateMachine->addToPostQueue(_sensorData, PostTransport::PostTransport_Mqtt);
        _parentStateMachine->transitionTo(ApplicationState::ApplicationState_Posting);
    }
    else
    {
        ESP_LOGI(TAG, "Dwell not complete - continuing to wait (%lu/%lu minutes)",
                 (unsigned long)_dwellMinutes, (unsigned long)dwellTimeMinutes);

        // DO NOT PERFORM POST - continue waiting, go to schedule check
        _parentStateMachine->transitionTo(ApplicationState::ApplicationState_ScheduleCheck);
    }
}



