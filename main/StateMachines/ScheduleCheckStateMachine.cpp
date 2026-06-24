#include "ScheduleCheckStateMachine.h"
#include <iostream>
#include "ApplicationStateMachine.h"
#include <memory>
#include "EspTimeKeeper.h"
#include "esp_log.h"
#include "esp_attr.h"
#include "wifiCredentials.h"
#include "logi/Faults.h"
#include <cstdio>
#include <cstdlib>

static const char *TAG = "ScheduleCheckStateMachine";

// RTC_DATA_ATTR variables survive deep sleep (stored in RTC slow memory)
RTC_DATA_ATTR static int64_t last_post_attempt = 0;
RTC_DATA_ATTR static bool has_valid_time = false;

uint32_t SCHEDULE_LOCKOUT_TIME_S = 5U;

// Battery voltage threshold for posting (in volts)
// CONFIG_LOGI_BATTERY_VOLTAGE_POST_THRESHOLD_10X is 35 meaning 3.5V
static constexpr float BATTERY_POST_THRESHOLD_V = static_cast<float>(CONFIG_LOGI_BATTERY_VOLTAGE_POST_THRESHOLD_10X) / 10.0f;

// Counter for minutes when we don't have valid time (also persisted across sleep)
RTC_DATA_ATTR static uint32_t minutes_since_last_post = 0;

static bool parseShadowScheduleString(const char* schedule, int& hour, int& minute, uint8_t& daysOfWeek)
{
    if (schedule == nullptr || schedule[0] == '\0')
    {
        return false;
    }

    unsigned int dayByte = 0;
    if (sscanf(schedule, "%d:%d;%x", &hour, &minute, &dayByte) != 3)
    {
        return false;
    }

    if (!(dayByte & 0x80) || hour < 0 || hour > 23 || minute < 0 || minute > 59)
    {
        return false;
    }

    daysOfWeek = static_cast<uint8_t>(dayByte & 0x7F);
    return true;
}

ScheduleCheckStateMachine::ScheduleCheckStateMachine(EspTimeKeeper* timeKeeper, const DeviceSettings& deviceSettings, EspNetworkManager* networkManager, LogiSensorData& sensorData): 
    _timeKeeper(timeKeeper),    
    _deviceSettings(deviceSettings),
    _networkManager(networkManager),
    _sensorData(sensorData)
{
}

void ScheduleCheckStateMachine::update() 
{
    switch (currentState) 
    {
        case ScheduleCheckState::ScheduleCheckState_ValidateDateTime:
        {
            ESP_LOGI(TAG, "Current State: Validate Date Time");
            ScheduleCheckStateValidateDateTime();
            break;
        }
        case ScheduleCheckState::ScheduleCheckState_CheckProvisioning:
        {
            ESP_LOGI(TAG, "Current State: Check Provisioning");
            ScheduleCheckStateCheckProvisioning();
            break;
        }
        case ScheduleCheckState::ScheduleCheckState_CompareTimeToScheduledTime:
        {
            // Stay awake unless sleep is triggered externally
            ScheduleCheckStateCompareTimeToScheduledTime();
            break;
        }     
    }
}

void ScheduleCheckStateMachine::ScheduleCheckStateValidateDateTime()
{
    WeeklySchedule schedules[DeviceSettings::MAX_WEEKLY_SCHEDULES];
    _deviceSettings.getWeeklySchedules(schedules);
    time_t now = _timeKeeper->GetCurrentTime();

    if(IsValidDateTime(now))
    {
        transitionTo(ScheduleCheckState::ScheduleCheckState_CompareTimeToScheduledTime);
    }
    else 
    {
        transitionTo(ScheduleCheckState::ScheduleCheckState_CheckProvisioning);
    }

}

void ScheduleCheckStateMachine::ScheduleCheckStateCheckProvisioning()
{
    if (IsProvisioned(_deviceSettings))
    {
        // TODO: Uncomment and test battery check when hardware with battery is available
        // Check battery voltage before posting
        // float batteryVoltage = _sensorData.AnalogBatteryVoltage;
        // if (batteryVoltage < BATTERY_POST_THRESHOLD_V)
        // {
        //     ESP_LOGW(TAG, "Battery voltage %.2fV is below threshold %.2fV - skipping post to save battery",
        //              batteryVoltage, BATTERY_POST_THRESHOLD_V);
        //     _parentStateMachine->transitionTo(ApplicationState::ApplicationState_Sleep);
        //     return;
        // }

        // LOWBAT is flagged once per measurement in LogiHardwareDriver::GetLatestSensorData
        // so it rides every post (incl. activation); no per-schedule check needed here.

        time_t current_time = _timeKeeper->GetCurrentTime();
        update_last_post_time(static_cast<int64_t>(current_time));

        // Queue sensor data for posting (time not valid, so post current reading)
        ESP_LOGI(TAG, "Enqueued LOGI Data (no valid time - posting current reading)");
        _parentStateMachine->addToPostQueue(_sensorData);
        _parentStateMachine->transitionTo(ApplicationState::ApplicationState_Posting);
    }
    else
    {
        // DO NOT PREFORM POST
        //
        //
        // Enter provisioning state machine
    }
}

