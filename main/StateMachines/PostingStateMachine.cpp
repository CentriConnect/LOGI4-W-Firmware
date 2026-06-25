#include "PostingStateMachine.h"
#include <iostream>
#include <cstring>
#include <ctime>
#include "ApplicationStateMachine.h"
#include "AwsIotConfig.h"
#include "sdkconfig.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "hal/EspNvsStorage.h"
#include "logi/ResetCounter.h"
#include "logi/Faults.h"
#include "UDP/UdpTelemetryClient.h"
#include <cstdio>
#include <algorithm>

static const char *TAG = "PostingStateMachine";

PostingStateMachine::PostingStateMachine(AwsIotManager* awsIotManager, EspTimeKeeper* timeKeeper,
                                         ILogiHardwareDriver* driver,
                                         LogiSensorData& sensorData, DeviceSettings& deviceSettings):
      _awsIotManager(awsIotManager),
      _timeKeeper(timeKeeper),
      _driver(driver),
      _sensorData(sensorData),
      _deviceSettings(deviceSettings),
      _postSuccessful(false),
      _connectRetryCount(0),
      _fotaCheckComplete(false)
{
}

void PostingStateMachine::update()
{
    if (currentState != PostingState::PostingState_InitialEnter &&
        currentState != PostingState::PostingState_ExitAWS &&
        isPostingTimeoutExpired())
    {
        ESP_LOGE(TAG, "Posting process timed out after wifi_timeout=%lu seconds; exiting to sleep",
                 static_cast<unsigned long>(getConnectionTimeoutSeconds()));
        transitionTo(PostingState::PostingState_ExitAWS);
    }

    switch (currentState)
    {
        // Entry point: Initializes timers and ensures time is synced before proceeding.
        case PostingState::PostingState_InitialEnter:
        {
            ESP_LOGI(TAG, "Current State: Initial Enter");
            PostingStateInitialEnter();
            break;
        }
        // Final state: Disconnects from AWS and transitions back to the main state machine.
        case PostingState::PostingState_ExitAWS:
        {
            ESP_LOGI(TAG, "Current State: Exit AWS");
            PostingStateExitAWS();
            break;
        }
        // Connection handling: Attempts to connect to AWS IoT with a retry mechanism.
        case PostingState::PostingState_TryConnect:
        {
            ESP_LOGI(TAG, "Current State: Try Connect");
            PostingStateTryConnect();
            break;
        }
        case PostingState::PostingState_PostImmediateTelemetry:
        {
            ESP_LOGI(TAG, "Current State: Post Immediate Telemetry");
            PostingStatePostImmediateTelemetry();
            break;
        }
        // Subscribes to all necessary topics, like shadow delta and jobs.
        case PostingState::PostingState_SubscribeToTopics:
        {
            ESP_LOGI(TAG, "Current State: Subscribe To Topics");
            PostingStateSubscribeToTopics();
            break;
        }
        // Requests the full device shadow from AWS to get the latest configuration.
        case PostingState::PostingState_SendGetShadowDelta:
        {
            ESP_LOGI(TAG, "Current State: Send Get Shadow Delta");
            PostingStateSendGetShadowDelta();
            break;
        }
        // Logic to handle a received shadow delta update.
        case PostingState::PostingState_GotShadowDelta:
        {
            ESP_LOGI(TAG, "Current State: Got Shadow Delta");
            PostingStateGotShadowDelta();
            break;
        }
        // Processes the changes from a shadow delta.
        case PostingState::PostingState_HandleShadowDelta:
        {
            ESP_LOGI(TAG, "Current State: Handle Shadow Delta");
            PostingStateHandleShadowDelta();
            break;
        }
        // Checks for any pending Firmware-Over-The-Air (FOTA) update jobs.
        case PostingState::PostingState_CheckFOTA:
        {
            ESP_LOGI(TAG, "Current State: Check FOTA");
            PostingStateCheckFOTA();
            break;
        }
        // Downloads and applies a FOTA update if one is available.
        case PostingState::PostingState_DoFOTAUpdate:
        {
            ESP_LOGI(TAG, "Current State: Process FOTA");
            PostingStateDoFOTAUpdate();
            break;
        }
        case PostingState::PostingState_AcquireFinalSample:
        {
            ESP_LOGI(TAG, "Current State: Acquire Final Sample");
            PostingStateAcquireFinalSample();
            break;
        }
        case PostingState::PostingState_PostFinalTelemetry:
        {
            ESP_LOGI(TAG, "Current State: Post Final Telemetry");
            PostingStatePostFinalTelemetry();
            break;
        }
        // Processes the queue of sensor data, posting each entry to AWS.
        case PostingState::PostingState_DoPostsFromQueue:
        {
            ESP_LOGI(TAG, "Current State: Do Posts From Queue");
            PostingStateDoPostsFromQueue();
            break;
        }
        // Wait after successful post for cloud commands/OTA checks
        case PostingState::PostingState_PostDwell:
        {
            ESP_LOGI(TAG, "Current State: Post Dwell");
            PostingStatePostDwell();
            break;
        }
    }
}

void PostingStateMachine::transitionTo(PostingState newState) 
{
    currentState = newState;
}

void PostingStateMachine::setParentStateMachine(ApplicationStateMachine* parentStateMachine)
{
    _parentStateMachine = parentStateMachine;
}

bool PostingStateMachine::publishTelemetrySnapshot(const LogiSensorData& data, const char* label)
{
    TelemetryContext telemetryContext;
    populateTelemetryContext(telemetryContext);

    ESP_LOGI(TAG, "Publishing %s telemetry snapshot (LOGI4 format)", label ? label : "activation");
    bool published = _awsIotManager->PostTelemetryLogi4Format(data, telemetryContext);
    if (!published)
    {
        Faults_Set(FAULT_AWS);
    }
    return published;
}

