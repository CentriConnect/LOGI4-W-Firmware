#include "ApplicationStateMachine.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_sleep.h"
#include "esp_system.h"  // REQ-PROV-01: esp_reset_reason()
#include "esp_rom_sys.h"  // DIAG: esp_rom_get_reset_reason() — raw HW value
#include "soc/reset_reasons.h"  // DIAG: RESET_REASON_* enum names
#include "MQTT/ShadowParser.h"
#include "MQTT/AwsIotConfig.h"

#include "WakeStateMachine.h"
#include "ProvisioningStateMachine.h"

// RTC memory for auth failure tracking (persists through deep sleep, reset on power-on)
RTC_DATA_ATTR static uint8_t s_authFailureCount = 0;
static constexpr uint8_t AUTH_FAILURE_THRESHOLD = 10;
#include "DataSampleStateMachine.h"
#include "CheckFillDetectStateMachine.h"
#include "ScheduleCheckStateMachine.h"
#include "PostingStateMachine.h"
#include "logi/EspLogiHardwareFactory.h"
#include "logi/ResetCounter.h"

static const char *TAG = "ApplicationStateMachine";
static const char *NVS_LAST_FUEL_KEY = "lastFuelLvl";

static const uint64_t MINUTE_INTERVAL_US = 60 * 1000 * 1000ULL; 
static const uint64_t SLEEP_OFFSET_US = 100 * 1000ULL;

static uint64_t microseconds_to_next_minute(time_t now)
{
    if (now == 0)
    {
        ESP_LOGW(TAG, "Time not synced, sleeping for default minute interval.");
        return MINUTE_INTERVAL_US; // Sleep for a full minute if time is unknown
    }
    
    uint32_t seconds_past_minute = now % 60;
    uint64_t sleep_s = (60 - seconds_past_minute);
    
    // Ensure we don't sleep for 0 seconds if we are exactly on the minute
    if (sleep_s == 0)
    {
        sleep_s = 60;
    }
    
    uint64_t sleep_us = sleep_s * 1000 * 1000ULL + SLEEP_OFFSET_US;
    ESP_LOGD(TAG, "Calculating sleep: now=%ld, secs_past_min=%lu, sleep_s=%llu, sleep_us=%llu",
             (long)now, seconds_past_minute, sleep_s, sleep_us);
             
    return sleep_us;
}

ApplicationStateMachine::ApplicationStateMachine() :
    _settingsStorage("DeviceCfg"),
    _stateStorage("DeviceState"),
    _settingsService(_settingsStorage),
    _deviceSettings(_settingsService),
    _timeKeeper() 
{
}

ApplicationStateMachine::~ApplicationStateMachine() = default;

