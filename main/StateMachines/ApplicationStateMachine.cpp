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
RTC_DATA_ATTR static int64_t s_wifiOutageStartTime = 0;
RTC_DATA_ATTR static bool s_wifiRepairAdvertisingRequested = false;
RTC_DATA_ATTR static bool s_repairBleConnectionRequested = false;
static constexpr uint8_t AUTH_FAILURE_THRESHOLD = 10;
static constexpr int64_t WIFI_REPAIR_ADVERTISING_DELAY_S = 24LL * 60LL * 60LL;
static constexpr uint32_t WIFI_REPAIR_BLE_ADV_INTERVAL_MS = 8000;
#include "DataSampleStateMachine.h"
#include "CheckFillDetectStateMachine.h"
#include "ScheduleCheckStateMachine.h"
#include "PostingStateMachine.h"
#include "logi/EspLogiHardwareFactory.h"
#include "logi/ResetCounter.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdlib>

static const char *TAG = "ApplicationStateMachine";
static const char *NVS_LAST_FUEL_KEY = "lastFuelLvl";

static const uint64_t SECOND_INTERVAL_US = 1000 * 1000ULL;
static const uint64_t SLEEP_OFFSET_US = 100 * 1000ULL;
static constexpr uint32_t MAX_SLEEP_MINUTES = DeviceSettings::MAX_SENSOR_SAMPLE_RATE_MIN;
RTC_DATA_ATTR static int64_t s_lastSensorSampleTime = 0;

static bool parseScheduleString(const char* schedule, int& hour, int& minute, uint8_t& daysOfWeek)
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
    return daysOfWeek != 0;
}