bool PostingStateMachine::publishUdpTelemetrySnapshot(const LogiSensorData& data, const char* label)
{
    TelemetryContext telemetryContext;
    populateTelemetryContext(telemetryContext);

    ESP_LOGI(TAG, "Publishing %s telemetry snapshot (compact UDP)", label ? label : "scheduled");
    bool published = UdpTelemetryClient::SendTelemetry(data, telemetryContext);
    if (!published)
    {
        Faults_Set(FAULT_AWS);
    }
    return published;
}

bool PostingStateMachine::acquireGpsForMqttPost(LogiSensorData& data)
{
    if (!_shadowState.acquire_gps_valid || !_shadowState.acquire_gps)
    {
        ESP_LOGI(TAG, "Shadow acquire_gps is false/not set; skipping GPS acquisition for MQTT post");
        return false;
    }

    if (_driver == nullptr)
    {
        ESP_LOGW(TAG, "Shadow acquire_gps is true, but no hardware driver is available");
        return false;
    }

    const uint32_t timeoutMs = getPostDwellTimeSeconds() * 1000U;
    constexpr uint32_t GPS_POLL_MS = 1000;
    uint32_t waitedMs = 0;

    ESP_LOGI(TAG, "Shadow acquire_gps is true; acquiring GPS fix for MQTT post, timeout=%lu ms",
             static_cast<unsigned long>(timeoutMs));
    _driver->SetGnssPower(true);

    while (!_driver->HasValidGpsFix() && waitedMs < timeoutMs)
    {
        vTaskDelay(pdMS_TO_TICKS(GPS_POLL_MS));
        waitedMs += GPS_POLL_MS;

        if ((waitedMs % 10000U) == 0U)
        {
            ESP_LOGI(TAG, "Waiting for GPS fix before MQTT telemetry: %lu/%lu ms",
                     static_cast<unsigned long>(waitedMs),
                     static_cast<unsigned long>(timeoutMs));
            _driver->PrintGpsStatus();
        }
    }

    GpsData_t gpsData{};
    bool gotFix = _driver->GetGpsData(gpsData) && gpsData.valid;
    _driver->SetGnssPower(false);

    if (gotFix)
    {
        data.GPSData = gpsData;
        ESP_LOGI(TAG, "MQTT post GPS fix acquired: lat=%.6f lon=%.6f alt=%.1f rssi=%d",
                 data.GPSData.latitude,
                 data.GPSData.longitude,
                 data.GPSData.altitude,
                 data.GPSData.rssi);
        return true;
    }

    ESP_LOGW(TAG, "GPS fix not acquired before post_dwell_time timeout; using last saved/default coordinates");
    Faults_Set(FAULT_GPS);
    return false;
}

void PostingStateMachine::applyShadowSettingsToMemory(const DeviceShadowState& shadowState)
{
    if (_parentStateMachine) {
        _parentStateMachine->updateSchedulesFromShadow(shadowState);
    }

    if (shadowState.fill_dwell_time != 0) {
        _deviceSettings.setFillDwellTime(shadowState.fill_dwell_time);
    }
    if (shadowState.fill_alarm_delta != 0) {
        _deviceSettings.setFillAlarmDelta(shadowState.fill_alarm_delta);
    }
    if (shadowState.post_dwell_time != 0) {
        _deviceSettings.setPostDwellTime(shadowState.post_dwell_time);
    }
    if (shadowState.mqtt_timeout != 0) {
        _deviceSettings.setMqttTimeout(shadowState.mqtt_timeout);
    }
    if (shadowState.lte_timeout != 0) {
        _deviceSettings.setLteTimeout(shadowState.lte_timeout);
    }
    if (shadowState.sensor_sample_rate != 0) {
        _deviceSettings.setSensorSampleRateMinutes(shadowState.sensor_sample_rate);
    }
    if (shadowState.event_posts_valid) {
        _deviceSettings.setEventPosts(shadowState.event_posts);
    }
    if (!shadowState.event_thresholds_pct.empty()) {
        _deviceSettings.setEventThresholdsPct(shadowState.event_thresholds_pct.c_str());
    }
    if (!shadowState.event_direction.empty()) {
        _deviceSettings.setEventDirection(shadowState.event_direction.c_str());
    }
    if (!shadowState.mqtt_scheduled_post.empty()) {
        _deviceSettings.setMqttScheduledPost(shadowState.mqtt_scheduled_post.c_str());
    }

    _deviceSettings.Commit();
}