bool ApplicationStateMachine::init()
{
    ESP_LOGI(TAG, "Initializing Application State Machine...");

    LogiResetCounter_Init();
    ESP_LOGI(TAG, "Reset counter: %u", static_cast<unsigned int>(LogiResetCounter_Get()));

    if (!_stateStorage.Init()) {
        ESP_LOGE(TAG, "State NVS Storage Initialization Failed!");
        return false;
    }
    ESP_LOGI(TAG, "State NVS Storage Initialized.");

    if (!_settingsStorage.Init()) {
        ESP_LOGE(TAG, "Settings NVS Storage Initialization Failed!");
        return false;
    }
    ESP_LOGI(TAG, "Settings NVS Storage Initialized.");

    if (!_settingsService.Initialize()) {
        ESP_LOGE(TAG, "NvsSettingsService Initialization Failed!");
        return false;
    }
    ESP_LOGI(TAG, "NvsSettingsService Initialized.");

    if (!_deviceSettings.Initialize()) {
        ESP_LOGE(TAG, "DeviceSettings Initialization Failed!");
        return false;
    }
    ESP_LOGI(TAG, "DeviceSettings Initialized.");

    {
        char thingBuf[DeviceSettings::DEVICE_ID_BUFFER_SIZE] = {0};
        if (_deviceSettings.getDeviceId(thingBuf, sizeof(thingBuf)) && _deviceSettings.isDeviceIdValid()) {
            if (!AwsIotConfig_Init(thingBuf)) {
                ESP_LOGE(TAG, "AwsIotConfig_Init rejected Thing UUID '%s'", thingBuf);
                return false;
            }
        } else {
            ESP_LOGW(TAG, "DeviceID not provisioned in NVS — AWS topics deferred until factory provisioning.");
        }
    }

    _logiHardwareDriver.reset(EspLogiHardwareFactory::CreateLogiHardware());
    if (!_logiHardwareDriver) {
        ESP_LOGE(TAG, "Failed to create Logi Hardware Driver via factory.");
        return false;
    }
    if (!_logiHardwareDriver->Initialize()) {
        ESP_LOGE(TAG, "LogiHardwareDriver Initialization Failed!");
        return false;
    }
    ESP_LOGI(TAG, "Logi Hardware Driver Initialized.");

    _powerManager = std::make_unique<EspPowerManager>();
    ESP_LOGI(TAG, "Power Manager Initialized.");

    _networkManager = std::make_unique<EspNetworkManager>();
    if (!_networkManager->Initialize()) {
        ESP_LOGE(TAG, "Network Manager Initialization Failed!");
        return false;
    }
    ESP_LOGI(TAG, "Network Manager Initialized.");

    if (!_awsIotManager.Initialize())
    {
        // Non-fatal: a board missing factory provisioning (no Thing UUID in NVS
        // or no cert in esp_secure_cert partition) will fail here. We still want
        // it to boot far enough to enter BLE WiFi provisioning so the field
        // user/technician can recover it rather than seeing a dead board.
        ESP_LOGW(TAG, "AWS IoT Manager init failed — board may need factory provisioning. Continuing without cloud.");
    }
    else
    {
        ESP_LOGI(TAG, "AWS IoT Manager Initialized.");
    }

    // Initialize BLE manager - always advertising when awake for manual provisioning trigger
    _bleManager = std::make_unique<EspBluetoothManager>();
    // Note: BLE advertising will be started after WiFi init to get MAC address
    ESP_LOGI(TAG, "Bluetooth Manager created.");

    // Create provisioning state machine (needed for credential checks)
    _provisioningStateMachine = std::make_unique<ProvisioningStateMachine>(
        &_settingsStorage,
        _bleManager.get(),
        _logiHardwareDriver.get(),
        &_awsIotManager,
        _powerManager.get(),
        &_timeKeeper,
        &_deviceSettings  // REQ-BLE-01
    );
    ESP_LOGI(TAG, "Provisioning State Machine created.");

    // REQ-PROV-01: true power-on reset wipes Wi-Fi credentials, forcing
    // re-provisioning. Uses esp_reset_reason() == ESP_RST_POWERON. The C6
    // USB-Serial-JTAG peripheral CAN self-trigger and mask POWERON as USB
    // (raw 0x15) during VDD ramp — BUT bench-verified 2026-06-02 this only
    // happens with an active USB host present. In field deployment (no host,
    // and the dev cable just plugged in the board side without a PC = no host)
    // POWERON latches cleanly. As defense in depth, USB self-reset is disabled
    // in EspPlatform::InitializeSystem (d432e33).
    esp_reset_reason_t reset_reason = esp_reset_reason();
    int raw_hw_reason = (int)esp_rom_get_reset_reason(0);  // DIAG: raw HW value
    if (reset_reason == ESP_RST_POWERON) {
        ESP_LOGW(TAG, "Power-on reset (esp_reset_reason=%d, raw HW=0x%02X) - wiping NVS Wi-Fi credentials (REQ-PROV-01)", reset_reason, raw_hw_reason);
        _provisioningStateMachine->wipeAllCredentials();
    } else {
        ESP_LOGI(TAG, "Non-POWERON reset (esp_reset_reason=%d, raw HW=0x%02X) - preserving NVS credentials", reset_reason, raw_hw_reason);
    }

    // Now that dependencies are ready, create the child state machines
    _wakeStateMachine = std::make_unique<WakeStateMachine>(_logiHardwareDriver.get(), _powerManager.get(), &_timeKeeper);
    _dataSampleStateMachine = std::make_unique<DataSampleStateMachine>(_logiHardwareDriver.get(), _currentSensorData, &_timeKeeper);
    _checkFillDetectStateMachine = std::make_unique<CheckFillDetectStateMachine>(&_timeKeeper, _deviceSettings, _currentSensorData);
    _scheduleCheckStateMachine = std::make_unique<ScheduleCheckStateMachine>(&_timeKeeper, _deviceSettings, _networkManager.get(), _currentSensorData);
    _postingStateMachine = std::make_unique<PostingStateMachine>(&_awsIotManager, &_timeKeeper, _currentSensorData, _deviceSettings);

    // Set parent pointers for communication
    _wakeStateMachine->setParentStateMachine(this);
    _provisioningStateMachine->setParentStateMachine(this);
    _dataSampleStateMachine->setParentStateMachine(this);
    _scheduleCheckStateMachine->setParentStateMachine(this);
    _checkFillDetectStateMachine->setParentStateMachine(this);
    _postingStateMachine->setParentStateMachine(this);

    // Check for stored WiFi credentials
    bool hasCredentials = _provisioningStateMachine->hasStoredCredentials();
    bool connect_success = false;

#ifdef CONFIG_LOGI_DEBUG_USE_HARDCODED_WIFI
    if (!hasCredentials) {
        ESP_LOGW(TAG, "DEBUG: No credentials found, using hardcoded WiFi from wifiCredentials.h");
        _provisioningStateMachine->saveCredentialsToNvs(MY_WIFI_SSID, MY_WIFI_PASS);
        hasCredentials = true;
    }
#endif

    if (hasCredentials) {
        // Load credentials from NVS and try to connect
        char ssid[33] = {0};
        char password[65] = {0};

        if (_settingsStorage.LoadString(ProvisioningStateMachine::NVS_WIFI_SSID_KEY, ssid, sizeof(ssid)) &&
            _settingsStorage.LoadString(ProvisioningStateMachine::NVS_WIFI_PASS_KEY, password, sizeof(password))) {

            ESP_LOGI(TAG, "Connecting to Wi-Fi (SSID: %s)...", ssid);
            connect_success = _networkManager->Connect(ssid, password);
        }
    } else {
        ESP_LOGW(TAG, "No stored WiFi credentials found");
    }

    if (connect_success) {
        ESP_LOGI(TAG, "Wi-Fi Connected.");
        // Reset auth failure counter on successful connection
        s_authFailureCount = 0;
    } else {
        ESP_LOGW(TAG, "Wi-Fi Not Connected.");

        // Check failure type and handle re-provisioning triggers
        if (hasCredentials) {
            WifiFailureType failureType = _networkManager->GetLastFailureType();

            switch (failureType) {
                case WifiFailureType::WifiFailure_AuthError:
                    s_authFailureCount++;
                    ESP_LOGW(TAG, "Auth failure count: %d/%d", s_authFailureCount, AUTH_FAILURE_THRESHOLD);
                    if (s_authFailureCount >= AUTH_FAILURE_THRESHOLD) {
                        ESP_LOGW(TAG, "Auth failure threshold reached - entering re-provisioning mode");
                        s_authFailureCount = 0;
                        _provisioningStateMachine->init(ProvisioningMode::ReProvision);
                        currentState = ApplicationState::ApplicationState_Provisioning;
                    }
                    break;

                case WifiFailureType::WifiFailure_ApNotFound:
                    ESP_LOGW(TAG, "AP not found - SSID may have changed, entering re-provisioning mode");
                    s_authFailureCount = 0;
                    _provisioningStateMachine->init(ProvisioningMode::ReProvision);
                    currentState = ApplicationState::ApplicationState_Provisioning;
                    break;

                case WifiFailureType::WifiFailure_Timeout:
                    // Connection timeout - do NOT trigger re-provisioning, just log
                    ESP_LOGW(TAG, "Connection timeout - will retry on next wake");
                    break;

                default:
                    ESP_LOGW(TAG, "Unknown WiFi failure type");
                    break;
            }
        }
    }

    WakeupReason wakeup_reason = _powerManager->GetWakeupReason();
    if (wakeup_reason == WAKEUP_REASON_RESET)
    {
        // If no credentials on first boot, enter provisioning mode
        if (!hasCredentials) {
            ESP_LOGI(TAG, "First boot without credentials - entering provisioning mode");
            _provisioningStateMachine->init(ProvisioningMode::FirstBoot);
            currentState = ApplicationState::ApplicationState_Provisioning;
        }
    }

    // Check for force-provisioning flag (set by shadow reset bool handler)
    if (currentState != ApplicationState::ApplicationState_Provisioning) {
        if (checkForceProvisioningFlag()) {
            ESP_LOGW(TAG, "Force provisioning flag detected — entering provisioning mode");
            _provisioningStateMachine->init(ProvisioningMode::ReProvision);
            currentState = ApplicationState::ApplicationState_Provisioning;
        }
    }

    // BLE is NOT started during normal operation (Req #1)
    // wifi_prov_mgr manages BLE internally during provisioning mode

    ESP_LOGI(TAG, "Application State Machine Initialized Successfully.");
    _isInitialized = true;
    return true;
}

