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
RTC_DATA_ATTR static int64_t s_dwellDeadline = 0;


CheckFillDetectStateMachine::CheckFillDetectStateMachine(EspTimeKeeper* timeKeeper, const DeviceSettings& deviceSettings, LogiSensorData& sensorData):
    _timeKeeper(timeKeeper),
    _deviceSettings(deviceSettings),
    _sensorData(sensorData)
{
    // Restore state from RTC memory
    _inDwell = s_inDwell;
    _lastStableLevel = s_lastStableLevel;
    _dwellDeadline = s_dwellDeadline;
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

    ESP_LOGI(TAG, "Fill Detection - Current: %.1f%%, LastStable: %.1f%%, InDwell: %s, Deadline: %lld",
             _currentFillLevel, _lastStableLevel, _inDwell ? "YES" : "NO", _dwellDeadline);

    if (_inDwell)
    {
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
        _fillDwellTime = _deviceSettings.getFillDwellTime();

        // Apply minimum delta if configured
        if (_fillAlarmDelta < CONFIG_LOGI_MIN_FILL_PERCENT_DELTA)
        {
            _fillAlarmDelta = CONFIG_LOGI_MIN_FILL_PERCENT_DELTA;
        }
        if (_fillDwellTime < CONFIG_LOGI_MIN_FILL_DWELL_TIME_S)
        {
            _fillDwellTime = CONFIG_LOGI_MIN_FILL_DWELL_TIME_S;
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

        clearDwellState(_currentFillLevel);
        _parentStateMachine->transitionTo(ApplicationState::ApplicationState_ScheduleCheck);
        return;
    }

    // Check if level is still RISING during dwell
    if (_currentFillLevel > s_lastPotentialFillLevel)
    {
        ESP_LOGI(TAG, "Level still rising during dwell: %.1f%% (was %.1f%%) - Resetting dwell timer",
                    _currentFillLevel, s_lastPotentialFillLevel);

        s_lastPotentialFillLevel = _currentFillLevel;
        _dwellDeadline = static_cast<int64_t>(_timeKeeper->GetCurrentTime()) + _fillDwellTime;
        persistDwellState();

        sleepUntilNextWake();
        return;
    }

    transitionTo(CheckFillDetectState::CheckFillDetectState_CheckDwellTime);
}
void CheckFillDetectStateMachine::CheckFillDetectStateCompareFuelLevelsWithDelta()
{
    ESP_LOGI(TAG, "Entered State CheckFillDetectStateCompareFuelLevelsWithDelta");
    // _currentFillLevel already set in CheckInDwellMode

    if (_currentFillLevel >= (_lastStableLevel + _fillAlarmDelta))
    {
        ESP_LOGI(TAG, "Fill rise detected: Level rose from %.1f%% to %.1f%% (delta: %u%%)",
                    _lastStableLevel, _currentFillLevel, _fillAlarmDelta);

        // Start dwell period - enter fill detection mode
        _inDwell = true;
        _dwellDeadline = static_cast<int64_t>(_timeKeeper->GetCurrentTime()) + _fillDwellTime;
        s_lastPotentialFillLevel = _currentFillLevel;
        persistDwellState();

        sleepUntilNextWake();
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

    time_t now = _timeKeeper->GetCurrentTime();
    if (_dwellDeadline <= 0)
    {
        _dwellDeadline = static_cast<int64_t>(now) + _fillDwellTime;
        persistDwellState();
    }

    int64_t remaining = _dwellDeadline - static_cast<int64_t>(now);
    ESP_LOGI(TAG, "Fill dwell check: now=%lld, deadline=%lld, remaining=%lld seconds",
             static_cast<int64_t>(now), _dwellDeadline, remaining);

    if (remaining <= 0)
    {
        ESP_LOGI(TAG, "Fill event confirmed after dwell: Final level %.1f%%", _currentFillLevel);
        enqueueFinalFillUdpPost();
        clearDwellState(_sensorData.PublishedFuelLevel);
        _parentStateMachine->transitionTo(ApplicationState::ApplicationState_Posting);
    }
    else
    {
        ESP_LOGI(TAG, "Fill dwell not complete - skipping scheduled UDP checks until dwell completes");
        sleepUntilNextWake();
    }
}

void CheckFillDetectStateMachine::persistDwellState()
{
    s_inDwell = _inDwell;
    s_lastStableLevel = _lastStableLevel;
    s_lastPotentialFillLevel = _currentFillLevel > s_lastPotentialFillLevel ? _currentFillLevel : s_lastPotentialFillLevel;
    s_dwellDeadline = _dwellDeadline;
}

void CheckFillDetectStateMachine::clearDwellState(float stableLevel)
{
    _inDwell = false;
    _dwellDeadline = 0;
    _lastStableLevel = stableLevel;
    s_inDwell = false;
    s_lastStableLevel = stableLevel;
    s_lastPotentialFillLevel = stableLevel;
    s_dwellDeadline = 0;
}

void CheckFillDetectStateMachine::sleepUntilNextWake()
{
    if (_parentStateMachine)
    {
        _parentStateMachine->transitionTo(ApplicationState::ApplicationState_Sleep);
    }
}

void CheckFillDetectStateMachine::enqueueFinalFillUdpPost()
{
    if (_parentStateMachine)
    {
        ILogiHardwareDriver* driver = _parentStateMachine->GetHardwareDriver();
        if (driver)
        {
            ESP_LOGI(TAG, "Resampling sensors/peripherals before final fill UDP post");
            driver->UpdateMeasurements();
            driver->GetLatestSensorData(_sensorData);
            driver->SetGnssPower(false);
            _sensorData.elapsedTimeStampS = static_cast<uint32_t>(_timeKeeper->GetCurrentTime());
        }

        ESP_LOGI(TAG, "Enqueuing final fill telemetry as compact UDP");
        _parentStateMachine->addToPostQueue(_sensorData, PostTransport::PostTransport_Udp);
    }
}