DeviceShadowState PostingStateMachine::buildReportedShadowFromSettings(const DeviceShadowState& parsedShadowState) const
{
    DeviceShadowState reported{};

    WeeklySchedule schedules[DeviceSettings::MAX_WEEKLY_SCHEDULES];
    if (_deviceSettings.getWeeklySchedules(schedules))
    {
        for (int i = 0; i < DeviceSettings::MAX_WEEKLY_SCHEDULES; ++i)
        {
            uint8_t dayByteWithEnable = 0;
            uint8_t hour = 0;
            uint8_t minute = 0;

            if (schedules[i].DaysOfWeek != 0)
            {
                dayByteWithEnable = schedules[i].DaysOfWeek | 0x80;
                hour = schedules[i].StartTime.Hour;
                minute = schedules[i].StartTime.Minute;
            }

            char scheduleBuf[16];
            snprintf(scheduleBuf, sizeof(scheduleBuf), "%02u:%02u;%02X",
                     static_cast<unsigned>(hour),
                     static_cast<unsigned>(minute),
                     static_cast<unsigned>(dayByteWithEnable));
            reported.post_schedule[i] = scheduleBuf;
        }
    }

    reported.fill_dwell_time = _deviceSettings.getFillDwellTime();
    reported.lte_timeout = _deviceSettings.getLteTimeout();
    reported.fill_alarm_delta = _deviceSettings.getFillAlarmDelta();
    reported.post_dwell_time = _deviceSettings.getPostDwellTime();
    char mqttScheduledPost[DeviceSettings::MQTT_SCHEDULED_POST_BUFFER_SIZE] = {0};
    if (_deviceSettings.getMqttScheduledPost(mqttScheduledPost, sizeof(mqttScheduledPost)))
    {
        reported.mqtt_scheduled_post = mqttScheduledPost;
    }
    reported.event_posts = _deviceSettings.getEventPosts();
    reported.event_posts_valid = true;
    char eventThresholds[DeviceSettings::EVENT_THRESHOLDS_BUFFER_SIZE] = {0};
    if (_deviceSettings.getEventThresholdsPct(eventThresholds, sizeof(eventThresholds)))
    {
        reported.event_thresholds_pct = eventThresholds;
    }
    char eventDirection[DeviceSettings::EVENT_DIRECTION_BUFFER_SIZE] = {0};
    if (_deviceSettings.getEventDirection(eventDirection, sizeof(eventDirection)))
    {
        reported.event_direction = eventDirection;
    }

    reported.sensor_sample_rate = _deviceSettings.getSensorSampleRateMinutes();

    // These fields currently do not have DeviceSettings storage, so report them
    // only when this cycle successfully parsed them from desired.
    reported.acquire_gps = parsedShadowState.acquire_gps;
    reported.acquire_gps_valid = parsedShadowState.acquire_gps_valid;
    reported.mqtt_timeout = parsedShadowState.mqtt_timeout;

    return reported;
}

void PostingStateMachine::PostingStateInitialEnter()
{
    ESP_LOGI(TAG, "Entering Posting State Machine");

    // Reset flags
    _connectRetryCount = 0;
    _fotaCheckComplete = false;
    _postSuccessful = false;
    _dwellStartTime = 0;
    _currentFotaJob.reset();

    // Record start time (may be invalid if not synced yet, will update after WiFi connects)
    _postStartTime = _timeKeeper->GetCurrentTime();
    _postStartUs = esp_timer_get_time();
    ESP_LOGI(TAG, "Starting post timer. Start time: %ld (may update after NTP sync)", (long int)_postStartTime);

    if (!_parentStateMachine || !_parentStateMachine->checkAndConnectWifi())
    {
        ESP_LOGW(TAG, "Wi-Fi connect failed or provisioning started; leaving posting state");
        if (_parentStateMachine && !_parentStateMachine->isPostQueueEmpty())
        {
            Faults_Set(FAULT_WIFI);
        }
        transitionTo(PostingState::PostingState_ExitAWS);
        return;
    }

    if (_parentStateMachine &&
        !_parentStateMachine->isPostQueueEmpty() &&
        _parentStateMachine->peekPostQueueTransport() == PostTransport::PostTransport_Udp)
    {
        if (!_timeKeeper->IsTimeSynced())
        {
            ESP_LOGI(TAG, "Synchronizing UTC time via NTP before compact UDP post...");
            if (_timeKeeper->SyncTime())
            {
                _postStartTime = _timeKeeper->GetCurrentTime();
            }
            else
            {
                ESP_LOGW(TAG, "NTP sync failed before UDP post; continuing with cached/invalid timestamp");
                Faults_Set(FAULT_NTP);
            }
        }

        transitionTo(PostingState::PostingState_DoPostsFromQueue);
        return;
    }

    // Proceed to connect - time sync happens after WiFi is connected
    transitionTo(PostingState::PostingState_TryConnect);
}

void PostingStateMachine::PostingStateExitAWS()
{
    ESP_LOGI(TAG, "Current State: PostingStateExitAWS");

    // Disconnect if connected
    if (_awsIotManager)
    {
        _awsIotManager->Disconnect();
    }

    // Clean up Jobs handler
    _jobsHandler = nullptr;
    _currentFotaJob.reset();

    // Transition back to parent state machine or handle error
    _parentStateMachine->transitionTo(ApplicationState::ApplicationState_Sleep);
}

void PostingStateMachine::PostingStateTryConnect()
{   
    ESP_LOGI(TAG, "Attempting AWS IoT MQTT waterfall");

    uint32_t remainingBudget = getRemainingPostingBudgetSeconds();
    if (remainingBudget < AWS_IOT_MIN_WATERFALL_TIMEOUT_S)
    {
        ESP_LOGW(TAG, "Remaining wifi_timeout budget too small for MQTT waterfall (%lu s); using UDP fallback/exit",
                 static_cast<unsigned long>(remainingBudget));
        Faults_Set(FAULT_AWS);
        transitionTo(PostingState::PostingState_DoPostsFromQueue);
        return;
    }

    if (!_timeKeeper->IsTimeSynced())
    {
        ESP_LOGI(TAG, "Synchronizing UTC time via NTP before AWS IoT TLS/MQTT connect...");
        if (_timeKeeper->SyncTime())
        {
            _postStartTime = _timeKeeper->GetCurrentTime();
        }
        else
        {
            ESP_LOGE(TAG, "NTP sync failed before AWS IoT connect.");
            Faults_Set(FAULT_NTP);
            _connectRetryCount++;

            if (_connectRetryCount >= MAX_CONNECT_RETRIES)
            {
                ESP_LOGE(TAG, "Max NTP sync retries exceeded. Exiting.");
                transitionTo(PostingState::PostingState_ExitAWS);
            }
            else
            {
                uint32_t delayMs = 1000 * (1 << (_connectRetryCount - 1));
                ESP_LOGI(TAG, "Will retry NTP sync in %lu ms...", delayMs);
                vTaskDelay(pdMS_TO_TICKS(delayMs));
            }
            return;
        }
    }
    
    remainingBudget = getRemainingPostingBudgetSeconds();
    uint32_t mqttTimeout = getMqttWaterfallTimeoutSeconds();
    uint32_t perLegTimeout = mqttTimeout;
    if (remainingBudget > 30U)
    {
        perLegTimeout = std::min(mqttTimeout, (remainingBudget - 30U) / 2U);
    }
    if (perLegTimeout < AWS_IOT_MIN_WATERFALL_TIMEOUT_S)
    {
        ESP_LOGW(TAG, "Remaining wifi_timeout budget too small for MQTT waterfall after NTP (%lu s); using UDP fallback/exit",
                 static_cast<unsigned long>(remainingBudget));
        Faults_Set(FAULT_AWS);
        transitionTo(PostingState::PostingState_DoPostsFromQueue);
        return;
    }

    ESP_LOGI(TAG, "MQTT waterfall per-leg timeout capped at %lu seconds by wifi_timeout budget",
             static_cast<unsigned long>(perLegTimeout));
    if (_awsIotManager->ConnectWithWaterfall(perLegTimeout))
    {
        ESP_LOGI(TAG, "Connection successful.");
        _connectRetryCount = 0;  // Reset retry counter on success
        ESP_LOGI(TAG, "AWS IoT connection established successfully");

        transitionTo(PostingState::PostingState_SendGetShadowDelta);
    }
    else
    {
        ESP_LOGE(TAG, "AWS MQTT waterfall failed; falling back queued MQTT post(s) to UDP.");
        Faults_Set(FAULT_AWS);
        transitionTo(PostingState::PostingState_DoPostsFromQueue);
    }
}