void ApplicationStateMachine::update()
{
    if (!_isInitialized)
    {
        ESP_LOGE(TAG, "Update called before successful initialization!");
        return;
    }

    switch (currentState)
    {
        case ApplicationState::ApplicationState_Wake:
        {
            ESP_LOGI(TAG, "Current State: Wake");
            ApplicationStateWake();
            break;
        }
        case ApplicationState::ApplicationState_Provisioning:
        {
            ESP_LOGI(TAG, "Current State: Provisioning");
            ApplicationStateProvisioning();
            break;
        }
        case ApplicationState::ApplicationState_DataSample:
        {
            ESP_LOGI(TAG, "Current State: Data Sample");
            ApplicationStateDataSample();
            break;
        }
        case ApplicationState::ApplicationState_ScheduleCheck:
        {
            ESP_LOGI(TAG, "Current State: Schedule Check");
            ApplicationStateScheduleCheck();
            break;
        }
        case ApplicationState::ApplicationState_CheckFillDetect:
        {
            ESP_LOGI(TAG, "Current State: Check Fill Detect");
            ApplicationStateCheckFillDetect();
            break;
        }
        case ApplicationState::ApplicationState_Posting:
        {
            ESP_LOGI(TAG, "Current State: Posting");
            ApplicationStatePosting();
            break;
        }
        case ApplicationState::ApplicationState_Sleep:
        {
            ESP_LOGI(TAG, "Current State: Sleep");
            ApplicationStateSleep();
            break;
        }
    }
}