void ScheduleCheckStateMachine::ScheduleCheckStateCompareTimeToScheduledTime()
{
    bool noPostInLastFiveMinutes = false;
    bool withinScheduledWindow = false;

    // Get current time using EspTimeKeeper
    time_t current_time = _timeKeeper->GetCurrentTime();
    
    // Check if time is valid first
    if (current_time > 0 && _timeKeeper->IsTimeSynced())
    {
        // Update _timeInfo with current time for schedule comparison
        if (localtime_r(&current_time, &_timeInfo) == nullptr)
        {
            ESP_LOGE(TAG, "Failed to convert time to local time structure");
            return;
        }
        
        // Check if more than 5 minutes have passed since last post
        const time_t FIVE_MINUTES_SECONDS = 5 * 60;
        
        if (!has_valid_time || (current_time - last_post_attempt) > FIVE_MINUTES_SECONDS)
        {
            noPostInLastFiveMinutes = true;
            if (noPostInLastFiveMinutes)
            {
                ESP_LOGD(TAG, "Last post was more than 5 minutes ago. Current: %ld, Last: %ld, Diff: %ld seconds", 
                         (long)current_time, (long)last_post_attempt, (long)(current_time - last_post_attempt));
            }
            else
            {
                ESP_LOGD(TAG, "No valid last post time recorded, allowing post");
            }
        }
        else
        {
            ESP_LOGD(TAG, "Last post was within 5 minutes. Current: %ld, Last: %ld, Diff: %ld seconds", 
                     (long)current_time, (long)last_post_attempt, (long)(current_time - last_post_attempt));
        }
    }
    else
    {
        ESP_LOGW(TAG, "Time not synchronized or invalid. Cannot check last post time.");
        // Decide if you want to allow posting when time is not synced
        // For safety, you might want to return here
        return;
    }

    // Extract schedules
    WeeklySchedule schedules[DeviceSettings::MAX_WEEKLY_SCHEDULES];
    bool success = _deviceSettings.getWeeklySchedules(schedules);

    if (!success)
    {
        ESP_LOGE(TAG, "Failed to get weekly schedules");
        return;
    }

    // Use _timeInfo for the schedule comparison (now populated from current_time)
    int currentMinutes = _timeInfo.tm_hour * 60 + _timeInfo.tm_min;
    int currentWday = _timeInfo.tm_wday;

    // Check if we're on any UDP or MQTT scheduled minute. Earlier builds used a
    // +/-5 minute window, but that can fire at xx:55 and then suppress the exact
    // xx:00 slot via the 5-minute duplicate-post lockout.
    PostTransport selectedTransport = PostTransport::PostTransport_Udp;
    char mqttSchedule[DeviceSettings::MQTT_SCHEDULED_POST_BUFFER_SIZE] = {0};
    int mqttHour = 0;
    int mqttMinute = 0;
    uint8_t mqttDays = 0;
    bool hasMqttSchedule = _deviceSettings.getMqttScheduledPost(mqttSchedule, sizeof(mqttSchedule)) &&
                           parseShadowScheduleString(mqttSchedule, mqttHour, mqttMinute, mqttDays);

    if (hasMqttSchedule)
    {
        int mqttScheduledMinutes = mqttHour * 60 + mqttMinute;
        if ((mqttDays & (1 << currentWday)) && currentMinutes == mqttScheduledMinutes)
        {
            ESP_LOGI(TAG, "On MQTT scheduled post minute: mask 0x%02X, scheduled %02d:%02d, now %02d:%02d.",
                     mqttDays,
                     mqttHour,
                     mqttMinute,
                     _timeInfo.tm_hour,
                     _timeInfo.tm_min);

            withinScheduledWindow = true;
            selectedTransport = PostTransport::PostTransport_Mqtt;
        }
        else if (!(mqttDays & (1 << currentWday)) && currentMinutes == mqttScheduledMinutes)
        {
            ESP_LOGI(TAG, "On derived UDP scheduled post minute from MQTT schedule: MQTT mask 0x%02X excludes today, scheduled %02d:%02d, now %02d:%02d.",
                     mqttDays,
                     mqttHour,
                     mqttMinute,
                     _timeInfo.tm_hour,
                     _timeInfo.tm_min);

            withinScheduledWindow = true;
            selectedTransport = PostTransport::PostTransport_Udp;
        }
    }

    for (int i = 0; i < DeviceSettings::MAX_WEEKLY_SCHEDULES && !withinScheduledWindow; ++i)
    {
        if (schedules[i].DaysOfWeek == 0) continue;

        if (schedules[i].DaysOfWeek & (1 << currentWday))
        {
            int scheduledMinutes = schedules[i].StartTime.Hour * 60 + schedules[i].StartTime.Minute;

            if (currentMinutes == scheduledMinutes)
            {
                ESP_LOGI(TAG, "On UDP scheduled post minute: schedule mask 0x%02X, scheduled %02d:%02d, now %02d:%02d.",
                         schedules[i].DaysOfWeek,
                         schedules[i].StartTime.Hour,
                         schedules[i].StartTime.Minute,
                         _timeInfo.tm_hour,
                         _timeInfo.tm_min);

                withinScheduledWindow = true;
                selectedTransport = PostTransport::PostTransport_Udp;
                break; 
            }
        }
    }

    // Both conditions must be true to allow posting
    if (noPostInLastFiveMinutes && withinScheduledWindow)
    {
        // TODO: Uncomment and test battery check when hardware with battery is available
        // Check battery voltage before posting
        // float batteryVoltage = _sensorData.AnalogBatteryVoltage;
        // if (batteryVoltage < BATTERY_POST_THRESHOLD_V)
        // {
        //     ESP_LOGW(TAG, "Battery voltage %.2fV is below threshold %.2fV - skipping post to save battery",
        //              batteryVoltage, BATTERY_POST_THRESHOLD_V);
        //     // Still queue the data for later, just don't post now
        //     _parentStateMachine->addToPostQueue(_sensorData);
        //     _parentStateMachine->transitionTo(ApplicationState::ApplicationState_Sleep);
        //     return;
        // }

        // LOWBAT is flagged once per measurement in LogiHardwareDriver::GetLatestSensorData
        // so it rides every post (incl. activation); no per-schedule check needed here.

        // DO PERFORM POST
        ESP_LOGI(TAG, "Both conditions met: Can post now (no post in last 5 min AND within scheduled window)");

        // Update last post attempt time when conditions are met
        update_last_post_time(static_cast<int64_t>(current_time));

        ESP_LOGI(TAG, "Enqueued LOGI Data");
        _parentStateMachine->addToPostQueue(_sensorData, selectedTransport);
        _parentStateMachine->transitionTo(ApplicationState::ApplicationState_Posting);
    }
    else
    {
        // DO NOT PERFORM POST
        //
        //
        //
        //
        
        if (!noPostInLastFiveMinutes)
        {
            ESP_LOGI(TAG, "Cannot post: last scheduled post attempt was less than/equal to 5 minutes ago");
        }
        if (!withinScheduledWindow)
        {
            ESP_LOGI(TAG, "Cannot post: not on a scheduled minute");
        }

        _parentStateMachine->transitionTo(ApplicationState::ApplicationState_Sleep);
    }
}