void PostingStateMachine::PostingStatePostImmediateTelemetry()
{
    ESP_LOGI(TAG, "Activation [1/5]: publishing immediate telemetry with currently available values");

    const LogiSensorData& data = (_parentStateMachine && !_parentStateMachine->isPostQueueEmpty())
        ? _parentStateMachine->peekPostQueue()
        : _sensorData;

    if (publishTelemetrySnapshot(data, "immediate"))
    {
        _postSuccessful = true;
    }
    else
    {
        ESP_LOGW(TAG, "Immediate telemetry publish failed; continuing activation cycle for shadow/jobs/final post");
    }

    transitionTo(PostingState::PostingState_SendGetShadowDelta);
}

void PostingStateMachine::PostingStateSubscribeToTopics()
{
    ESP_LOGI(TAG, "Current State: PostingStateSubscribeToTopics");

    // Time is expected to be valid before MQTT connect. Keep this defensive
    // check here in case a future path enters this state directly.
    if (!_timeKeeper->IsTimeSynced())
    {
        ESP_LOGI(TAG, "Syncing time via NTP before topic work...");
        if (_timeKeeper->SyncTime())
        {
            ESP_LOGI(TAG, "Time synchronized successfully");
            // Update post start time with accurate time
            _postStartTime = _timeKeeper->GetCurrentTime();
        }
        else
        {
            ESP_LOGW(TAG, "NTP sync failed, continuing with cached/invalid time");
            Faults_Set(FAULT_NTP);
        }
    }

    // Initialize Jobs handler with subscriptions
    if (_awsIotManager->InitializeJobsHandler(AWS_IOT_THING_NAME))
    {
        _jobsHandler = _awsIotManager->GetJobsHandler();
        ESP_LOGI(TAG, "Jobs handler initialized successfully");
    }
    else
    {
        ESP_LOGW(TAG, "Failed to initialize Jobs handler");
    }

    ESP_LOGI(TAG, "Topic subscriptions completed");

    // Move to get shadow state
    transitionTo(PostingState::PostingState_SendGetShadowDelta);
}

void PostingStateMachine::PostingStateSendGetShadowDelta()
{
    ESP_LOGI(TAG, "Current State: PostingStateSendGetShadowDelta");

    // This function sends the 'get' request and returns immediately.
    // The response will be handled asynchronously by the MQTT event handler.
    if (_awsIotManager->GetEnhancedShadow(_shadowState)) 
    {
        ESP_LOGI(TAG, "Activation [2/5]: shadow fetched; applying settings to memory");
        applyShadowSettingsToMemory(_shadowState);

        DeviceShadowState reportedState = buildReportedShadowFromSettings(_shadowState);
        if (_awsIotManager->UpdateShadowWithStatus(reportedState))
        {
            ESP_LOGI(TAG, "Shadow reported state updated after applying settings");
        }
        else
        {
            ESP_LOGW(TAG, "Failed to update shadow reported state after applying settings");
        }

        if (_shadowState.reset_provisioning) {
            ESP_LOGW(TAG, "Shadow reset_provisioning flag detected - clearing and rebooting");

            _shadowState.reset_provisioning = false;
            DeviceShadowState reportedState = buildReportedShadowFromSettings(_shadowState);
            _awsIotManager->UpdateShadowWithStatus(reportedState);
            vTaskDelay(pdMS_TO_TICKS(2000));

            EspNvsStorage stateStorage("DeviceState");
            if (stateStorage.Init()) {
                stateStorage.SaveString("force_prov", "1");
                stateStorage.Commit();
            }

            _awsIotManager->Disconnect();
            esp_restart();
            return;
        }
    } 
    else 
    {
        ESP_LOGE(TAG, "Activation [2/5]: failed to fetch shadow; continuing with existing settings");
    }

    transitionTo(PostingState::PostingState_CheckFOTA);
}

void PostingStateMachine::PostingStateGotShadowDelta()
{
    ESP_LOGI(TAG, "Current State: PostingStateGotShadowDelta");

    // Check the flag that is set by the onShadowDelta callback in AwsIotManager
    if (_awsIotManager->GetDeltaReceivedFlag())
    {
        ESP_LOGI(TAG, "Delta was received. Transitioning to handle it.");
        // A delta was received, so go to the state that processes the changes.
        transitionTo(PostingState::PostingState_HandleShadowDelta);
    }
    else
    {
        ESP_LOGI(TAG, "No delta was received. Proceeding to Post from Queue");
        // No delta, so skip the handler and proceed with the normal flow.
        transitionTo(PostingState::PostingState_DoPostsFromQueue);
    }
}