void ApplicationStateMachine::transitionTo(ApplicationState newState)
{
    currentState = newState;
}

bool ApplicationStateMachine::isPostQueueEmpty() const
{
    return _postQueueCount == 0;
}

bool ApplicationStateMachine::addToPostQueue(const LogiSensorData& data, PostTransport transport)
{
    if (_postQueueCount >= POST_QUEUE_SIZE) {
        ESP_LOGE(TAG, "Post queue is full. Cannot add new data.");
        return false;
    }
    _postQueue[_postQueueTail] = data;
    _postQueueTransport[_postQueueTail] = transport;
    _postQueueTail = (_postQueueTail + 1) % POST_QUEUE_SIZE;
    _postQueueCount++;
    ESP_LOGI(TAG, "Added sensor data to post queue (%s). Count: %d",
             transport == PostTransport::PostTransport_Udp ? "UDP" : "MQTT",
             _postQueueCount);
    return true;
}

const LogiSensorData& ApplicationStateMachine::peekPostQueue() const
{
    // This should only be called after checking isPostQueueEmpty()
    return _postQueue[_postQueueHead];
}

PostTransport ApplicationStateMachine::peekPostQueueTransport() const
{
    // This should only be called after checking isPostQueueEmpty()
    return _postQueueTransport[_postQueueHead];
}

void ApplicationStateMachine::removeFirstFromPostQueue()
{
    if (isPostQueueEmpty()) {
        return;
    }
    _postQueueHead = (_postQueueHead + 1) % POST_QUEUE_SIZE;
    _postQueueCount--;
    ESP_LOGI(TAG, "Removed sensor data from post queue. Count: %d", _postQueueCount);
}

void ApplicationStateMachine::ApplicationStateWake()
{
    _wakeStateMachine->update();
}

void ApplicationStateMachine::ApplicationStateProvisioning()
{
    if (_provisioningStateMachine) {
        _provisioningStateMachine->update();
    }
}

void ApplicationStateMachine::enterProvisioningMode(ProvisioningMode mode)
{
    ESP_LOGI(TAG, "Entering provisioning mode: %s",
             mode == ProvisioningMode::FirstBoot ? "FirstBoot" : "ReProvision");

    if (!_provisioningStateMachine) {
        _provisioningStateMachine = std::make_unique<ProvisioningStateMachine>(
            &_settingsStorage,
            _bleManager.get(),
            _logiHardwareDriver.get(),
            &_awsIotManager,
            _powerManager.get(),
            &_timeKeeper,
            &_deviceSettings  // REQ-BLE-01
        );
    }

    // Initialize and start provisioning
    if (_provisioningStateMachine->init(mode)) {
        _provisioningStateMachine->setParentStateMachine(this);
        transitionTo(ApplicationState::ApplicationState_Provisioning);
    } else {
        ESP_LOGE(TAG, "Failed to initialize provisioning state machine");
        // Fall back to sleep on failure
        transitionTo(ApplicationState::ApplicationState_Sleep);
    }
}

