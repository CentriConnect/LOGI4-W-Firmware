#include "AwsIotManager.h"
#include "AwsIotClient.h"
#include "AwsIotJobsHandler.h"
#include "ShadowParser.h"
#include "esp_log.h"
#include "DeviceShadowState.h"
#include "cJSON.h"
#include "esp_system.h"
#include "esp_timer.h"
#include <time.h>
#include <cstdio> 

const char* AwsIotManager::TAG = "AwsIotManager";

AwsIotManager::AwsIotManager()
    : shadow_synced(false),
      delta_received_flag(false) {
    aws_client = std::make_unique<AwsIotClient>();
}

AwsIotManager::~AwsIotManager() {
    Disconnect();
}

bool AwsIotManager::Initialize() 
{
    if (!aws_client->Initialize()) 
    {
        return false;
    }

    aws_client->SetShadowDeltaCallback
    (
        [this](const DeviceShadowState& newState) 
        {
            this->onShadowDelta(newState);
        }
    );
    return true;
}

bool AwsIotManager::Connect() 
{
    return ConnectWithProfile(AwsIotConnectionProfile::Primary8883,
                              AWS_IOT_DEFAULT_WATERFALL_TIMEOUT_S);
}

bool AwsIotManager::ConnectWithProfile(AwsIotConnectionProfile profile, uint32_t timeoutSeconds)
{
    if (timeoutSeconds < AWS_IOT_MIN_WATERFALL_TIMEOUT_S)
    {
        timeoutSeconds = AWS_IOT_MIN_WATERFALL_TIMEOUT_S;
    }

    AwsIotClient* client = static_cast<AwsIotClient*>(aws_client.get());
    return client->ConnectWithProfile(profile, timeoutSeconds * 1000UL);
}

bool AwsIotManager::ConnectWithWaterfall(uint32_t timeoutSeconds)
{
    if (timeoutSeconds < AWS_IOT_MIN_WATERFALL_TIMEOUT_S)
    {
        timeoutSeconds = AWS_IOT_MIN_WATERFALL_TIMEOUT_S;
    }

    ESP_LOGI(TAG, "AWS MQTT waterfall: trying primary 8883 for %lu seconds",
             static_cast<unsigned long>(timeoutSeconds));
    if (ConnectWithProfile(AwsIotConnectionProfile::Primary8883, timeoutSeconds))
    {
        ESP_LOGI(TAG, "AWS MQTT waterfall connected on primary 8883");
        return true;
    }

    Disconnect();

    ESP_LOGW(TAG, "AWS MQTT primary 8883 failed; trying backup 443 for %lu seconds",
             static_cast<unsigned long>(timeoutSeconds));
    if (ConnectWithProfile(AwsIotConnectionProfile::Backup443, timeoutSeconds))
    {
        ESP_LOGI(TAG, "AWS MQTT waterfall connected on backup 443");
        return true;
    }

    Disconnect();
    ESP_LOGE(TAG, "AWS MQTT waterfall failed on both primary 8883 and backup 443");
    return false;
}

bool AwsIotManager::IsConnectedViaBackup443() const
{
    if (!aws_client)
    {
        return false;
    }

    const AwsIotClient* client = static_cast<const AwsIotClient*>(aws_client.get());
    return client->IsBackup443Connection();
}

void AwsIotManager::Disconnect() 
{
    if (aws_client && aws_client->IsConnected()) 
    {
        aws_client->Disconnect();
        shadow_synced = false;
    }
}

bool AwsIotManager::ShouldPostTelemetry(uint32_t minutesSinceLastPost) 
{
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);

    for (int i = 0; i < MAX_SCHEDULE_ENTRIES; ++i) 
    {
        const std::string& schedule_str = shadow_state.post_schedule[i];
        if(schedule_str.empty()) 
        {
            continue; 
        }

        int hour = 0, minute = 0;
        unsigned int day_byte = 0;

        if (sscanf(schedule_str.c_str(), "%d:%d;%x", &hour, &minute, &day_byte) != 3) 
        {
            continue;
        }

        if (!(day_byte & 0x80)) 
        {
            continue;
        }

        if (!(day_byte & (1 << timeinfo.tm_wday))) 
        {
            continue;
        }

        bool time_to_post = (timeinfo.tm_hour == hour) && (timeinfo.tm_min == minute);

        if (time_to_post && minutesSinceLastPost > 1) 
        {
            ESP_LOGI(TAG, "Scheduled post time met for schedule: %s", schedule_str.c_str());
            return true;
        }
    }

    return false;
}