void ScheduleCheckStateMachine::transitionTo(ScheduleCheckState newState) 
{
    currentState = newState;
}

void ScheduleCheckStateMachine::setParentStateMachine(ApplicationStateMachine* parentStateMachine)
{
    // Set the pointer to the parent state machine
    _parentStateMachine = parentStateMachine;
}

bool ScheduleCheckStateMachine::IsValidDateTime(time_t now)
{
    if (now == static_cast<time_t>(-1))
    {
        return false;
    }

    if (localtime_r(&now, &_timeInfo) == nullptr)
    {
        return false;
    }

    if (_timeInfo.tm_hour < 0 || _timeInfo.tm_hour > 23 ||
        _timeInfo.tm_min < 0 || _timeInfo.tm_min > 59 ||
        _timeInfo.tm_wday < 0 || _timeInfo.tm_wday > 6)
    {
        return false;
    }

    return true;
}

bool ScheduleCheckStateMachine::IsProvisioned(const DeviceSettings& deviceSettings)
{
    // 1. Check for a valid device ID. If it's invalid, exit immediately.
    if (!deviceSettings.isDeviceIdValid())
    {
        ESP_LOGE(TAG, "Invalid device ID - device not provisioned.");
        return false;
    }

    // 2. Attempt to connect to Wi-Fi.
    ESP_LOGI(TAG, "Connecting to Wi-Fi (SSID: %s)...", MY_WIFI_SSID);
    const bool connected = _networkManager->Connect(MY_WIFI_SSID, MY_WIFI_PASS);

    if (!connected)
    {
        ESP_LOGE(TAG, "Failed to connect to Wi-Fi (SSID: %s)", MY_WIFI_SSID);
        return false;
    }

    // 3. If all checks passed, the device is considered provisioned.
    ESP_LOGI(TAG, "Device is provisioned and connected to Wi-Fi.");
    return true;
}

// Update the last post attempt time
void ScheduleCheckStateMachine::update_last_post_time(int64_t current_time) 
{
    last_post_attempt = current_time;
    has_valid_time = true;
    minutes_since_last_post = 0;  // Reset minute counter on any post
}