void PostingStateMachine::PostingStateHandleShadowDelta()
{
    ESP_LOGI(TAG, "Current State: PostingStateHandleShadowDelta");

    // 1. Get the current state, which has already been updated by the
    //    onShadowDelta callback in the background.
    _awsIotManager->GetCurrentShadowState(_shadowState);

    // 2. Persist accepted shadow values to DeviceSettings/NVS before reporting.
    applyShadowSettingsToMemory(_shadowState);

    // 3. Post the updated state back to the shadow's "reported" section.
    //    This confirms to the cloud that we have accepted and applied the changes.
    DeviceShadowState reportedState = buildReportedShadowFromSettings(_shadowState);
    if (_awsIotManager->UpdateShadowWithStatus(reportedState))
    {
        ESP_LOGI(TAG, "Successfully reported the updated shadow state.");
    }
    else
    {
        ESP_LOGE(TAG, "Failed to report the updated shadow state.");
    }

    // 4. Check for provisioning reset request from cloud
    if (_shadowState.reset_provisioning) {
        ESP_LOGW(TAG, "Shadow reset_provisioning flag detected — clearing and rebooting");

        _shadowState.reset_provisioning = false;
        reportedState = buildReportedShadowFromSettings(_shadowState);
        _awsIotManager->UpdateShadowWithStatus(reportedState);
        vTaskDelay(pdMS_TO_TICKS(2000));

        // Set force-provisioning NVS flag (persists across reboot)
        EspNvsStorage stateStorage("DeviceState");
        if (stateStorage.Init()) {
            stateStorage.SaveString("force_prov", "1");
            stateStorage.Commit();
        }

        _awsIotManager->Disconnect();
        esp_restart();
        return;
    }

    // 5. After handling the delta, proceed to the next step in the workflow.
    transitionTo(PostingState::PostingState_CheckFOTA);
}

void PostingStateMachine::PostingStateCheckFOTA()
{
    ESP_LOGI(TAG, "Current State: PostingStateCheckFOTA");

    if (_awsIotManager->IsConnectedViaBackup443())
    {
        ESP_LOGW(TAG, "Connected via AWS MQTT backup 443; skipping jobs/OTA on this link");
        transitionTo(PostingState::PostingState_DoPostsFromQueue);
        return;
    }

    ESP_LOGI(TAG, "Checking AWS IoT jobs / OTA before MQTT telemetry");

    if (_awsIotManager->GetJobsHandler() == nullptr)
    {
        if (_awsIotManager->InitializeJobsHandler(AWS_IOT_THING_NAME))
        {
            _jobsHandler = _awsIotManager->GetJobsHandler();
            ESP_LOGI(TAG, "Jobs handler initialized successfully");
        }
        else
        {
            ESP_LOGW(TAG, "Failed to initialize Jobs handler");
        }
    }
    
    AwsIotJobsHandler* jobsHandler = _awsIotManager->GetJobsHandler();
    if (!jobsHandler) 
    {
        ESP_LOGW(TAG, "Jobs handler not initialized");
        transitionTo(PostingState::PostingState_DoPostsFromQueue);
        return;
    }
    
    // Request pending jobs
    jobsHandler->RequestPendingJobs();
    
    // Wait a bit for response
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // Check if there's a job
    AwsIotJob job;
    if (jobsHandler->GetNextJob(job)) 
    {
        ESP_LOGI(TAG, "FOTA job found: %s", job.job_id.c_str());
        ESP_LOGI(TAG, "Firmware version: %s", job.firmware_version.c_str());
        
        // Check if update is needed
        DeviceShadowState currentState;
        _awsIotManager->GetCurrentShadowState(currentState);
        
        if (job.firmware_version != currentState.firmware_version || job.force_update) 
        {
            ESP_LOGI(TAG, "Firmware update needed. Current: %s, Target: %s", currentState.firmware_version.c_str(), job.firmware_version.c_str());
            
            _currentFotaJob = job;
            transitionTo(PostingState::PostingState_DoFOTAUpdate);
        } 
        else 
        {
            ESP_LOGI(TAG, "Firmware already up to date");
            
            jobsHandler->UpdateJobStatus(job.job_id, JobExecutionStatus::SUCCEEDED, "Firmware already up to date");
            transitionTo(PostingState::PostingState_DoPostsFromQueue);
        }
    } 
    else 
    {
        ESP_LOGI(TAG, "No pending FOTA jobs");
        transitionTo(PostingState::PostingState_DoPostsFromQueue);
    }
}

void PostingStateMachine::PostingStateDoFOTAUpdate()
{
    ESP_LOGI(TAG, "Current State: PostingStateDoFOTAUpdate");
    
    AwsIotJobsHandler* jobsHandler = _awsIotManager->GetJobsHandler();
    if (!jobsHandler || !_currentFotaJob.has_value()) 
    {
        ESP_LOGE(TAG, "No FOTA job to process");
        transitionTo(PostingState::PostingState_DoPostsFromQueue);
        return;
    }
    
    // Set progress callback
    jobsHandler->SetFotaProgressCallback
    (
        [](size_t downloaded, size_t total) 
        {
            static uint32_t lastPercent = 0;
            uint32_t percent = (downloaded * 100) / total;
            if (percent != lastPercent && percent % 10 == 0) {
                ESP_LOGI(TAG, "FOTA Progress: %u%%", static_cast<unsigned int>(percent));
                lastPercent = percent;
            }
        }
    );
    
    // Process the FOTA job (this will handle download, verification, and reboot)
    if (jobsHandler->ProcessFotaJob(_currentFotaJob.value())) 
    {
        ESP_LOGI(TAG, "FOTA update successful, device will restart");
        
        // Device will restart, so we won't reach here
    } 
    else 
    {
        ESP_LOGE(TAG, "FOTA update failed");
        // Continue with normal operation
        
    }

    transitionTo(PostingState::PostingState_DoPostsFromQueue);
}