bool AwsIotManager::IsPostingEnabled() const {
    for (int i = 0; i < MAX_SCHEDULE_ENTRIES; ++i) 
    {
        const std::string& schedule_str = shadow_state.post_schedule[i];
        if(schedule_str.empty()) 
        {
            continue;
        }

        unsigned int day_byte = 0;
        if (sscanf(schedule_str.c_str(), "%*d:%*d;%x", &day_byte) == 1) 
        {
            if (day_byte & 0x80) return true;
        }
    }
    return false;
}

bool AwsIotManager::PostTelemetry(const LogiSensorData& sensorData) 
{
    if (!aws_client->IsConnected()) 
    {
        return false;
    }

    return aws_client->PublishTelemetry(sensorData);
}

bool AwsIotManager::PostTelemetryAndWait(const LogiSensorData& sensorData)
{
    AwsIotClient* client = static_cast<AwsIotClient*>(aws_client.get());
    return client->PublishTelemetryAndWait(sensorData);
}

bool AwsIotManager::PostTelemetryLogi4Format(const LogiSensorData& sensorData, const TelemetryContext& context)
{
    if (!aws_client->IsConnected())
    {
        return false;
    }

    AwsIotClient* client = static_cast<AwsIotClient*>(aws_client.get());
    return client->PublishTelemetryLogi4Format(sensorData, context);
}

bool AwsIotManager::SyncDeviceShadow() {
    if (!aws_client->IsConnected()) 
    {
        return false;
    }

    DeviceShadowState newState;
    if (aws_client->GetDeviceShadow(newState)) 
    {
        shadow_state = newState;
        shadow_synced = true;
        return true;
    }
    
    return false;
}

uint32_t AwsIotManager::GetPostingIntervalMinutes() const {
    return shadow_state.telemetry_interval_seconds / 60;
}

bool AwsIotManager::InitializeJobsHandler(const std::string& thingName) {
    if (!aws_client || !aws_client->IsConnected())
    {
        return false;
    }

    jobs_handler = std::make_unique<AwsIotJobsHandler>(GetAwsClient());

    if (!jobs_handler->Initialize(thingName))
    {
        jobs_handler.reset();
        return false;
    }

    // Wire up MQTT message forwarding so job topic messages reach the handler
    GetAwsClient()->SetGenericMessageCallback(
        [this](const std::string& topic, const std::string& data) {
            if (jobs_handler) {
                jobs_handler->handleJobNotification(topic, data);
            }
        }
    );

    return true;
}

bool AwsIotManager::GetEnhancedShadow(DeviceShadowState& state) {
    if (!aws_client->IsConnected()) 
    {
        return false;
    }
    
    if (aws_client->GetDeviceShadow(state)) 
    {
        shadow_state = state;
        shadow_synced = true;
        return true;
    }
    
    return false;
}

bool AwsIotManager::PostEnhancedTelemetry(const LogiSensorData& sensorData,
                                          const DeviceShadowState& deviceState) {
    if (!aws_client->IsConnected()) 
    {
        return false;
    }
    
    bool result = aws_client->PublishTelemetry(sensorData);
    
    return result;
}

bool AwsIotManager::UpdateShadowWithStatus(const DeviceShadowState& state,
                                           const LogiSensorData* sensorData) {
    if (!aws_client->IsConnected()) 
    {
        return false;
    }
      
    return aws_client->UpdateDeviceShadow(state);
}

void AwsIotManager::onShadowDelta(const DeviceShadowState& deltaState) 
{   
    MergeShadowDelta(shadow_state, deltaState);
    shadow_synced = true;
    delta_received_flag = true; 
}
