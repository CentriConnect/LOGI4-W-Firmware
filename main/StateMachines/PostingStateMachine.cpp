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

static const char *TAG = "PostingStateMachine";

PostingStateMachine::PostingStateMachine(AwsIotManager* awsIotManager, EspTimeKeeper* timeKeeper,
                                         LogiSensorData& sensorData, const DeviceSettings& deviceSettings):
      _awsIotManager(awsIotManager),
      _timeKeeper(timeKeeper),
      _sensorData(sensorData),
      _deviceSettings(deviceSettings),
      _postSuccessful(false),
      _connectRetryCount(0),
      _fotaCheckComplete(false)
{
}

void PostingStateMachine::update()
{
    // Check connection timeout (uses configurable value from shadow, internally renamed from lte_timeout)
    uint32_t connectionTimeout = getConnectionTimeoutSeconds();
    if (_postStartTime != 0 && (_timeKeeper->GetCurrentTime() - _postStartTime) > connectionTimeout)
    {
        ESP_LOGE(TAG, "Posting process timed out after %lu seconds!", (unsigned long)connectionTimeout);
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
    ESP_LOGI(TAG, "Starting post timer. Start time: %ld (may update after NTP sync)", (long int)_postStartTime);

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
    _jobsHandler.reset();
    _currentFotaJob.reset();

    // Transition back to parent state machine or handle error
    _parentStateMachine->transitionTo(ApplicationState::ApplicationState_Sleep);
}

void PostingStateMachine::PostingStateTryConnect()
{   
    ESP_LOGI(TAG, "Attempting to connect to AWS IoT... (Attempt %d/%d)", static_cast<unsigned int>(_connectRetryCount + 1), static_cast<unsigned int>(MAX_CONNECT_RETRIES));
    
    // The Connect() method returns true if successful
    if (_awsIotManager->Connect())
    {
        ESP_LOGI(TAG, "Connection successful.");
        _connectRetryCount = 0;  // Reset retry counter on success
        // Move to AWS Connected state
        ESP_LOGI(TAG, "AWS IoT connection established successfully");

        transitionTo(PostingState::PostingState_SubscribeToTopics);
    }
    else
    {
        ESP_LOGE(TAG, "Connection failed.");
        _connectRetryCount++;

        // Check if we've exceeded max retries
        if (_connectRetryCount >= MAX_CONNECT_RETRIES)
        {
            ESP_LOGE(TAG, "Max connection retries exceeded. Exiting.");

            transitionTo(PostingState::PostingState_ExitAWS);
        } 
        else 
        {
            // Stay in TryConnect state for retry (will be called again on next update)
            uint32_t delayMs = 1000 * (1 << (_connectRetryCount - 1)); // Exponential backoff: 1s, 2s
            ESP_LOGI(TAG, "Will retry connection in %lu ms...", delayMs);
            vTaskDelay(pdMS_TO_TICKS(delayMs));
            // Stay in current state for retry
        }
    }
}

void PostingStateMachine::PostingStateSubscribeToTopics()
{
    ESP_LOGI(TAG, "Current State: PostingStateSubscribeToTopics");

    // Sync time via NTP now that WiFi is connected
    if (!_timeKeeper->IsTimeSynced())
    {
        ESP_LOGI(TAG, "Syncing time via NTP (WiFi now connected)...");
        if (_timeKeeper->SyncTime())
        {
            ESP_LOGI(TAG, "Time synchronized successfully");
            // Update post start time with accurate time
            _postStartTime = _timeKeeper->GetCurrentTime();
        }
        else
        {
            ESP_LOGW(TAG, "NTP sync failed, continuing with cached/invalid time");
        }
    }

    // Initialize Jobs handler with subscriptions
    if (_awsIotManager->InitializeJobsHandler(AWS_IOT_THING_NAME))
    {
        _jobsHandler = std::unique_ptr<AwsIotJobsHandler>(_awsIotManager->GetJobsHandler());
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
        ESP_LOGI(TAG, "Shadow request sent successfully.");
    } 
    else 
    {
        ESP_LOGE(TAG, "Failed to send shadow request.");
    }

    // After sending the request, transition to the state that will
    // check if a delta was received in the response.
    transitionTo(PostingState::PostingState_GotShadowDelta);
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

    // 2. Persist schedules from shadow to DeviceSettings NVS
    if (_parentStateMachine) {
        _parentStateMachine->updateSchedulesFromShadow(_shadowState);
    }

    // 3. Post the updated state back to the shadow's "reported" section.
    //    This confirms to the cloud that we have accepted and applied the changes.
    if (_awsIotManager->UpdateShadowWithStatus(_shadowState))
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
        _awsIotManager->UpdateShadowWithStatus(_shadowState);
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

void PostingStateMachine::PostingStateDoPostsFromQueue()
{
    ESP_LOGI(TAG, "Current State: PostingStateDoPostsFromQueue");

    bool anyPostSucceeded = false;

    // Populate telemetry context once (device info doesn't change between posts)
    TelemetryContext telemetryContext;
    populateTelemetryContext(telemetryContext);

    while (!_parentStateMachine->isPostQueueEmpty())
    {
        const LogiSensorData& data = _parentStateMachine->peekPostQueue();
        ESP_LOGI(TAG, "Attempting to post next item from queue (LOGI4 format)...");

        if (_awsIotManager->PostTelemetryLogi4Format(data, telemetryContext))
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

    // If at least one post succeeded, enter dwell state to wait for cloud commands
    if (anyPostSucceeded)
    {
        _postSuccessful = true;
        _dwellStartTime = _timeKeeper->GetCurrentTime();
        ESP_LOGI(TAG, "Post successful, entering post dwell state for %lu seconds",
                 (unsigned long)getPostDwellTimeSeconds());
        transitionTo(PostingState::PostingState_PostDwell);
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
    if (now > 0)
    {
        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);
        strftime(ctx.dateTimeIso, sizeof(ctx.dateTimeIso), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
        ctx.dateTimeIsoValid = true;
    }

    // === MQTT Schema Version ===
    snprintf(ctx.mqttSchema, sizeof(ctx.mqttSchema), "%d.%d.%d",
             CONFIG_LOGI_MQTT_VERSION_MAJOR,
             CONFIG_LOGI_MQTT_VERSION_MINOR,
             CONFIG_LOGI_MQTT_VERSION_REVISION);
    ctx.mqttSchemaValid = true;

    // === Device Version (HW:x.x.x,SW:x.x.x) ===
    snprintf(ctx.deviceVersion, sizeof(ctx.deviceVersion), "HW:%d.%d.%d,SW:%d.%d.%d",
             CONFIG_LOGI_HARDWARE_VERSION_MAJOR,
             CONFIG_LOGI_HARDWARE_VERSION_MINOR,
             CONFIG_LOGI_HARDWARE_VERSION_REVISION,
             CONFIG_LOGI_SOFTWARE_VERSION_MAJOR,
             CONFIG_LOGI_SOFTWARE_VERSION_MINOR,
             CONFIG_LOGI_SOFTWARE_VERSION_REVISION);
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

    // === Charger Status (from sensor data battery level as proxy) ===
    // 0 = not charging, 1 = charging - we can infer from solar voltage
    ctx.chargerStatus = (_sensorData.SolarVoltage > 1.0f) ? 1 : 0;
    ctx.chargerStatusValid = true;

    // === BLE Status (not implemented in WiFi variant) ===
    ctx.bleStatus = 0;
    ctx.bleStatusValid = true;

    // === Device Status (0 = normal operation) ===
    ctx.deviceStatus = 0;
    ctx.deviceStatusValid = true;

    // === Error Log (empty for now) ===
    ctx.errorLog[0] = '\0';
    ctx.errorLogValid = false;

    // === Reset Counter (using boot count from RTC memory if available) ===
    // For now, set to 0 - would need RTC_DATA_ATTR counter for accurate tracking
    ctx.resetCounter = 0;
    ctx.resetCounterValid = true;

    // === Configuration Echo ===
    // Posting schedule - convert WeeklySchedule array to JSON string
    WeeklySchedule schedules[DeviceSettings::MAX_WEEKLY_SCHEDULES];
    _deviceSettings.getWeeklySchedules(schedules);

    // Build JSON array string for posting schedule
    // Format: "HH:MM;XX" where XX is day bitmask with bit 7 as enable flag
    char* schedPtr = ctx.postingSchedule;
    size_t schedRemaining = sizeof(ctx.postingSchedule);
    int written = snprintf(schedPtr, schedRemaining, "[");
    schedPtr += written;
    schedRemaining -= written;

    bool first = true;
    for (size_t i = 0; i < DeviceSettings::MAX_WEEKLY_SCHEDULES && schedRemaining > 20; i++)
    {
        // Schedule is enabled if any day is set in DaysOfWeek
        if (schedules[i].DaysOfWeek != 0)
        {
            // Add enable bit (bit 7) to match LOGI4 format
            uint8_t dayByteWithEnable = schedules[i].DaysOfWeek | 0x80;
            written = snprintf(schedPtr, schedRemaining, "%s\"%02d:%02d;%02X\"",
                              first ? "" : ",",
                              schedules[i].StartTime.Hour,
                              schedules[i].StartTime.Minute,
                              dayByteWithEnable);
            schedPtr += written;
            schedRemaining -= written;
            first = false;
        }
    }
    snprintf(schedPtr, schedRemaining, "]");
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
}