static int64_t secondsUntilWeeklyTime(time_t now, int hour, int minute, uint8_t daysOfWeek)
{
    struct tm timeInfo{};
    if (now <= 0 || localtime_r(&now, &timeInfo) == nullptr)
    {
        return -1;
    }

    const int currentWday = timeInfo.tm_wday;
    const int currentSecondsOfDay = (timeInfo.tm_hour * 3600) + (timeInfo.tm_min * 60) + timeInfo.tm_sec;
    const int scheduledSecondsOfDay = (hour * 3600) + (minute * 60);

    int64_t best = -1;
    for (int dayOffset = 0; dayOffset < 7; ++dayOffset)
    {
        int candidateWday = (currentWday + dayOffset) % 7;
        if (!(daysOfWeek & (1 << candidateWday)))
        {
            continue;
        }

        int64_t delta = static_cast<int64_t>(dayOffset) * 86400LL +
                        scheduledSecondsOfDay -
                        currentSecondsOfDay;
        if (delta <= 0)
        {
            delta += 7LL * 86400LL;
        }

        if (best < 0 || delta < best)
        {
            best = delta;
        }
    }

    return best;
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
    _bleManager->setConnectionCallback([this]() { this->onRepairBleConnection(); });
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
    _postingStateMachine = std::make_unique<PostingStateMachine>(&_awsIotManager, &_timeKeeper, _logiHardwareDriver.get(), _currentSensorData, _deviceSettings);

    // Set parent pointers for communication
    _wakeStateMachine->setParentStateMachine(this);
    _provisioningStateMachine->setParentStateMachine(this);
    _dataSampleStateMachine->setParentStateMachine(this);
    _scheduleCheckStateMachine->setParentStateMachine(this);
    _checkFillDetectStateMachine->setParentStateMachine(this);
    _postingStateMachine->setParentStateMachine(this);

    // Check for stored WiFi credentials
    bool hasCredentials = _provisioningStateMachine->hasStoredCredentials();

#ifdef CONFIG_LOGI_DEBUG_USE_HARDCODED_WIFI
    if (!hasCredentials) {
        ESP_LOGW(TAG, "DEBUG: No credentials found, using hardcoded WiFi from wifiCredentials.h");
        _provisioningStateMachine->saveCredentialsToNvs(MY_WIFI_SSID, MY_WIFI_PASS);
        hasCredentials = true;
    }
#endif

    WakeupReason wakeup_reason = _powerManager->GetWakeupReason();
    if (wakeup_reason == WAKEUP_REASON_RESET)
    {
        // If no credentials on first boot, enter provisioning mode
        if (!hasCredentials) {
            ESP_LOGI(TAG, "First boot without credentials - entering provisioning mode");
            _provisioningStateMachine->init(ProvisioningMode::FirstBoot);
            currentState = ApplicationState::ApplicationState_Provisioning;
        } else {
            ESP_LOGI(TAG, "Stored Wi-Fi credentials found; deferring Wi-Fi connection until a post is due");
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

    if (s_repairBleConnectionRequested &&
        currentState != ApplicationState::ApplicationState_Provisioning)
    {
        s_repairBleConnectionRequested = false;
        ESP_LOGI(TAG, "Repair BLE connection request observed in app loop");
        stopWifiRepairAdvertising();
        enterProvisioningMode(ProvisioningMode::ReProvision);
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

void ApplicationStateMachine::recordSensorSampleTime(time_t sampleTime)
{
    if (sampleTime > 0)
    {
        s_lastSensorSampleTime = static_cast<int64_t>(sampleTime);
        ESP_LOGD(TAG, "Recorded sensor sample time: %lld", s_lastSensorSampleTime);
    }
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

    stopWifiRepairAdvertising();

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

void ApplicationStateMachine::onRepairBleConnection()
{
    ESP_LOGI(TAG, "Repair BLE connection received - requesting re-provisioning");
    s_repairBleConnectionRequested = true;
}

void ApplicationStateMachine::buildBleServiceName(char *serviceName, size_t maxLen) const
{
    if (serviceName == nullptr || maxLen == 0)
    {
        return;
    }

    if (_deviceSettings.isDeviceIdValid())
    {
        char uuid[DeviceSettings::DEVICE_ID_BUFFER_SIZE] = {0};
        if (_deviceSettings.getDeviceId(uuid, sizeof(uuid)))
        {
            char prefix[5] = {0};
            strncpy(prefix, uuid, 4);
            snprintf(serviceName, maxLen, "MyPropane-%s", prefix);
            return;
        }
    }

    snprintf(serviceName, maxLen, "MyPropane");
}

void ApplicationStateMachine::markWifiOutageStarted(time_t now)
{
    if (s_wifiOutageStartTime == 0 && now > 0)
    {
        s_wifiOutageStartTime = static_cast<int64_t>(now);
        ESP_LOGW(TAG, "Wi-Fi outage timer started at %lld", s_wifiOutageStartTime);
    }
}

void ApplicationStateMachine::clearWifiOutage()
{
    if (s_wifiOutageStartTime != 0 || s_wifiRepairAdvertisingRequested)
    {
        ESP_LOGI(TAG, "Wi-Fi recovered; clearing outage timer and repair advertising");
    }
    s_wifiOutageStartTime = 0;
    s_wifiRepairAdvertisingRequested = false;
    stopWifiRepairAdvertising();
}

bool ApplicationStateMachine::isWifiOutageRepairDue(time_t now) const
{
    return now > 0 &&
           s_wifiOutageStartTime > 0 &&
           (static_cast<int64_t>(now) - s_wifiOutageStartTime) >= WIFI_REPAIR_ADVERTISING_DELAY_S;
}

void ApplicationStateMachine::startWifiRepairAdvertisingIfDue(time_t now)
{
    if (!isWifiOutageRepairDue(now) || !_bleManager)
    {
        return;
    }

    if (!_bleManager->isInitialized())
    {
        _bleManager->init();
        _bleManager->startHost();
    }

    char serviceName[32] = {0};
    buildBleServiceName(serviceName, sizeof(serviceName));

    if (!_bleManager->isAdvertising())
    {
        ESP_LOGW(TAG, "Wi-Fi outage >24h; starting connectable BLE repair advertising (%s, %lu ms)",
                 serviceName,
                 static_cast<unsigned long>(WIFI_REPAIR_BLE_ADV_INTERVAL_MS));
        _bleManager->startAdvertising(serviceName, WIFI_REPAIR_BLE_ADV_INTERVAL_MS);
    }

    s_wifiRepairAdvertisingRequested = true;
}

void ApplicationStateMachine::stopWifiRepairAdvertising()
{
    if (_bleManager && _bleManager->isInitialized())
    {
        _bleManager->stopAdvertising();
        _bleManager->deinit();
    }
    s_wifiRepairAdvertisingRequested = false;
}

void ApplicationStateMachine::waitAwakeWithRepairAdvertising(uint64_t durationUs)
{
    if (_powerManager)
    {
        _powerManager->AllowLightSleep(true);
    }

    const uint64_t sliceMs = 1000;
    uint64_t remainingMs = durationUs / 1000ULL;
    if (remainingMs == 0)
    {
        remainingMs = 1;
    }

    ESP_LOGW(TAG, "Wi-Fi repair advertising active; staying awake/light-sleeping for %llu ms",
             remainingMs);

    while (remainingMs > 0)
    {
        if (s_repairBleConnectionRequested)
        {
            ESP_LOGI(TAG, "Repair BLE connection requested during awake wait");
            break;
        }

        uint64_t delayMs = remainingMs < sliceMs ? remainingMs : sliceMs;
        vTaskDelay(pdMS_TO_TICKS(delayMs));
        remainingMs -= delayMs;
    }

    if (_powerManager)
    {
        _powerManager->AllowLightSleep(false);
    }
}

void ApplicationStateMachine::resetDutyCycleStateMachines()
{
    if (_dataSampleStateMachine)
    {
        _dataSampleStateMachine->transitionTo(DataSampleState::DataSampleState_SampleSensorData);
    }
    if (_scheduleCheckStateMachine)
    {
        _scheduleCheckStateMachine->transitionTo(ScheduleCheckState::ScheduleCheckState_ValidateDateTime);
    }
    if (_checkFillDetectStateMachine)
    {
        _checkFillDetectStateMachine->transitionTo(CheckFillDetectState::CheckFillDetectState_CheckInDwellMode);
    }
    if (_postingStateMachine)
    {
        _postingStateMachine->transitionTo(PostingState::PostingState_InitialEnter);
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
        time_t now = _timeKeeper.GetCurrentTime();
        startWifiRepairAdvertisingIfDue(now);
        bool connected = _networkManager->Connect(ssid, password);

        if (connected) {
            ESP_LOGI(TAG, "WiFi connected successfully");
            s_authFailureCount = 0;
            clearWifiOutage();
            return true;
        } else {
            ESP_LOGW(TAG, "WiFi connection failed");
            markWifiOutageStarted(now);
            WifiFailureType failureType = _networkManager->GetLastFailureType();
            switch (failureType) {
                case WifiFailureType::WifiFailure_AuthError:
                    s_authFailureCount++;
                    ESP_LOGW(TAG, "Auth failure count: %d/%d", s_authFailureCount, AUTH_FAILURE_THRESHOLD);
                    if (s_authFailureCount >= AUTH_FAILURE_THRESHOLD) {
                        ESP_LOGW(TAG, "Auth failure threshold reached - entering re-provisioning mode");
                        s_authFailureCount = 0;
                        enterProvisioningMode(ProvisioningMode::ReProvision);
                    }
                    break;

                case WifiFailureType::WifiFailure_ApNotFound:
                    ESP_LOGW(TAG, "AP not found - preserving credentials and retrying on future post windows");
                    s_authFailureCount = 0;
                    startWifiRepairAdvertisingIfDue(_timeKeeper.GetCurrentTime());
                    break;

                case WifiFailureType::WifiFailure_Timeout:
                    ESP_LOGW(TAG, "Connection timeout - will retry on next scheduled post");
                    startWifiRepairAdvertisingIfDue(_timeKeeper.GetCurrentTime());
                    break;

                default:
                    ESP_LOGW(TAG, "Unknown WiFi failure type");
                    startWifiRepairAdvertisingIfDue(_timeKeeper.GetCurrentTime());
                    break;
            }
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
    uint64_t sleep_duration_us = calculateSleepDurationFrom(current_time);
    const bool repairAdvertisingDue = isWifiOutageRepairDue(current_time);

    ESP_LOGI(TAG, "All tasks complete. %s for %llu us...",
             repairAdvertisingDue ? "Staying discoverable in repair BLE mode" : "Entering deep sleep",
             sleep_duration_us);

    // Ensure LED is off before sleeping or repair wait
    if (_logiHardwareDriver)
    {
        _logiHardwareDriver->SetLedState(LedState::LedState_Off);
    }

    vTaskDelay(pdMS_TO_TICKS(100));

    if (repairAdvertisingDue)
    {
        _networkManager->Disconnect();
        startWifiRepairAdvertisingIfDue(current_time);
        waitAwakeWithRepairAdvertising(sleep_duration_us);

        if (s_repairBleConnectionRequested)
        {
            s_repairBleConnectionRequested = false;
            stopWifiRepairAdvertising();
            enterProvisioningMode(ProvisioningMode::ReProvision);
            return;
        }

        resetDutyCycleStateMachines();
        transitionTo(ApplicationState::ApplicationState_Wake);
        return;
    }

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

uint64_t ApplicationStateMachine::calculateSleepDuration()
{
    return calculateSleepDurationFrom(_timeKeeper.GetCurrentTime());
}

uint64_t ApplicationStateMachine::calculateSleepDurationFrom(time_t now)
{
    const uint32_t sampleRateMin = _deviceSettings.getSensorSampleRateMinutes();
    int64_t sleepSeconds = static_cast<int64_t>(sampleRateMin) * 60LL;

    if (now > 0 && s_lastSensorSampleTime > 0)
    {
        int64_t nextSample = s_lastSensorSampleTime + sleepSeconds;
        int64_t untilSample = nextSample - static_cast<int64_t>(now);
        if (untilSample <= 0)
        {
            untilSample = 1;
        }
        sleepSeconds = untilSample;
    }

    const bool fillDwellActive = _checkFillDetectStateMachine && _checkFillDetectStateMachine->isInDwell();
    if (fillDwellActive)
    {
        const int64_t dwellDeadline = _checkFillDetectStateMachine->getDwellDeadline();
        if (now > 0 && dwellDeadline > 0)
        {
            int64_t untilDwellDeadline = dwellDeadline - static_cast<int64_t>(now);
            if (untilDwellDeadline <= 0)
            {
                untilDwellDeadline = 1;
            }
            sleepSeconds = std::min(sleepSeconds, untilDwellDeadline);
        }

        ESP_LOGI(TAG, "Sleep plan: fill dwell active, sleeping %lld seconds (sample_rate=%lu min)",
                 sleepSeconds,
                 static_cast<unsigned long>(sampleRateMin));
        return static_cast<uint64_t>(sleepSeconds) * SECOND_INTERVAL_US + SLEEP_OFFSET_US;
    }

    if (now > 0 && _timeKeeper.IsTimeSynced())
    {
        int64_t nextScheduledPostSeconds = -1;

        WeeklySchedule schedules[DeviceSettings::MAX_WEEKLY_SCHEDULES];
        if (!_deviceSettings.getEventPosts() && _deviceSettings.getWeeklySchedules(schedules))
        {
            for (int i = 0; i < DeviceSettings::MAX_WEEKLY_SCHEDULES; ++i)
            {
                if (schedules[i].DaysOfWeek == 0)
                {
                    continue;
                }

                int64_t delta = secondsUntilWeeklyTime(now,
                                                       schedules[i].StartTime.Hour,
                                                       schedules[i].StartTime.Minute,
                                                       schedules[i].DaysOfWeek);
                if (delta > 0 && (nextScheduledPostSeconds < 0 || delta < nextScheduledPostSeconds))
                {
                    nextScheduledPostSeconds = delta;
                }
            }
        }

        char mqttSchedule[DeviceSettings::MQTT_SCHEDULED_POST_BUFFER_SIZE] = {0};
        int mqttHour = 0;
        int mqttMinute = 0;
        uint8_t mqttDays = 0;
        if (_deviceSettings.getMqttScheduledPost(mqttSchedule, sizeof(mqttSchedule)) &&
            parseScheduleString(mqttSchedule, mqttHour, mqttMinute, mqttDays))
        {
            const uint8_t allDaysMask = 0x7F;
            uint8_t derivedUdpDays = static_cast<uint8_t>((~mqttDays) & allDaysMask);
            uint8_t mqttSplitScheduleDays = static_cast<uint8_t>((mqttDays | derivedUdpDays) & allDaysMask);
            int64_t delta = secondsUntilWeeklyTime(now, mqttHour, mqttMinute, mqttSplitScheduleDays);
            if (delta > 0 && (nextScheduledPostSeconds < 0 || delta < nextScheduledPostSeconds))
            {
                nextScheduledPostSeconds = delta;
            }
        }

        if (nextScheduledPostSeconds > 0)
        {
            sleepSeconds = std::min(sleepSeconds, nextScheduledPostSeconds);
        }
    }
    else
    {
        ESP_LOGW(TAG, "Time not synced; using sensor_sample_rate sleep only (%lu minutes)",
                 static_cast<unsigned long>(sampleRateMin));
    }

    const int64_t maxSleepSeconds = static_cast<int64_t>(MAX_SLEEP_MINUTES) * 60LL;
    if (sleepSeconds > maxSleepSeconds)
    {
        sleepSeconds = maxSleepSeconds;
    }
    if (sleepSeconds <= 0)
    {
        sleepSeconds = 1;
    }

    ESP_LOGI(TAG, "Sleep plan: sleeping %lld seconds (sample_rate=%lu min)",
             sleepSeconds,
             static_cast<unsigned long>(sampleRateMin));

    return static_cast<uint64_t>(sleepSeconds) * SECOND_INTERVAL_US + SLEEP_OFFSET_US;
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