bool ApplicationStateMachine::checkAndConnectWifi()
{
    // Check if we have stored credentials
    if (!_provisioningStateMachine) {
        _provisioningStateMachine = std::make_unique<ProvisioningStateMachine>(
            &_settingsStorage,
            _bleManager.get(),
            _logiHardwareDriver.get(),
            &_awsIotManager,
            _powerManager.get(),
            &_timeKeeper,
            &_deviceSettings  // REQ-BLE-01
        );
    }

    if (!_provisioningStateMachine->hasStoredCredentials()) {
        ESP_LOGW(TAG, "No stored WiFi credentials - entering first boot provisioning");
        enterProvisioningMode(ProvisioningMode::FirstBoot);
        return false;
    }

    // Load credentials from NVS and try to connect
    char ssid[33] = {0};
    char password[65] = {0};

    if (_settingsStorage.LoadString(ProvisioningStateMachine::NVS_WIFI_SSID_KEY, ssid, sizeof(ssid)) &&
        _settingsStorage.LoadString(ProvisioningStateMachine::NVS_WIFI_PASS_KEY, password, sizeof(password))) {

        ESP_LOGI(TAG, "Connecting to WiFi (SSID: %s)...", ssid);
        bool connected = _networkManager->Connect(ssid, password);

        if (connected) {
            ESP_LOGI(TAG, "WiFi connected successfully");
            return true;
        } else {
            ESP_LOGW(TAG, "WiFi connection failed");
            // TODO: Track failure type and count for re-provisioning logic
            return false;
        }
    }

    ESP_LOGW(TAG, "Failed to load WiFi credentials from NVS - entering first boot provisioning");
    enterProvisioningMode(ProvisioningMode::FirstBoot);
    return false;
}

bool ApplicationStateMachine::checkForceProvisioningFlag()
{
    char flag[4] = {0};
    if (_stateStorage.LoadString("force_prov", flag, sizeof(flag)) && strcmp(flag, "1") == 0) {
        // Clear the flag so we don't loop
        _stateStorage.SaveString("force_prov", "0");
        _stateStorage.Commit();
        return true;
    }
    return false;
}

void ApplicationStateMachine::ApplicationStateDataSample()
{
    _dataSampleStateMachine->update();   
}

void ApplicationStateMachine::ApplicationStateScheduleCheck()
{
    _scheduleCheckStateMachine->update();
}

void ApplicationStateMachine::ApplicationStateCheckFillDetect()
{   
    _checkFillDetectStateMachine->update();
}

void ApplicationStateMachine::ApplicationStatePosting()
{
    _postingStateMachine->update();
}

void ApplicationStateMachine::ApplicationStateSleep()
{
    time_t current_time = _timeKeeper.GetCurrentTime();
    uint64_t sleep_duration_us = microseconds_to_next_minute(current_time);

    ESP_LOGI(TAG, "All tasks complete. Entering deep sleep for %llu us...", sleep_duration_us);

    // Ensure LED is off before entering deep sleep
    if (_logiHardwareDriver)
    {
        _logiHardwareDriver->SetLedState(LedState::LedState_Off);
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    // Stop BLE before deep sleep (defensive - BLE should only run during provisioning)
    if (_bleManager && _bleManager->isInitialized())
    {
        _bleManager->stopAdvertising();
        _bleManager->deinit();
    }

    _networkManager->Disconnect();
    _powerManager->Sleep(sleep_duration_us);

    ESP_LOGE(TAG, "!!! Critical Error: Exited Sleep call unexpectedly !!!");
}

bool ApplicationStateMachine::updateSchedulesFromShadow(const DeviceShadowState& shadowState)
{
    WeeklySchedule schedules[DeviceSettings::MAX_WEEKLY_SCHEDULES];

    int validCount = ConvertShadowSchedulesToWeekly(shadowState, schedules);

    if (validCount == 0) {
        ESP_LOGW(TAG, "No valid schedules in shadow, keeping existing schedules");
        return false;
    }

    if (_deviceSettings.setWeeklySchedules(schedules)) {
        ESP_LOGI(TAG, "Successfully persisted %d schedules from shadow to NVS", validCount);
        return true;
    } else {
        ESP_LOGE(TAG, "Failed to persist schedules to NVS");
        return false;
    }
}
