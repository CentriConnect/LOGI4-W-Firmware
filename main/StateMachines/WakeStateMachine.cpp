#include "WakeStateMachine.h"
#include <iostream>

#include <memory>
#include "esp_log.h"
#include "hal/EspPowerManager.h"
#include "logi/ILogiHardwareDriver.h"

#include "ApplicationStateMachine.h"

static const char *TAG = "WakeStateMachine";

static const uint64_t MINUTE_INTERVAL_US = 60 * 1000 * 1000ULL; 
static const uint64_t SLEEP_OFFSET_US = 100 * 1000ULL;

WakeStateMachine::WakeStateMachine(ILogiHardwareDriver* driver, EspPowerManager* powerManager, EspTimeKeeper* timeKeeper): 
    _driver(driver),
    _powerManager(powerManager),
    _timeKeeper(timeKeeper)
{
}

static uint64_t microseconds_to_next_minute(time_t now)
{
    if (now == 0)
    {
        ESP_LOGW(TAG, "Time not synced, sleeping for default minute interval.");
        return MINUTE_INTERVAL_US; 
    }
    
    uint32_t seconds_past_minute = now % 60;
    uint64_t sleep_s = (60 - seconds_past_minute);
    
    if (sleep_s == 0)
    {
        sleep_s = 60;
    }
    
    uint64_t sleep_us = sleep_s * 1000 * 1000ULL + SLEEP_OFFSET_US;
    ESP_LOGD(TAG, "Calculating sleep: now=%ld, secs_past_min=%lu, sleep_s=%llu, sleep_us=%llu",
             (long)now, seconds_past_minute, sleep_s, sleep_us);
             
    return sleep_us;
}

void WakeStateMachine::update() 
{
    switch (currentState) 
    {
        case WakeState::WakeState_CheckStartupCause:
        {
            ESP_LOGI(TAG, "Current State: Check Startup Cause");
            WakeStateCheckStartupCause();
            break;
        }
    }
}

void WakeStateMachine::transitionTo(WakeState newState) 
{
    currentState = newState;
}

void WakeStateMachine::WakeStateCheckStartupCause()
{
    WakeupReason wakeup_reason = _powerManager->GetWakeupReason();

    switch (wakeup_reason)
    {
        case WAKEUP_REASON_RESET:
        {
            ESP_LOGW(TAG, "Wake Reason: Initial Boot / Reset");
            ESP_LOGI(TAG, "Attempting filter saturation on first boot...");

            if (!_driver->SaturateFilters())
            {
                ESP_LOGW(TAG, "Filter saturation failed or timed out.");
            }
            else
            {
                ESP_LOGI(TAG, "Filters saturated.");
            }

            // Time sync happens after WiFi connects in PostingStateMachine
            // Go to Data Sample state to collect and post telemetry
            _parentStateMachine->transitionTo(ApplicationState::ApplicationState_DataSample);
            break;
        }
        
        case WAKEUP_REASON_TIMER:
        {
            ESP_LOGI(TAG, "Wake Reason: Timer");
            // Time sync happens after WiFi connects in PostingStateMachine
            _parentStateMachine->transitionTo(ApplicationState::ApplicationState_DataSample);
            break;
        }

        case WAKEUP_REASON_GPIO:
        case WAKEUP_REASON_UNKNOWN:
        default:
        {
            ESP_LOGW(TAG, "Wake Reason: Unknown or Unhandled (%d). Sleeping for a full minute.", wakeup_reason);

            time_t current_time = _timeKeeper->GetCurrentTime();
            uint64_t sleep_duration_us = microseconds_to_next_minute(current_time);
            _powerManager->Sleep(sleep_duration_us);
            break;
        }   
    }
}

void WakeStateMachine::setParentStateMachine(ApplicationStateMachine* parentStateMachine)
{
    // Set the pointer to the parent state machine
    _parentStateMachine = parentStateMachine;
}