void PostingStateMachine::PostingStateAcquireFinalSample()
{
    ESP_LOGI(TAG, "Activation [4/5]: acquiring GPS for final post (reusing DataSample sensors)");

    ILogiHardwareDriver* driver = _parentStateMachine ? _parentStateMachine->GetHardwareDriver() : nullptr;
    if (driver == nullptr)
    {
        ESP_LOGW(TAG, "No hardware driver available; final telemetry will reuse current sample");
        transitionTo(PostingState::PostingState_PostFinalTelemetry);
        return;
    }

    // V13-010: do NOT re-measure here. A 2nd back-to-back UpdateMeasurements gives
    // bad ADS1015 reads (wrong bat/sol/supv -> wrong fuel). Reuse the DataSample
    // sensors already in _sensorData and only acquire GPS (mirrors the activation
    // measure-once approach). Re-enable GNSS (DataSample's GetLatestSensorData
    // powered it off) so it can get a fix during the wait below.
    driver->SetGnssPower(true);

    constexpr uint32_t GPS_WAIT_MS = 120000;
    constexpr uint32_t GPS_POLL_MS = 1000;
    uint32_t waitedMs = 0;

    while (!driver->HasValidGpsFix() && waitedMs < GPS_WAIT_MS)
    {
        vTaskDelay(pdMS_TO_TICKS(GPS_POLL_MS));
        waitedMs += GPS_POLL_MS;

        if ((waitedMs % 10000) == 0)
        {
            ESP_LOGI(TAG, "Waiting for GPS fix before final telemetry: %lu/%lu ms",
                     (unsigned long)waitedMs, (unsigned long)GPS_WAIT_MS);
            driver->PrintGpsStatus();
        }
    }

    driver->GetGpsData(_sensorData.GPSData);   // merge ONLY GPS into the reused DataSample snapshot
    driver->SetGnssPower(false);
    _sensorData.elapsedTimeStampS = static_cast<uint32_t>(_timeKeeper->GetCurrentTime());

    if (_sensorData.GPSData.valid)
    {
        ESP_LOGI(TAG, "Final sample includes GPS fix: lat=%.6f lon=%.6f alt=%.1f rssi=%d",
                 _sensorData.GPSData.latitude,
                 _sensorData.GPSData.longitude,
                 _sensorData.GPSData.altitude,
                 _sensorData.GPSData.rssi);
    }
    else
    {
        ESP_LOGW(TAG, "GPS fix not acquired before timeout; final telemetry will report GPS defaults");
        Faults_Set(FAULT_GPS);
    }

    transitionTo(PostingState::PostingState_PostFinalTelemetry);
}

void PostingStateMachine::PostingStatePostFinalTelemetry()
{
    ESP_LOGI(TAG, "Activation [4/5]: publishing final telemetry after shadow/jobs/GPS acquisition");

    if (publishTelemetrySnapshot(_sensorData, "final"))
    {
        _postSuccessful = true;
        // Final post landed: this cycle's faults have been reported, so reset the
        // accumulator. The next cycle starts clean. (Only after the FINAL post.)
        Faults_Clear();
        while (_parentStateMachine && !_parentStateMachine->isPostQueueEmpty())
        {
            _parentStateMachine->removeFirstFromPostQueue();
        }
    }
    else
    {
        ESP_LOGE(TAG, "Final telemetry publish failed");
    }

    _dwellStartTime = _timeKeeper->GetCurrentTime();
    ESP_LOGI(TAG, "Activation [5/5]: post dwell (%lu seconds) with LED OFF, then sleep",
             (unsigned long)getPostDwellTimeSeconds());

    // No LED in the normal duty loop: the LED is provisioning-blue and the
    // 30-min post-activation green ONLY. Keep it off through dwell + deep sleep.
    ILogiHardwareDriver* driver = _parentStateMachine ? _parentStateMachine->GetHardwareDriver() : nullptr;
    if (driver != nullptr)
    {
        driver->SetLedState(LedState::LedState_Off);
    }

    transitionTo(PostingState::PostingState_PostDwell);
}

void PostingStateMachine::PostingStateDoPostsFromQueue()
{
    ESP_LOGI(TAG, "Current State: PostingStateDoPostsFromQueue");

    bool anyPostSucceeded = false;

    // Populate telemetry context once (device info doesn't change between posts)
    TelemetryContext telemetryContext;
    populateTelemetryContext(telemetryContext);

    while (!_parentStateMachine->isPostQueueEmpty())
    {
        LogiSensorData data = _parentStateMachine->peekPostQueue();
        PostTransport transport = _parentStateMachine->peekPostQueueTransport();
        ESP_LOGI(TAG, "Attempting to post next item from queue (%s)...",
                 transport == PostTransport::PostTransport_Udp ? "compact UDP" : "LOGI4 MQTT");

        bool posted = false;
        if (transport == PostTransport::PostTransport_Udp)
        {
            posted = UdpTelemetryClient::SendTelemetry(data, telemetryContext);
        }
        else if (!_awsIotManager->GetAwsClient()->IsConnected())
        {
            ESP_LOGW(TAG, "MQTT unavailable; sending queued MQTT post via compact UDP fallback");
            posted = UdpTelemetryClient::SendTelemetry(data, telemetryContext);
        }
        else
        {
            acquireGpsForMqttPost(data);
            posted = _awsIotManager->PostTelemetryLogi4Format(data, telemetryContext);
        }

        if (posted)
        {
            ESP_LOGI(TAG, "Post successful for one item.");
            _parentStateMachine->removeFirstFromPostQueue();
            anyPostSucceeded = true;
        }
        else
        {
            ESP_LOGE(TAG, "Failed to post item from queue. Stopping post attempts to save battery.");
            break;
        }
    }

    if (_parentStateMachine->isPostQueueEmpty())
    {
        ESP_LOGI(TAG, "Post queue is now empty.");
    }

    if (anyPostSucceeded)
    {
        _postSuccessful = true;
        ESP_LOGI(TAG, "Post successful; exiting immediately to sleep");
        transitionTo(PostingState::PostingState_ExitAWS);
    }
    else
    {
        // No posts succeeded, exit immediately
        transitionTo(PostingState::PostingState_ExitAWS);
    }
}

void PostingStateMachine::PostingStatePostDwell()
{
    ESP_LOGI(TAG, "Current State: PostingStatePostDwell");

    // Get configurable dwell time
    uint32_t dwellTime = getPostDwellTimeSeconds();
    time_t currentTime = _timeKeeper->GetCurrentTime();
    time_t elapsedTime = currentTime - _dwellStartTime;

    // Check if dwell time has elapsed
    if (elapsedTime >= dwellTime)
    {
        ESP_LOGI(TAG, "Post dwell complete after %ld seconds", (long)elapsedTime);
        transitionTo(PostingState::PostingState_ExitAWS);
        return;
    }

    // Still in dwell period - check for any incoming messages (OTA jobs, shadow updates)
    // The AWS connection is still active, so message handlers will process incoming data
    ESP_LOGD(TAG, "Post dwell in progress: %ld / %lu seconds",
             (long)elapsedTime, (unsigned long)dwellTime);

    // Small delay to prevent busy-waiting
    vTaskDelay(pdMS_TO_TICKS(1000));
}

uint32_t PostingStateMachine::getConnectionTimeoutSeconds() const
{
    // Get timeout from DeviceSettings (stored as lte_timeout for cloud compatibility,
    // but functionally serves as WiFi connection timeout in this variant)
    uint32_t timeout = _deviceSettings.getLteTimeout();

    // Enforce minimum
    if (timeout < CONFIG_LOGI_MIN_LTE_TIMEOUT_S)
    {
        timeout = CONFIG_LOGI_MIN_LTE_TIMEOUT_S;
    }

    return timeout;
}

bool PostingStateMachine::isPostingTimeoutExpired() const
{
    if (_postStartUs <= 0)
    {
        return false;
    }

    const int64_t elapsedUs = esp_timer_get_time() - _postStartUs;
    const int64_t timeoutUs = static_cast<int64_t>(getConnectionTimeoutSeconds()) * 1000000LL;
    return elapsedUs >= timeoutUs;
}

uint32_t PostingStateMachine::getRemainingPostingBudgetSeconds() const
{
    if (_postStartUs <= 0)
    {
        return getConnectionTimeoutSeconds();
    }

    const int64_t elapsedUs = esp_timer_get_time() - _postStartUs;
    const int64_t timeoutUs = static_cast<int64_t>(getConnectionTimeoutSeconds()) * 1000000LL;
    if (elapsedUs >= timeoutUs)
    {
        return 0;
    }

    return static_cast<uint32_t>((timeoutUs - elapsedUs + 999999LL) / 1000000LL);
}

uint32_t PostingStateMachine::getMqttWaterfallTimeoutSeconds() const
{
    uint32_t timeout = _deviceSettings.getMqttTimeout();
    if (timeout < AWS_IOT_MIN_WATERFALL_TIMEOUT_S)
    {
        timeout = AWS_IOT_MIN_WATERFALL_TIMEOUT_S;
    }
    return timeout;
}

uint32_t PostingStateMachine::getPostDwellTimeSeconds() const
{
    // Get dwell time from DeviceSettings
    uint32_t dwellTime = _deviceSettings.getPostDwellTime();

    // Enforce minimum (30 seconds per spec)
    if (dwellTime < CONFIG_LOGI_MIN_POST_DWELL_TIME_S)
    {
        dwellTime = CONFIG_LOGI_MIN_POST_DWELL_TIME_S;
    }

    return dwellTime;
}

void PostingStateMachine::populateTelemetryContext(TelemetryContext& ctx) const
{
    // Initialize all validity flags to false
    memset(&ctx, 0, sizeof(TelemetryContext));

    // === Device ID ===
    char deviceId[DeviceSettings::DEVICE_ID_BUFFER_SIZE];
    if (_deviceSettings.getDeviceId(deviceId, sizeof(deviceId)) && _deviceSettings.isDeviceIdValid())
    {
        strncpy(ctx.deviceId, deviceId, sizeof(ctx.deviceId) - 1);
        ctx.deviceId[sizeof(ctx.deviceId) - 1] = '\0';
        ctx.deviceIdValid = true;
    }

    // === IMEI and ICCID (WiFi variant uses "WIFI") ===
    strncpy(ctx.imei, "WIFI", sizeof(ctx.imei) - 1);
    ctx.imei[sizeof(ctx.imei) - 1] = '\0';
    ctx.imeiValid = true;

    strncpy(ctx.iccid, "WIFI", sizeof(ctx.iccid) - 1);
    ctx.iccid[sizeof(ctx.iccid) - 1] = '\0';
    ctx.iccidValid = true;

    // === Timestamp (ISO 8601) ===
    time_t now = _timeKeeper->GetCurrentTime();
    if (now >= 1609459200)
    {
        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);
        strftime(ctx.dateTimeIso, sizeof(ctx.dateTimeIso), "%Y-%m-%dT%H:%M:%S.000", &timeinfo);
        ctx.dateTimeIsoValid = true;
    }

    // === MQTT Schema Version ===
    snprintf(ctx.mqttSchema, sizeof(ctx.mqttSchema), "%d.%d.%d",
             CONFIG_LOGI_MQTT_VERSION_MAJOR,
             CONFIG_LOGI_MQTT_VERSION_MINOR,
             CONFIG_LOGI_MQTT_VERSION_REVISION);
    ctx.mqttSchemaValid = true;

    // === Device Version (HW, SW, MQTT schema major.minor) ===
    snprintf(ctx.deviceVersion, sizeof(ctx.deviceVersion), "%d.%d,%d.%d,%d.%d.%d",
             CONFIG_LOGI_HARDWARE_VERSION_MAJOR,
             CONFIG_LOGI_HARDWARE_VERSION_MINOR,
             CONFIG_LOGI_SOFTWARE_VERSION_MAJOR,
             CONFIG_LOGI_SOFTWARE_VERSION_MINOR,
             CONFIG_LOGI_MQTT_VERSION_MAJOR,
             CONFIG_LOGI_MQTT_VERSION_MINOR,
             CONFIG_LOGI_MQTT_VERSION_REVISION);
    ctx.deviceVersionValid = true;

    // === Modem Firmware Version (empty for WiFi variant) ===
    ctx.modemFirmwareVersion[0] = '\0';
    ctx.modemFirmwareVersionValid = false;

    // === WiFi RSSI (replaces LTE signal quality) ===
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
    {
        ctx.lteSignalQuality = ap_info.rssi;
        ctx.lteSignalQualityValid = true;
    }

    // === Charger Status ===
    ctx.chargerStatus = (_driver != nullptr && _driver->IsChargingErrorActive()) ? 1 : 0;
    ctx.chargerStatusValid = true;

    // === BLE Status (not implemented in WiFi variant) ===
    ctx.bleStatus = 0;
    ctx.bleStatusValid = true;

    // === Error Log (rendered from the per-post fault accumulator) ===
    Faults_Render(ctx.errorLog, sizeof(ctx.errorLog), nullptr);
    ctx.errorLogValid = true;

    // === Reset Counter ===
    ctx.resetCounter = LogiResetCounter_Get();
    ctx.resetCounterValid = true;

    // === Configuration Echo ===
    // Posting schedule - convert WeeklySchedule array to JSON string
    WeeklySchedule schedules[DeviceSettings::MAX_WEEKLY_SCHEDULES];
    _deviceSettings.getWeeklySchedules(schedules);

    // Build comma-separated posting schedule string. Always emit all 8 slots;
    // disabled/empty slots are represented as "00:00;00" per REV B.
    // Format: "HH:MM;XX" where XX is day bitmask with bit 7 as enable flag
    char* schedPtr = ctx.postingSchedule;
    size_t schedRemaining = sizeof(ctx.postingSchedule);
    int written = 0;

    for (size_t i = 0; i < DeviceSettings::MAX_WEEKLY_SCHEDULES && schedRemaining > 20; i++)
    {
        uint8_t dayByteWithEnable = 0;
        uint8_t hour = 0;
        uint8_t minute = 0;

        if (schedules[i].DaysOfWeek != 0) {
            dayByteWithEnable = schedules[i].DaysOfWeek | 0x80;
            hour = schedules[i].StartTime.Hour;
            minute = schedules[i].StartTime.Minute;
        }

        written = snprintf(schedPtr, schedRemaining, "%s%02d:%02d;%02X",
                          i == 0 ? "" : ", ",
                          hour,
                          minute,
                          dayByteWithEnable);
        schedPtr += written;
        schedRemaining -= written;
    }
    ctx.postingScheduleValid = true;

    // Fill dwell time
    ctx.fillDwellTime = _deviceSettings.getFillDwellTime();
    ctx.fillDwellTimeValid = true;

    // Fill post delta value
    ctx.fillPostDeltaValue = _deviceSettings.getFillAlarmDelta();
    ctx.fillPostDeltaValueValid = true;

    // LTE attempt timeout (WiFi connection timeout)
    ctx.lteAttemptTimeout = _deviceSettings.getLteTimeout();
    ctx.lteAttemptTimeoutValid = true;

    // Post dwell time
    ctx.postDwellTime = _deviceSettings.getPostDwellTime();
    ctx.postDwellTimeValid = true;

    // Sensor sample rate in minutes
    ctx.sensorSampleRateMinutes = _deviceSettings.getSensorSampleRateMinutes();
    ctx.sensorSampleRateMinutesValid = true;

    // MQTT connection port used for this publish
    strncpy(ctx.mqttPort,
            (_awsIotManager != nullptr && _awsIotManager->IsConnectedViaBackup443()) ? "443" : "8883",
            sizeof(ctx.mqttPort) - 1);
    ctx.mqttPort[sizeof(ctx.mqttPort) - 1] = '\0';
    ctx.mqttPortValid = true;

    // MQTT scheduled post echo
    char mqttScheduledPost[DeviceSettings::MQTT_SCHEDULED_POST_BUFFER_SIZE] = {0};
    if (_deviceSettings.getMqttScheduledPost(mqttScheduledPost, sizeof(mqttScheduledPost)))
    {
        strncpy(ctx.mqttScheduledPost, mqttScheduledPost, sizeof(ctx.mqttScheduledPost) - 1);
        ctx.mqttScheduledPost[sizeof(ctx.mqttScheduledPost) - 1] = '\0';
        ctx.mqttScheduledPostValid = true;
    }

    // Event settings echo
    ctx.eventPosts = _deviceSettings.getEventPosts();
    ctx.eventPostsValid = true;

    char eventThresholds[DeviceSettings::EVENT_THRESHOLDS_BUFFER_SIZE] = {0};
    if (_deviceSettings.getEventThresholdsPct(eventThresholds, sizeof(eventThresholds)) && eventThresholds[0] != '\0')
    {
        strncpy(ctx.eventThresholdsPct, eventThresholds, sizeof(ctx.eventThresholdsPct) - 1);
        ctx.eventThresholdsPct[sizeof(ctx.eventThresholdsPct) - 1] = '\0';
        ctx.eventThresholdsPctValid = true;
    }
}
