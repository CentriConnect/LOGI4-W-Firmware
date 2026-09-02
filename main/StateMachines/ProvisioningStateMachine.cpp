#include "ProvisioningStateMachine.h"
#include "ApplicationStateMachine.h"

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <esp_sleep.h>
#include <wifi_provisioning/scheme_ble.h>
#include <ctime>
#include <cstdio>
#include <cstring>
#include "logi/ResetCounter.h"
#include "logi/Faults.h"
#include "MQTT/AwsIotConfig.h"
#include "UDP/UdpTelemetryClient.h"

ESP_EVENT_DECLARE_BASE(LOGI_PROV_TIMEOUT_EVENT);
ESP_EVENT_DEFINE_BASE(LOGI_PROV_TIMEOUT_EVENT);

enum {
    LOGI_PROV_TIMEOUT_REACHED = 0
};

const char* ProvisioningStateMachine::TAG = "ProvisioningSM";
const char* ProvisioningStateMachine::NVS_WIFI_SSID_KEY = "wifi_ssid";
const char* ProvisioningStateMachine::NVS_WIFI_PASS_KEY = "wifi_pass";
const char* ProvisioningStateMachine::NVS_BACKUP_SSID_KEY = "backup_ssid";
const char* ProvisioningStateMachine::NVS_BACKUP_PASS_KEY = "backup_pass";
static constexpr const char* LOGI_PROVISIONING_POP = "F10974B8";
static constexpr uint32_t PROVISIONING_SUCCESS_BLE_GRACE_MS = 3000;
static constexpr uint16_t LOGI_BLE_COMPANY_ID = 0xFFFF; // Internal/test manufacturer ID.
static constexpr size_t LOGI_EOL_ADV_PAYLOAD_LEN = 25;
static constexpr size_t LOGI_EOL_SCAN_RESPONSE_PAYLOAD_LEN = 14;
static constexpr size_t LOGI_EOL_DEVICE_ID_PREFIX_BYTES = 4;
static constexpr float LOGI_EOL_TEMP_MIN_C = -40.0f;
static constexpr float LOGI_EOL_TEMP_MAX_C = 85.0f;
static bool s_eolBleBurstRanThisBoot = false;

enum EolBleFlags : uint8_t {
    EOL_FLAG_DEVICE_ID_VALID = 1u << 0,
    EOL_FLAG_BATTERY_VALID   = 1u << 1,
    EOL_FLAG_SOLAR_VALID     = 1u << 2,
    EOL_FLAG_SENSOR_VALID    = 1u << 3,
    EOL_FLAG_SUPPLY_VALID    = 1u << 4,
    EOL_FLAG_FUEL_VALID      = 1u << 5,
    EOL_FLAG_DEVICE_ID_PREFIX_ENCODED = 1u << 6,
    EOL_FLAG_TEMPERATURE_VALID = 1u << 7,
};

static void appendU8(uint8_t* buffer, size_t capacity, size_t& len, uint8_t value)
{
    if (len < capacity) {
        buffer[len++] = value;
    }
}

static void appendU16Le(uint8_t* buffer, size_t capacity, size_t& len, uint16_t value)
{
    appendU8(buffer, capacity, len, static_cast<uint8_t>(value & 0xFFu));
    appendU8(buffer, capacity, len, static_cast<uint8_t>((value >> 8) & 0xFFu));
}

static void appendU32Le(uint8_t* buffer, size_t capacity, size_t& len, uint32_t value)
{
    appendU16Le(buffer, capacity, len, static_cast<uint16_t>(value & 0xFFFFu));
    appendU16Le(buffer, capacity, len, static_cast<uint16_t>((value >> 16) & 0xFFFFu));
}

static uint16_t voltsToMillivolts(float volts)
{
    if (volts <= 0.0f) {
        return 0;
    }

    float mv = (volts * 1000.0f) + 0.5f;
    if (mv > 65535.0f) {
        return 65535;
    }
    return static_cast<uint16_t>(mv);
}

static uint8_t temperatureToInt8C(float temperatureC)
{
    float clamped = temperatureC;
    if (clamped < -128.0f) {
        clamped = -128.0f;
    } else if (clamped > 127.0f) {
        clamped = 127.0f;
    }

    const int rounded = static_cast<int>(clamped >= 0.0f ? clamped + 0.5f : clamped - 0.5f);
    return static_cast<uint8_t>(static_cast<int8_t>(rounded));
}

static uint32_t fnv1a32(const char* value)
{
    uint32_t hash = 2166136261u;
    if (value == nullptr) {
        return hash;
    }
    while (*value != '\0') {
        hash ^= static_cast<uint8_t>(*value++);
        hash *= 16777619u;
    }
    return hash;
}

static int hexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool parseUuidPrefixBytes(const char* uuid, uint8_t out[LOGI_EOL_DEVICE_ID_PREFIX_BYTES])
{
    uint8_t bytes[LOGI_EOL_DEVICE_ID_PREFIX_BYTES] = {0};
    size_t nibbleCount = 0;
    for (const char* p = uuid; p != nullptr && *p != '\0'; ++p) {
        if (nibbleCount >= LOGI_EOL_DEVICE_ID_PREFIX_BYTES * 2) {
            break;
        }
        if (*p == '-') {
            continue;
        }
        int value = hexValue(*p);
        if (value < 0) {
            return false;
        }
        if ((nibbleCount & 1u) == 0) {
            bytes[nibbleCount / 2] = static_cast<uint8_t>(value << 4);
        } else {
            bytes[nibbleCount / 2] |= static_cast<uint8_t>(value);
        }
        nibbleCount++;
    }

    if (nibbleCount != LOGI_EOL_DEVICE_ID_PREFIX_BYTES * 2) {
        return false;
    }

    memcpy(out, bytes, LOGI_EOL_DEVICE_ID_PREFIX_BYTES);
    return true;
}

static bool isWifiAuthFailureReason(int reason)
{
    return reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
           reason == WIFI_REASON_AUTH_FAIL ||
           reason == WIFI_REASON_HANDSHAKE_TIMEOUT ||
           reason == WIFI_REASON_MIC_FAILURE;
}

static bool isWifiApNotFoundReason(int reason)
{
    return reason == WIFI_REASON_NO_AP_FOUND ||
           reason == WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY ||
           reason == WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD ||
           reason == WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD;
}

static DeviceShadowState buildReportedShadowFromSettings(DeviceSettings* deviceSettings,
                                                         const DeviceShadowState& parsedShadowState,
                                                         bool clearWifiResetRequest = false)
{
    DeviceShadowState reported{};

    if (deviceSettings == nullptr) {
        return reported;
    }

    WeeklySchedule schedules[DeviceSettings::MAX_WEEKLY_SCHEDULES];
    if (deviceSettings->getWeeklySchedules(schedules))
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

    reported.fill_dwell_time = deviceSettings->getFillDwellTime();
    reported.lte_timeout = deviceSettings->getLteTimeout();
    reported.fill_alarm_delta = deviceSettings->getFillAlarmDelta();
    reported.post_dwell_time = deviceSettings->getPostDwellTime();
    char mqttScheduledPost[DeviceSettings::MQTT_SCHEDULED_POST_BUFFER_SIZE] = {0};
    if (deviceSettings->getMqttScheduledPost(mqttScheduledPost, sizeof(mqttScheduledPost))) {
        reported.mqtt_scheduled_post = mqttScheduledPost;
    }
    reported.event_posts = deviceSettings->getEventPosts();
    reported.event_posts_valid = true;
    char eventThresholds[DeviceSettings::EVENT_THRESHOLDS_BUFFER_SIZE] = {0};
    if (deviceSettings->getEventThresholdsPct(eventThresholds, sizeof(eventThresholds))) {
        reported.event_thresholds_pct = eventThresholds;
    }
    char eventDirection[DeviceSettings::EVENT_DIRECTION_BUFFER_SIZE] = {0};
    if (deviceSettings->getEventDirection(eventDirection, sizeof(eventDirection))) {
        reported.event_direction = eventDirection;
    }

    reported.sensor_sample_rate = deviceSettings->getSensorSampleRateMinutes();

    // These fields currently do not have DeviceSettings storage, so report them
    // only when this cycle successfully parsed them from desired.
    reported.acquire_gps = parsedShadowState.acquire_gps;
    reported.acquire_gps_valid = parsedShadowState.acquire_gps_valid;
    reported.wifi_reset_req = clearWifiResetRequest
        ? false
        : (parsedShadowState.wifi_reset_req_valid ? parsedShadowState.wifi_reset_req : false);
    reported.wifi_reset_req_valid = true;
    reported.clear_wifi_reset_req_desired = clearWifiResetRequest;
    reported.mqtt_timeout = parsedShadowState.mqtt_timeout;

    return reported;
}

ProvisioningStateMachine::ProvisioningStateMachine(EspNvsStorage* nvsStorage,
                                                   EspBluetoothManager* bleManager,
                                                   ILogiHardwareDriver* driver,
                                                   AwsIotManager* awsIotManager,
                                                   EspPowerManager* powerManager,
                                                   EspTimeKeeper* timeKeeper,
                                                   DeviceSettings* deviceSettings)
    : _nvsStorage(nvsStorage)
    , _bleManager(bleManager)
    , _driver(driver)
    , _awsIotManager(awsIotManager)
    , _powerManager(powerManager)
    , _timeKeeper(timeKeeper)
    , _deviceSettings(deviceSettings)
{
}

ProvisioningStateMachine::~ProvisioningStateMachine()
{
    unregisterGapEventListener();
    unregisterProvisioningEventHandlers();
    wifi_prov_mgr_deinit();
}

bool ProvisioningStateMachine::init(ProvisioningMode mode)
{
    ESP_LOGI(TAG, "Initializing provisioning state machine (mode: %s)",
             mode == ProvisioningMode::FirstBoot ? "FirstBoot" : "ReProvision");

    _mode = mode;
    _provisioningComplete = false;
    _provisioningSuccess = false;
    _isBluetoothConnected = false;
    _credentialsReceived = false;
    _bleConnHandle = 0;
    _bleConnHandleValid = false;
    _provisioningSessionHadNewCredentials = false;
    _forceRepairBeaconOnTimeout = (mode == ProvisioningMode::ReProvision);
    _pendingSsid[0] = '\0';
    _pendingPassword[0] = '\0';
    _provisioningStartTimeMs = 0;
    _resetProvisioningManagerOnFailure = false;
    _resetProvisioningManagerForReprovision = false;
    _successDisplayStartMs = 0;
    _stateEnteredMs = esp_timer_get_time() / 1000; // arm deadman from init
    _firstBootGpsStartMs = 0;
    _firstBootLastGpsLogMs = 0;
    _firstBootGpsSnapshot = {};
    _firstBootGpsSnapshotValid = false;
    _lastStaDisconnectReason = 0;

    if (!registerProvisioningEventHandlers()) {
        return false;
    }

    _initialized = true;
    _currentState = ProvisioningState::ProvisioningState_Init;

    return true;
}

void ProvisioningStateMachine::update()
{
    if (!_initialized) {
        ESP_LOGE(TAG, "Update called before initialization!");
        return;
    }

    // ISS-FW-020 deadman: transient states must make progress. Advertising
    // (bounded by the provisioning timeout) and SuccessDisplay (bounded by its configured
    // window) are exempt; everything else older than the deadman means a lost
    // event or wedged handshake -> reboot. SW reset preserves credentials, so
    // a provisioned device lands in duty cycle and an unprovisioned one
    // re-enters a fresh provisioning window.
    if (_currentState != ProvisioningState::ProvisioningState_Advertising &&
        _currentState != ProvisioningState::ProvisioningState_SuccessDisplay) {
        int64_t in_state_ms = (esp_timer_get_time() / 1000) - _stateEnteredMs;
        int64_t deadman_ms = TRANSIENT_STATE_DEADMAN_MS;
        if (_currentState == ProvisioningState::ProvisioningState_PostJobsShadow &&
            _firstBootSubStep == 3) {
            deadman_ms = ACTIVATION_GPS_TIMEOUT_MS + (60LL * 1000LL);
        }

        if (_stateEnteredMs != 0 && in_state_ms > deadman_ms) {
            ESP_LOGE(TAG, "DEADMAN: state %d stuck for %lld ms (limit %lld) - rebooting (creds preserved)",
                     static_cast<int>(_currentState), in_state_ms, deadman_ms);
            vTaskDelay(pdMS_TO_TICKS(100)); // log flush
            if (_powerManager) {
                _powerManager->Reboot();
            } else {
                esp_restart();
            }
        }
    }

    switch (_currentState)
    {
        case ProvisioningState::ProvisioningState_Init:
            ESP_LOGI(TAG, "State: Init");
            ProvisioningStateInit();
            break;

        case ProvisioningState::ProvisioningState_Advertising:
            if (isProvisioningTimedOut()) {
                transitionTo(ProvisioningState::ProvisioningState_Timeout);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            break;

        case ProvisioningState::ProvisioningState_Connected:
            ESP_LOGI(TAG, "State: Connected");
            if (isProvisioningTimedOut()) {
                handleConnectedIdleTimeout();
                break;
            }
            ProvisioningStateConnected();
            break;

        case ProvisioningState::ProvisioningState_VerifyCredentials:
            ESP_LOGI(TAG, "State: VerifyCredentials");
            if (isProvisioningTimedOut()) {
                ESP_LOGW(TAG, "Provisioning credential verification idle timeout - resetting for retry");
                _resetProvisioningManagerForReprovision = true;
                transitionTo(ProvisioningState::ProvisioningState_Failed);
                break;
            }
            ProvisioningStateVerifyCredentials();
            break;

        case ProvisioningState::ProvisioningState_Success:
            ESP_LOGI(TAG, "State: Success");
            ProvisioningStateSuccess();
            break;

        case ProvisioningState::ProvisioningState_PostJobsShadow:
            ProvisioningStatePostJobsShadow();
            break;

        case ProvisioningState::ProvisioningState_SuccessDisplay:
            ProvisioningStateSuccessDisplay();
            break;

        case ProvisioningState::ProvisioningState_Failed:
            ESP_LOGI(TAG, "State: Failed");
            ProvisioningStateFailed();
            break;

        case ProvisioningState::ProvisioningState_Timeout:
            ESP_LOGI(TAG, "State: Timeout");
            ProvisioningStateTimeout();
            break;
    }
}

void ProvisioningStateMachine::transitionTo(ProvisioningState newState)
{
    ESP_LOGI(TAG, "Transitioning from state %d to %d",
             static_cast<int>(_currentState), static_cast<int>(newState));
    _currentState = newState;
    _stateEnteredMs = esp_timer_get_time() / 1000; // deadman reference
}

void ProvisioningStateMachine::setParentStateMachine(ApplicationStateMachine* parent)
{
    _parentStateMachine = parent;
}

bool ProvisioningStateMachine::isProvisioningTimedOut() const
{
    int64_t now_ms = esp_timer_get_time() / 1000;
    return (now_ms - _provisioningStartTimeMs) >= PROVISIONING_TIMEOUT_MS;
}

void ProvisioningStateMachine::noteProvisioningActivity()
{
    _provisioningStartTimeMs = esp_timer_get_time() / 1000;
}

void ProvisioningStateMachine::handleConnectedIdleTimeout()
{
    ESP_LOGW(TAG, "BLE provisioning connection idle timeout - disconnecting and continuing advertising");

    if (_bleConnHandleValid) {
        int rc = ble_gap_terminate(_bleConnHandle, BLE_ERR_REM_USER_CONN_TERM);
        if (rc != 0) {
            ESP_LOGW(TAG, "ble_gap_terminate during provisioning idle timeout failed, rc=%d", rc);
        }
    }

    _isBluetoothConnected = false;
    _bleConnHandleValid = false;
    _credentialsReceived = false;
    noteProvisioningActivity();
    transitionTo(ProvisioningState::ProvisioningState_Advertising);
}

// ============================================================================
// State Handlers
// ============================================================================

void ProvisioningStateMachine::ProvisioningStateInit()
{
    // Blue LED blink during provisioning
    if (_driver) {
        _driver->SetLedState(LedState::LedState_BlueBlink);
    }

    runEolBleBurstIfNeeded();

    // Backup existing credentials before starting (Req #7)
    backupCurrentCredentials();

    esp_err_t err = startProvisioningService();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start provisioning service: %s", esp_err_to_name(err));
        transitionTo(ProvisioningState::ProvisioningState_Failed);
        return;
    }

    noteProvisioningActivity();

    ESP_LOGI(TAG, "Provisioning started with %d minute idle timeout",
             CONFIG_LOGI_PROVISIONING_TIMEOUT_MINUTES);

    registerGapEventListener();

    transitionTo(ProvisioningState::ProvisioningState_Advertising);
}

void ProvisioningStateMachine::ProvisioningStateConnected()
{
    if (_credentialsReceived) {
        transitionTo(ProvisioningState::ProvisioningState_VerifyCredentials);
    }
    vTaskDelay(pdMS_TO_TICKS(100));
}

void ProvisioningStateMachine::ProvisioningStateVerifyCredentials()
{
    vTaskDelay(pdMS_TO_TICKS(100));
}

void ProvisioningStateMachine::ProvisioningStateSuccess()
{
    ESP_LOGI(TAG, "WiFi credentials verified by provisioning manager");

    // Save credentials to our NVS keys
    wifi_config_t wifi_cfg;
    if (esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg) == ESP_OK) {
        if (!saveCredentialsToNvs((const char*)wifi_cfg.sta.ssid,
                                  (const char*)wifi_cfg.sta.password)) {
            ESP_LOGE(TAG, "NVS credential write failed — staying in provisioning");
            _resetProvisioningManagerForReprovision = true;
            transitionTo(ProvisioningState::ProvisioningState_Failed);
            return;
        }

        if (_parentStateMachine) {
            _parentStateMachine->onProvisioningWifiRecovered();
        }
    } else {
        ESP_LOGE(TAG, "Failed to get WiFi config — staying in provisioning");
        transitionTo(ProvisioningState::ProvisioningState_Failed);
        return;
    }

    // Test AWS IoT connection (Req #3). REQ-FIRSTBOOT-01: keep the connection
    // alive so the PostJobsShadow state can reuse it for the 4-step sequence
    // (Disconnect happens at the end of that state).
    if (_timeKeeper != nullptr && !_timeKeeper->IsTimeSynced()) {
        ESP_LOGI(TAG, "Synchronizing UTC time before AWS IoT TLS/MQTT connect...");
        if (!_timeKeeper->SyncTime()) {
            ESP_LOGE(TAG, "NTP sync failed before AWS IoT connect - staying in provisioning");
            Faults_Set(FAULT_NTP);
            _resetProvisioningManagerForReprovision = true;
            transitionTo(ProvisioningState::ProvisioningState_Failed);
            return;
        }
    }

    ESP_LOGI(TAG, "Testing AWS IoT connection waterfall...");
    bool awsWaterfallConnected = _awsIotManager &&
                                 _awsIotManager->ConnectWithActivationWaterfall(
                                     AWS_IOT_ACTIVATION_PRIMARY_TIMEOUT_S,
                                     AWS_IOT_ACTIVATION_BACKUP_TIMEOUT_S);
    if (!awsWaterfallConnected) {
        ESP_LOGE(TAG, "AWS IoT connection failed - attempting UDP activation fallback");
        Faults_Set(FAULT_AWS);
        if (!publishFirstBootUdpTelemetry()) {
            ESP_LOGE(TAG, "UDP activation fallback failed - staying in provisioning");
            _resetProvisioningManagerForReprovision = true;
            transitionTo(ProvisioningState::ProvisioningState_Failed);
            return;
        }

        ESP_LOGI(TAG, "Provisioning success confirmed; waiting %lu ms before BLE teardown",
                 static_cast<unsigned long>(PROVISIONING_SUCCESS_BLE_GRACE_MS));
        vTaskDelay(pdMS_TO_TICKS(PROVISIONING_SUCCESS_BLE_GRACE_MS));
        stopProvisioningService();
        _provisioningComplete = true;
        _provisioningSuccess = true;
        if (_driver) {
            _driver->SetLedState(LedState::LedState_Off);
        }
        if (_parentStateMachine) {
            _parentStateMachine->transitionTo(ApplicationState::ApplicationState_Sleep);
        } else if (_powerManager) {
            _powerManager->Sleep(60ULL * 1000ULL * 1000ULL);
        }
        return;
    }

    if (awsWaterfallConnected) {
        ESP_LOGI(TAG, "AWS IoT connection verified — keeping connection for first-boot sequence");
    } else {
        ESP_LOGE(TAG, "AWS IoT connection failed — staying in provisioning");
        transitionTo(ProvisioningState::ProvisioningState_Failed);
        return;
    }

    // All three checks passed: creds received, AWS connected, NVS write succeeded
    ESP_LOGI(TAG, "Provisioning success confirmed; waiting %lu ms before BLE teardown",
             static_cast<unsigned long>(PROVISIONING_SUCCESS_BLE_GRACE_MS));
    vTaskDelay(pdMS_TO_TICKS(PROVISIONING_SUCCESS_BLE_GRACE_MS));
    stopProvisioningService();

    _provisioningComplete = true;
    _provisioningSuccess = true;

    // Green LED blink starts now (covers both the first-boot sequence and the
    // 30-min display window — single transition for the user-visible cue).
    ESP_LOGI(TAG, "Provisioning successful — running first-boot sequence then %d-min display",
             CONFIG_LOGI_PROVISIONING_SUCCESS_DISPLAY_MIN);

    // Green LED on from AWS-connect so it blinks 1 Hz for the WHOLE window (the
    // first-boot cycle + the configured success-display hold), per the activation spec.
    if (_driver) {
        _driver->SetLedState(LedState::LedState_GreenBlink);
    }

    // REQ-FIRSTBOOT-01: run post -> shadow -> jobs -> post BEFORE the long
    // success-display window, while AWS is still connected.
    _firstBootSubStep = 0;
    _firstBootStepStartMs = esp_timer_get_time() / 1000;
    transitionTo(ProvisioningState::ProvisioningState_PostJobsShadow);
}

// REQ-FIRSTBOOT-01: post -> check jobs -> sync shadow -> post (with applied
// shadow values). Implemented as a tick-driven sub-state machine so update()
// stays non-blocking and the BLE/Wi-Fi stacks keep servicing events between
// steps.
//
// Sub-step layout:
//   0 -> publish initial telemetry (POST #1, ACK: sensors, no GPS)
//   1 -> fetch device shadow and apply desired-state fields to DeviceSettings
//   2 -> request pending jobs (OTA)
//   3 -> publish telemetry again with updated context (POST #2)
//   4 -> disconnect AWS, hand off to SuccessDisplay
//
// On any step failure we log WARN and advance — the success-display window
// still runs so the user sees the green LED and the device reboots into normal
// duty cycle. Hard failure during prov was already caught earlier in Success.
void ProvisioningStateMachine::ProvisioningStatePostJobsShadow()
{
    if (!_awsIotManager) {
        ESP_LOGE(TAG, "REQ-FIRSTBOOT-01: no AwsIotManager — skipping sequence");
        _successDisplayStartMs = esp_timer_get_time() / 1000;
        if (_driver) {
            _driver->SetLedState(LedState::LedState_GreenBlink);
        }
        transitionTo(ProvisioningState::ProvisioningState_SuccessDisplay);
        return;
    }

    AwsIotClient* client = _awsIotManager->GetAwsClient();
    AwsIotJobsHandler* jobs = _awsIotManager->GetJobsHandler();

    // Tick pacing: 250 ms minimum between sub-steps so AWS callbacks land and
    // network buffers drain. Cheap insurance — total sequence still completes
    // in ~1.5 s under good conditions.
    int64_t now_ms = esp_timer_get_time() / 1000;
    if ((now_ms - _firstBootStepStartMs) < 250 && _firstBootSubStep > 0) {
        vTaskDelay(pdMS_TO_TICKS(50));
        return;
    }
    _firstBootStepStartMs = now_ms;

    switch (_firstBootSubStep) {
    case 0: {
        // POST #1 — activation "ack" post. V13-010: sample the (fast) sensors
        // first so this carries real battery/fuel/temp/solar/supply values
        // instead of a zero-init struct (which published bat:0/ful:0/amb:0/...
        // to the cloud). GPS is not waited on here (no fix yet -> 0; it fills in
        // the duty loop). Identity (deviceId/imei) lives in TelemetryContext and
        // is stamped by AwsIotClient internally.
        ESP_LOGI(TAG, "REQ-FIRSTBOOT-01 [1/4]: initial (ack) POST");
        if (client) {
            if (_driver) {
                _driver->UpdateMeasurements();
                _driver->GetLatestSensorData(_firstBootSensorSnapshot);
                // GPS: GetLatestSensorData powered GNSS off; re-enable it so it
                // can acquire a fix across shadow/jobs for the final post.
                _driver->SetGnssPower(true);
            }
            TelemetryContext context{};
            populateFirstBootTelemetryContext(context, _firstBootSensorSnapshot);
            bool ok = client->PublishTelemetryLogi4Format(_firstBootSensorSnapshot, context);
            ESP_LOGI(TAG, "REQ-FIRSTBOOT-01 [1/4]: POST %s", ok ? "ok" : "FAILED");
        } else {
            ESP_LOGW(TAG, "REQ-FIRSTBOOT-01 [1/4]: no client — skipped");
        }
        _firstBootSubStep = 1;
        break;
    }

    case 1: {
        ESP_LOGI(TAG, "REQ-FIRSTBOOT-01 [2/4]: fetch shadow + apply to DeviceSettings");
        DeviceShadowState shadow{};
        bool ok = _awsIotManager->GetEnhancedShadow(shadow);
        if (ok && _deviceSettings) {
            // REQ-SHADOW: persist each desired-state field. Setters log
            // their own apply/reject so we don't double-log here.
            // 0 means "field not present in shadow" — skip those.
            if (_parentStateMachine) _parentStateMachine->updateSchedulesFromShadow(shadow);
            if (shadow.fill_dwell_time != 0) _deviceSettings->setFillDwellTime(shadow.fill_dwell_time);
            if (shadow.fill_alarm_delta != 0) _deviceSettings->setFillAlarmDelta(shadow.fill_alarm_delta);
            if (shadow.post_dwell_time  != 0) _deviceSettings->setPostDwellTime(shadow.post_dwell_time);
            if (shadow.mqtt_timeout     != 0) _deviceSettings->setMqttTimeout(shadow.mqtt_timeout);
            if (shadow.lte_timeout      != 0) _deviceSettings->setLteTimeout(shadow.lte_timeout);
            if (shadow.sensor_sample_rate != 0) _deviceSettings->setSensorSampleRateMinutes(shadow.sensor_sample_rate);
            if (shadow.event_posts_valid) _deviceSettings->setEventPosts(shadow.event_posts);
            if (!shadow.event_thresholds_pct.empty()) _deviceSettings->setEventThresholdsPct(shadow.event_thresholds_pct.c_str());
            if (!shadow.event_direction.empty()) _deviceSettings->setEventDirection(shadow.event_direction.c_str());
            if (!shadow.mqtt_scheduled_post.empty()) _deviceSettings->setMqttScheduledPost(shadow.mqtt_scheduled_post.c_str());
            _deviceSettings->Commit();
            const bool clearWifiResetRequest = shadow.wifi_reset_req_valid && shadow.wifi_reset_req;
            if (_parentStateMachine) {
                _parentStateMachine->clearWifiResetRepairMode();
                if (clearWifiResetRequest) {
                    ESP_LOGI(TAG, "Shadow wifi_reset_req=true after Wi-Fi reprovision; reporting request cleared");
                }
            }
            DeviceShadowState reported = buildReportedShadowFromSettings(_deviceSettings, shadow, clearWifiResetRequest);
            if (_awsIotManager->UpdateShadowWithStatus(reported)) {
                ESP_LOGI(TAG, "REQ-FIRSTBOOT-01 [2/4]: shadow applied and reported");
            } else {
                ESP_LOGW(TAG, "REQ-FIRSTBOOT-01 [2/4]: shadow applied but reported update FAILED");
            }
        } else {
            ESP_LOGW(TAG, "REQ-FIRSTBOOT-01 [2/4]: shadow fetch %s",
                     ok ? "ok but no DeviceSettings to apply" : "FAILED");
        }
        _firstBootSubStep = 2;
        break;
    }

    case 2: {
        ESP_LOGI(TAG, "REQ-FIRSTBOOT-01 [3/4]: request pending jobs");
        if (_awsIotManager->IsConnectedViaBackup443()) {
            ESP_LOGW(TAG, "REQ-FIRSTBOOT-01 [3/4]: backup AWS MQTT 443 link active; skipping jobs/OTA");
            ESP_LOGI(TAG, "REQ-FIRSTBOOT-01 [3/4]: closing AWS MQTT before GPS acquisition");
            _awsIotManager->Disconnect();
            _firstBootGpsStartMs = esp_timer_get_time() / 1000;
            _firstBootLastGpsLogMs = 0;
            _firstBootSubStep = 3;
            break;
        }
        if (jobs) {
            bool ok = jobs->RequestPendingJobs();
            ESP_LOGI(TAG, "REQ-FIRSTBOOT-01 [3/4]: jobs %s", ok ? "requested" : "FAILED");
        } else {
            ESP_LOGW(TAG, "REQ-FIRSTBOOT-01 [3/4]: no jobs handler — skipped");
        }
        ESP_LOGI(TAG, "REQ-FIRSTBOOT-01 [3/4]: closing AWS MQTT before GPS acquisition");
        _awsIotManager->Disconnect();
        _firstBootGpsStartMs = esp_timer_get_time() / 1000;
        _firstBootLastGpsLogMs = 0;
        _firstBootSubStep = 3;
        break;
    }

    case 3: {
        constexpr int64_t GPS_WAIT_MS = ACTIVATION_GPS_TIMEOUT_MS;
        constexpr int64_t GPS_LOG_INTERVAL_MS = 10000;

        if (_driver) {
            int64_t gpsElapsedMs = (esp_timer_get_time() / 1000) - _firstBootGpsStartMs;
            if (!_driver->HasValidGpsFix() && gpsElapsedMs < GPS_WAIT_MS) {
                if (_firstBootLastGpsLogMs == 0 ||
                    (gpsElapsedMs - _firstBootLastGpsLogMs) >= GPS_LOG_INTERVAL_MS) {
                    _firstBootLastGpsLogMs = gpsElapsedMs;
                    ESP_LOGI(TAG, "REQ-FIRSTBOOT-01 [4/5]: waiting for GPS fix before confirm POST: %lld/%lld ms",
                             gpsElapsedMs,
                             GPS_WAIT_MS);
                    _driver->PrintGpsStatus();
                }
                vTaskDelay(pdMS_TO_TICKS(250));
                return;
            }

            if (_driver->HasValidGpsFix()) {
                _firstBootGpsSnapshotValid = _driver->GetGpsData(_firstBootGpsSnapshot) && _firstBootGpsSnapshot.valid;
                ESP_LOGI(TAG, "REQ-FIRSTBOOT-01 [4/5]: GPS fix acquired before confirm POST");
            } else {
                ESP_LOGW(TAG, "REQ-FIRSTBOOT-01 [4/5]: GPS fix not acquired before timeout; confirm POST will use defaults");
                Faults_Set(FAULT_GPS);
            }
        }

        _firstBootSubStep = 4;
        break;
    }

    case 4: {
        ESP_LOGI(TAG, "REQ-FIRSTBOOT-01 [5/5]: resample + confirming POST after shadow apply");
        bool ok = false;
        if (reconnectForFinalActivationPost()) {
            AwsIotClient* finalClient = _awsIotManager ? _awsIotManager->GetAwsClient() : nullptr;
            ok = publishFirstBootTelemetry(finalClient);
            ESP_LOGI(TAG, "REQ-FIRSTBOOT-01 [5/5]: final MQTT POST %s", ok ? "ok" : "FAILED");
        } else {
            ESP_LOGW(TAG, "REQ-FIRSTBOOT-01 [5/5]: final MQTT reconnect failed; using compact UDP activation fallback");
            ok = publishFirstBootUdpTelemetry();
            ESP_LOGI(TAG, "REQ-FIRSTBOOT-01 [5/5]: final UDP fallback POST %s", ok ? "ok" : "FAILED");
        }
        _firstBootSubStep = 5;
        break;
    }

    case 5:
    default: {
        ESP_LOGI(TAG, "REQ-FIRSTBOOT-01: sequence complete — disconnecting AWS");
        _awsIotManager->Disconnect();
        if (_driver) {
            _driver->SetLedState(LedState::LedState_Off);
        }
        if (_parentStateMachine) {
            _parentStateMachine->transitionTo(ApplicationState::ApplicationState_Sleep);
        } else if (_powerManager) {
            _powerManager->Sleep(60ULL * 1000ULL * 1000ULL);
        }
        break;
    }
    }
}

void ProvisioningStateMachine::ProvisioningStateSuccessDisplay()
{
    int64_t now_ms = esp_timer_get_time() / 1000;
    int64_t elapsed_ms = now_ms - _successDisplayStartMs;

    if (elapsed_ms >= SUCCESS_DISPLAY_MS) {
        ESP_LOGI(TAG, "Success display complete — rebooting into normal duty cycle");

        if (_driver) {
            _driver->SetLedState(LedState::LedState_Off);
        }

        vTaskDelay(pdMS_TO_TICKS(100));

        if (_parentStateMachine) {
            _parentStateMachine->transitionTo(ApplicationState::ApplicationState_Sleep);
        } else if (_powerManager) {
            _powerManager->Sleep(60ULL * 1000ULL * 1000ULL);
        }
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
}

void ProvisioningStateMachine::ProvisioningStateFailed()
{
    _forceRepairBeaconOnTimeout = true;
    ESP_LOGW(TAG, "Provisioning verification failed — allowing retry");

    // Return to blue blink (Req #4: failed credentials keep device in provisioning)
    if (_driver) {
        _driver->SetLedState(LedState::LedState_BlueBlink);
    }

    quiesceWifiBeforeProvisioningRetry();
    restoreBackupAfterFailedProvisioning();

    bool resetOk = true;

    if (_resetProvisioningManagerOnFailure) {
        esp_err_t err = wifi_prov_mgr_reset_sm_state_on_failure();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Provisioning failure-state reset failed: %s", esp_err_to_name(err));
            resetOk = false;
        }
        _resetProvisioningManagerOnFailure = false;
    }

    if (_resetProvisioningManagerForReprovision) {
        esp_err_t err = wifi_prov_mgr_reset_sm_state_for_reprovision();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Provisioning reprovision-state reset failed: %s", esp_err_to_name(err));
            resetOk = false;
        }
        _resetProvisioningManagerForReprovision = false;
    }

    _credentialsReceived = false;
    _provisioningSessionHadNewCredentials = false;
    noteProvisioningActivity();

    if (!resetOk) {
        ESP_LOGW(TAG, "Provisioning manager reset failed after Wi-Fi quiesce; restarting BLE provisioning service");
        stopProvisioningService();

        bool restarted = registerProvisioningEventHandlers();
        if (restarted) {
            esp_err_t err = startProvisioningService();
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Provisioning service restart after reset failure failed: %s",
                         esp_err_to_name(err));
                restarted = false;
            }
        }

        if (restarted) {
            registerGapEventListener();
            noteProvisioningActivity();
            transitionTo(ProvisioningState::ProvisioningState_Advertising);
        } else {
            ESP_LOGE(TAG, "Unable to restart BLE provisioning service; entering repair beacon as last resort");
            _provisioningComplete = true;
            _provisioningSuccess = false;
            if (_parentStateMachine) {
                time_t now = _timeKeeper ? _timeKeeper->GetCurrentTime() : time(nullptr);
                _parentStateMachine->requestProvisioningTimeoutRepairMode(now);
                _parentStateMachine->transitionTo(ApplicationState::ApplicationState_Sleep);
            }
        }
        return;
    }

    if (_isBluetoothConnected) {
        transitionTo(ProvisioningState::ProvisioningState_Connected);
    } else {
        transitionTo(ProvisioningState::ProvisioningState_Advertising);
    }
}

void ProvisioningStateMachine::ProvisioningStateTimeout()
{
    ESP_LOGW(TAG, "Provisioning timeout reached (%d minutes)",
             CONFIG_LOGI_PROVISIONING_TIMEOUT_MINUTES);

    stopProvisioningService();

    if (_driver) {
        _driver->SetLedState(LedState::LedState_Off);
    }

    _provisioningComplete = true;
    _provisioningSuccess = false;

    if (!_forceRepairBeaconOnTimeout && hasStoredCredentials()) {
        // Has valid creds — reboot into normal duty cycle (Req #6)
        ESP_LOGI(TAG, "Valid credentials exist — rebooting to normal duty cycle");
        restoreBackupCredentials();
        vTaskDelay(pdMS_TO_TICKS(100));

        if (_powerManager) {
            _powerManager->Reboot();
        } else {
            esp_restart();
        }
    } else {
        ESP_LOGW(TAG, "Entering BLE repair-beacon mode after provisioning timeout");
        vTaskDelay(pdMS_TO_TICKS(100));

        if (_parentStateMachine) {
            time_t now = _timeKeeper ? _timeKeeper->GetCurrentTime() : time(nullptr);
            _parentStateMachine->requestProvisioningTimeoutRepairMode(now);
            _parentStateMachine->transitionTo(ApplicationState::ApplicationState_Sleep);
        } else {
            ESP_LOGE(TAG, "No parent state machine for repair beacon mode; falling back to last-resort deep sleep");
            esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
            esp_deep_sleep_start();
        }
    }
}

// ============================================================================
// Provisioning Service Management
// ============================================================================

void ProvisioningStateMachine::runEolBleBurstIfNeeded()
{
#if CONFIG_LOGI_EOL_BLE_BURST_ENABLED
    if (s_eolBleBurstRanThisBoot) {
        ESP_LOGD(TAG, "EOL BLE diagnostics burst already ran this boot; skipping");
        return;
    }
    s_eolBleBurstRanThisBoot = true;

    if (_bleManager == nullptr) {
        ESP_LOGW(TAG, "EOL BLE diagnostics skipped: no BLE manager");
        return;
    }

    uint8_t advPayload[LOGI_EOL_ADV_PAYLOAD_LEN] = {0};
    uint8_t scanResponsePayload[LOGI_EOL_SCAN_RESPONSE_PAYLOAD_LEN] = {0};
    size_t advPayloadLen = 0;
    size_t scanResponsePayloadLen = 0;
    if (!buildEolBlePayloads(advPayload,
                             sizeof(advPayload),
                             advPayloadLen,
                             scanResponsePayload,
                             sizeof(scanResponsePayload),
                             scanResponsePayloadLen)) {
        ESP_LOGW(TAG, "EOL BLE diagnostics skipped: payload build failed");
        return;
    }

    if (!_bleManager->isInitialized()) {
        _bleManager->init();
        _bleManager->startHost();
    }

    ESP_LOGI(TAG, "Starting EOL BLE diagnostics burst for %d seconds",
             CONFIG_LOGI_EOL_BLE_BURST_SECONDS);

    bool started = _bleManager->startAdvertisingWithManufacturerData(
        "LOGI4W-EOL",
        CONFIG_LOGI_EOL_BLE_ADV_INTERVAL_MS,
        advPayload,
        advPayloadLen,
        scanResponsePayload,
        scanResponsePayloadLen,
        false);

    if (started) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_LOGI_EOL_BLE_BURST_SECONDS * 1000));
    } else {
        ESP_LOGW(TAG, "EOL BLE diagnostics advertising failed to start");
    }

    _bleManager->stopAdvertising();
    _bleManager->deinit();
    vTaskDelay(pdMS_TO_TICKS(250));
    ESP_LOGI(TAG, "EOL BLE diagnostics burst complete; starting normal provisioning next");
#endif
}

bool ProvisioningStateMachine::buildEolBlePayloads(uint8_t* advPayload,
                                                   size_t advPayloadCapacity,
                                                   size_t& advPayloadLen,
                                                   uint8_t* scanResponsePayload,
                                                   size_t scanResponsePayloadCapacity,
                                                   size_t& scanResponsePayloadLen)
{
    if (advPayload == nullptr || scanResponsePayload == nullptr ||
        advPayloadCapacity < LOGI_EOL_ADV_PAYLOAD_LEN ||
        scanResponsePayloadCapacity < LOGI_EOL_SCAN_RESPONSE_PAYLOAD_LEN) {
        return false;
    }

    LogiSensorData sensorData{};
    if (_driver != nullptr) {
        _driver->UpdateMeasurements();
        _driver->GetLatestSensorData(sensorData);
    }

    char deviceId[DeviceSettings::DEVICE_ID_BUFFER_SIZE] = {0};
    bool deviceIdValid = _deviceSettings != nullptr &&
                         _deviceSettings->isDeviceIdValid() &&
                         _deviceSettings->getDeviceId(deviceId, sizeof(deviceId));

    uint8_t deviceIdPrefixBytes[LOGI_EOL_DEVICE_ID_PREFIX_BYTES] = {0};
    const bool deviceIdPrefixEncoded = deviceIdValid &&
                                       parseUuidPrefixBytes(deviceId, deviceIdPrefixBytes);
    const uint32_t deviceIdHash = deviceIdValid ? fnv1a32(deviceId) : 0;
    const uint16_t batteryMv = voltsToMillivolts(sensorData.AnalogBatteryVoltage);
    const uint16_t solarMv = voltsToMillivolts(sensorData.SolarVoltage);
    const uint16_t sensorRawMv = voltsToMillivolts(sensorData.AnalogFuelVoltage);
    const uint16_t sensorSupplyMv = voltsToMillivolts(sensorData.SensorSupplyVoltage);
    const uint16_t fuelTenthsPct =
        static_cast<uint16_t>(sensorData.PublishedFuelLevel <= 100
                                  ? sensorData.PublishedFuelLevel * 10u
                                  : 0u);
    const uint16_t faults = static_cast<uint16_t>(Faults_Get() & 0xFFFFu);
    const uint8_t temperatureC = temperatureToInt8C(sensorData.MeasuredTemperatureC);
    const bool temperatureValid = sensorData.MeasuredTemperatureC >= LOGI_EOL_TEMP_MIN_C &&
                                  sensorData.MeasuredTemperatureC <= LOGI_EOL_TEMP_MAX_C &&
                                  (faults & FAULT_AMB) == 0;

    uint8_t flags = 0;
    if (deviceIdValid) flags |= EOL_FLAG_DEVICE_ID_VALID;
    if (batteryMv > 0) flags |= EOL_FLAG_BATTERY_VALID;
    if (solarMv > 0) flags |= EOL_FLAG_SOLAR_VALID;
    if (sensorRawMv > 0) flags |= EOL_FLAG_SENSOR_VALID;
    if (sensorSupplyMv > 0) flags |= EOL_FLAG_SUPPLY_VALID;
    if (sensorData.PublishedFuelLevel <= 100) flags |= EOL_FLAG_FUEL_VALID;
    if (deviceIdPrefixEncoded) flags |= EOL_FLAG_DEVICE_ID_PREFIX_ENCODED;
    if (temperatureValid) flags |= EOL_FLAG_TEMPERATURE_VALID;

    advPayloadLen = 0;
    appendU16Le(advPayload, advPayloadCapacity, advPayloadLen, LOGI_BLE_COMPANY_ID);
    appendU8(advPayload, advPayloadCapacity, advPayloadLen, 'L');
    appendU8(advPayload, advPayloadCapacity, advPayloadLen, 'W');
    appendU8(advPayload, advPayloadCapacity, advPayloadLen, temperatureC);
    appendU8(advPayload, advPayloadCapacity, advPayloadLen, flags);
    appendU32Le(advPayload, advPayloadCapacity, advPayloadLen, deviceIdHash);
    appendU8(advPayload, advPayloadCapacity, advPayloadLen, CONFIG_LOGI_SOFTWARE_VERSION_MAJOR);
    appendU8(advPayload, advPayloadCapacity, advPayloadLen, CONFIG_LOGI_SOFTWARE_VERSION_MINOR);
    appendU8(advPayload, advPayloadCapacity, advPayloadLen, CONFIG_LOGI_SOFTWARE_VERSION_REVISION);
    appendU16Le(advPayload, advPayloadCapacity, advPayloadLen, batteryMv);
    appendU16Le(advPayload, advPayloadCapacity, advPayloadLen, solarMv);
    appendU16Le(advPayload, advPayloadCapacity, advPayloadLen, sensorRawMv);
    appendU16Le(advPayload, advPayloadCapacity, advPayloadLen, sensorSupplyMv);
    appendU16Le(advPayload, advPayloadCapacity, advPayloadLen, fuelTenthsPct);
    appendU16Le(advPayload, advPayloadCapacity, advPayloadLen, faults);

    scanResponsePayloadLen = 0;
    appendU16Le(scanResponsePayload, scanResponsePayloadCapacity, scanResponsePayloadLen, LOGI_BLE_COMPANY_ID);
    appendU8(scanResponsePayload, scanResponsePayloadCapacity, scanResponsePayloadLen, 'L');
    appendU8(scanResponsePayload, scanResponsePayloadCapacity, scanResponsePayloadLen, 'I');
    appendU8(scanResponsePayload, scanResponsePayloadCapacity, scanResponsePayloadLen, temperatureC);
    appendU8(scanResponsePayload, scanResponsePayloadCapacity, scanResponsePayloadLen, flags);
    appendU32Le(scanResponsePayload, scanResponsePayloadCapacity, scanResponsePayloadLen, deviceIdHash);
    for (uint8_t byte : deviceIdPrefixBytes) {
        appendU8(scanResponsePayload, scanResponsePayloadCapacity, scanResponsePayloadLen, byte);
    }

    ESP_LOGI(TAG,
             "EOL BLE payload: flags=0x%02X fw=%d.%d.%d temp_c=%d bat=%u sol=%u raw=%u sup=%u fuel_x10=%u faults=0x%04X id_hash=0x%08lX",
             flags,
             CONFIG_LOGI_SOFTWARE_VERSION_MAJOR,
             CONFIG_LOGI_SOFTWARE_VERSION_MINOR,
             CONFIG_LOGI_SOFTWARE_VERSION_REVISION,
             static_cast<int>(static_cast<int8_t>(temperatureC)),
             batteryMv,
             solarMv,
             sensorRawMv,
             sensorSupplyMv,
             fuelTenthsPct,
             faults,
             static_cast<unsigned long>(deviceIdHash));

    return advPayloadLen <= advPayloadCapacity && scanResponsePayloadLen <= scanResponsePayloadCapacity;
}

esp_err_t ProvisioningStateMachine::startProvisioningService()
{
    ESP_LOGI(TAG, "Starting WiFi provisioning service");
    // REQ-PROV-02 (v1.3.0): auto light sleep during the prov window with
    // 1 Hz BLE advertising. HISTORY: v1.2.0 shipped this with the BLE LP clock
    // on the main XTAL (off in light sleep) - advertising, USB, and the LED
    // all died (Nick board-2; bench-reproduced 2026-06-12) and v1.2.1 pinned
    // the window awake as a hotfix. v1.3.0 runs the BLE LP clock from the
    // rev-4.1 32.768 kHz crystal (CONFIG_RTC_CLK_SRC_EXT_CRYS +
    // BT_LE_LP_CLK_SRC_DEFAULT) - the IDF-supported config for BLE through
    // light sleep. Bench units on USB are unaffected: the USJ keep-awake lock
    // (CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION) holds them out of light sleep
    // while a host is attached. Joulescope target ~3.79 mA @ 1000 ms ADV.
    if (_powerManager) {
        // V13-013 FIX: do NOT enable light sleep in the provisioning window. With
        // light sleep allowed, the device hard-hangs and is reset by the interrupt
        // watchdog (INT_WDT, reset_reason=5): the free-running LED I2C write lands
        // across a light-sleep entry/exit on the marginal bus and stalls with
        // interrupts disabled. Symptom (V13-013): activation/blue LED only start
        // when a USB host holds the USJ awake (CONFIG_USJ_NO_AUTO_LS_ON_CONNECTION
        // masks it); battery/no-COM field units look dead. It also saves ~no power
        // (WiFi STA is up the whole window). Keep the window AWAKE; battery life
        // comes from deep sleep in the duty cycle, not light sleep here.
        _powerManager->AllowLightSleep(false);
        ESP_LOGI(TAG, "Prov: light sleep DISABLED (V13-013 INT_WDT fix) - window stays awake");
    }

    wifi_prov_mgr_config_t config = {};
    config.scheme = wifi_prov_scheme_ble;
    // Keep BLE memory available after provisioning. The normal duty cycle can
    // later start the repair beacon in the same boot if Wi-Fi is lost.
    config.scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BT;

    esp_err_t err = wifi_prov_mgr_init(config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init wifi_prov_mgr: %s", esp_err_to_name(err));
        return err;
    }

    wifi_prov_mgr_disable_auto_stop(1000);

    char service_name[16];
    err = getDeviceServiceName(service_name, sizeof(service_name));
    if (err != ESP_OK) {
        return err;
    }

    char pop[12];
    err = getDevicePop(pop, sizeof(pop));
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Service name: %s, PoP: %s", service_name, pop);

    err = wifi_prov_mgr_start_provisioning(
        WIFI_PROV_SECURITY_1,
        pop,
        service_name,
        NULL
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start provisioning: %s", esp_err_to_name(err));
        wifi_prov_mgr_deinit();
        return err;
    }

    return ESP_OK;
}

void ProvisioningStateMachine::stopProvisioningService()
{
    ESP_LOGI(TAG, "Stopping WiFi provisioning service");
    wifi_prov_mgr_stop_provisioning();
    wifi_prov_mgr_deinit();
    unregisterGapEventListener();
    unregisterProvisioningEventHandlers();
    // REQ-PROV-02: re-acquire NO_LIGHT_SLEEP lock when leaving prov mode (normal
    // duty cycle uses deep sleep between minute wakes, not auto light sleep).
    if (_powerManager) {
        _powerManager->AllowLightSleep(false);
        ESP_LOGI(TAG, "Prov: light sleep blocked (back to duty cycle)");
    }
}

esp_err_t ProvisioningStateMachine::getDeviceServiceName(char* serviceName, size_t maxLen)
{
    // REQ-BLE-01: advertise as "MyPropane-XXXX" where XXXX = first 4 hex chars
    // of the factory-provisioned UUID. Unprovisioned boards fall back to
    // "MyPropane-0000" with a WARN so a tech knows to factory-provision.
    if (_deviceSettings != nullptr && _deviceSettings->isDeviceIdValid()) {
        char uuid[40] = {0};
        if (_deviceSettings->getDeviceId(uuid, sizeof(uuid))) {
            char prefix[5] = {0};
            strncpy(prefix, uuid, 4);
            snprintf(serviceName, maxLen, "MyPropane-%s", prefix);
            return ESP_OK;
        }
    }

    ESP_LOGW(TAG, "Device not factory-provisioned (no valid DeviceID) - advertising as plain MyPropane (REV B spec default; the -XXXX suffix is the per-board enhancement)");
    snprintf(serviceName, maxLen, "MyPropane");
    return ESP_OK;
}

esp_err_t ProvisioningStateMachine::getDevicePop(char* pop, size_t maxLen)
{
    if (pop == nullptr || maxLen < strlen(LOGI_PROVISIONING_POP) + 1) {
        return ESP_ERR_INVALID_ARG;
    }

    snprintf(pop, maxLen, "%s", LOGI_PROVISIONING_POP);
    return ESP_OK;
}

bool ProvisioningStateMachine::publishFirstBootTelemetry(AwsIotClient* client)
{
    if (client == nullptr) {
        return false;
    }

    if (_driver) {
        ESP_LOGI(TAG, "REQ-FIRSTBOOT-01 [5/5]: resampling sensors/peripherals before final activation POST");
        _driver->UpdateMeasurements();
        _driver->GetLatestSensorData(_firstBootSensorSnapshot);
        if (_firstBootGpsSnapshotValid) {
            _firstBootSensorSnapshot.GPSData = _firstBootGpsSnapshot;
        }
        _driver->SetGnssPower(false);
    }

    time_t now = 0;
    time(&now);
    if (now >= 1609459200) {
        _firstBootSensorSnapshot.elapsedTimeStampS = static_cast<uint32_t>(now);
    }

    TelemetryContext context{};
    populateFirstBootTelemetryContext(context, _firstBootSensorSnapshot);
    return client->PublishTelemetryLogi4Format(_firstBootSensorSnapshot, context);
}

bool ProvisioningStateMachine::publishFirstBootUdpTelemetry()
{
    if (_driver) {
        _driver->UpdateMeasurements();
        _driver->GetLatestSensorData(_firstBootSensorSnapshot);
        if (_firstBootGpsSnapshotValid) {
            _firstBootSensorSnapshot.GPSData = _firstBootGpsSnapshot;
        }
        _driver->SetGnssPower(false);
    }

    time_t now = 0;
    time(&now);
    if (now >= 1609459200) {
        _firstBootSensorSnapshot.elapsedTimeStampS = static_cast<uint32_t>(now);
    }

    TelemetryContext context{};
    populateFirstBootTelemetryContext(context, _firstBootSensorSnapshot);
    ESP_LOGI(TAG, "Publishing activation telemetry via compact UDP fallback");
    return UdpTelemetryClient::SendTelemetry(_firstBootSensorSnapshot, context, 1);
}

bool ProvisioningStateMachine::reconnectForFinalActivationPost()
{
    if (_awsIotManager == nullptr)
    {
        ESP_LOGE(TAG, "REQ-FIRSTBOOT-01 [5/5]: no AWS IoT manager for final activation reconnect");
        return false;
    }

    AwsIotClient* client = _awsIotManager->GetAwsClient();
    if (client != nullptr && client->IsConnected())
    {
        ESP_LOGI(TAG, "REQ-FIRSTBOOT-01 [5/5]: refreshing AWS MQTT connection before final activation POST");
        _awsIotManager->Disconnect();
    }
    else
    {
        ESP_LOGW(TAG, "REQ-FIRSTBOOT-01 [5/5]: AWS MQTT not connected before final activation POST; reconnecting");
    }

    bool connected = _awsIotManager->ConnectWithActivationWaterfall(
        AWS_IOT_ACTIVATION_PRIMARY_TIMEOUT_S,
        AWS_IOT_ACTIVATION_BACKUP_TIMEOUT_S);
    if (connected)
    {
        ESP_LOGI(TAG, "REQ-FIRSTBOOT-01 [5/5]: final activation MQTT reconnect ok on port %s",
                 _awsIotManager->IsConnectedViaBackup443() ? "443" : "8883");
    }
    else
    {
        ESP_LOGE(TAG, "REQ-FIRSTBOOT-01 [5/5]: final activation MQTT reconnect failed");
    }

    return connected;
}

void ProvisioningStateMachine::populateFirstBootTelemetryContext(TelemetryContext& ctx, const LogiSensorData& data) const
{
    memset(&ctx, 0, sizeof(TelemetryContext));

    if (_deviceSettings != nullptr) {
        char deviceId[DeviceSettings::DEVICE_ID_BUFFER_SIZE];
        if (_deviceSettings->getDeviceId(deviceId, sizeof(deviceId)) && _deviceSettings->isDeviceIdValid()) {
            strncpy(ctx.deviceId, deviceId, sizeof(ctx.deviceId) - 1);
            ctx.deviceId[sizeof(ctx.deviceId) - 1] = '\0';
            ctx.deviceIdValid = true;
        }

        WeeklySchedule schedules[DeviceSettings::MAX_WEEKLY_SCHEDULES];
        _deviceSettings->getWeeklySchedules(schedules);

        char* schedPtr = ctx.postingSchedule;
        size_t schedRemaining = sizeof(ctx.postingSchedule);
        int written = 0;

        for (size_t i = 0; i < DeviceSettings::MAX_WEEKLY_SCHEDULES && schedRemaining > 20; i++) {
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

        ctx.fillDwellTime = _deviceSettings->getFillDwellTime();
        ctx.fillDwellTimeValid = true;
        ctx.lteAttemptTimeout = _deviceSettings->getLteTimeout();
        ctx.lteAttemptTimeoutValid = true;
        ctx.fillPostDeltaValue = _deviceSettings->getFillAlarmDelta();
        ctx.fillPostDeltaValueValid = true;
        ctx.postDwellTime = _deviceSettings->getPostDwellTime();
        ctx.postDwellTimeValid = true;

        ctx.sensorSampleRateMinutes = _deviceSettings->getSensorSampleRateMinutes();
        ctx.sensorSampleRateMinutesValid = true;

        char mqttScheduledPost[DeviceSettings::MQTT_SCHEDULED_POST_BUFFER_SIZE] = {0};
        if (_deviceSettings->getMqttScheduledPost(mqttScheduledPost, sizeof(mqttScheduledPost))) {
            strncpy(ctx.mqttScheduledPost, mqttScheduledPost, sizeof(ctx.mqttScheduledPost) - 1);
            ctx.mqttScheduledPost[sizeof(ctx.mqttScheduledPost) - 1] = '\0';
            ctx.mqttScheduledPostValid = true;
        }

        ctx.eventPosts = _deviceSettings->getEventPosts();
        ctx.eventPostsValid = true;

        char eventThresholds[DeviceSettings::EVENT_THRESHOLDS_BUFFER_SIZE] = {0};
        if (_deviceSettings->getEventThresholdsPct(eventThresholds, sizeof(eventThresholds)) && eventThresholds[0] != '\0') {
            strncpy(ctx.eventThresholdsPct, eventThresholds, sizeof(ctx.eventThresholdsPct) - 1);
            ctx.eventThresholdsPct[sizeof(ctx.eventThresholdsPct) - 1] = '\0';
            ctx.eventThresholdsPctValid = true;
        }
    }

    strncpy(ctx.mqttPort,
            (_awsIotManager != nullptr && _awsIotManager->IsConnectedViaBackup443()) ? "443" : "8883",
            sizeof(ctx.mqttPort) - 1);
    ctx.mqttPort[sizeof(ctx.mqttPort) - 1] = '\0';
    ctx.mqttPortValid = true;

    time_t now;
    time(&now);
    if (now >= 1609459200) {
        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);
        strftime(ctx.dateTimeIso, sizeof(ctx.dateTimeIso), "%Y-%m-%dT%H:%M:%S.000", &timeinfo);
        ctx.dateTimeIsoValid = true;
    }

    snprintf(ctx.mqttSchema, sizeof(ctx.mqttSchema), "%d.%d.%d",
             CONFIG_LOGI_MQTT_VERSION_MAJOR,
             CONFIG_LOGI_MQTT_VERSION_MINOR,
             CONFIG_LOGI_MQTT_VERSION_REVISION);
    ctx.mqttSchemaValid = true;

    snprintf(ctx.deviceVersion, sizeof(ctx.deviceVersion), "%d.%d.%d,0.0,%d.%d.%d",
             CONFIG_LOGI_HARDWARE_VERSION_MAJOR,
             CONFIG_LOGI_HARDWARE_VERSION_MINOR,
             CONFIG_LOGI_HARDWARE_VERSION_REVISION,
             CONFIG_LOGI_SOFTWARE_VERSION_MAJOR,
             CONFIG_LOGI_SOFTWARE_VERSION_MINOR,
             CONFIG_LOGI_SOFTWARE_VERSION_REVISION);
    ctx.deviceVersionValid = true;

    wifi_ap_record_t apInfo;
    if (esp_wifi_sta_get_ap_info(&apInfo) == ESP_OK) {
        ctx.lteSignalQuality = apInfo.rssi;
        ctx.lteSignalQualityValid = true;
        strncpy(ctx.wifiSsid, reinterpret_cast<const char*>(apInfo.ssid), sizeof(ctx.wifiSsid) - 1);
        ctx.wifiSsid[sizeof(ctx.wifiSsid) - 1] = '\0';
        ctx.wifiSsidValid = true;
    }

    ctx.wifiReset = (_parentStateMachine != nullptr) &&
                    _parentStateMachine->isWifiResetRepairModeActive(_timeKeeper ? _timeKeeper->GetCurrentTime() : time(nullptr));
    ctx.wifiResetValid = true;

    ctx.chargerStatus = (_driver != nullptr && _driver->IsChargingErrorActive()) ? 1 : 0;
    ctx.chargerStatusValid = true;
    ctx.resetCounter = LogiResetCounter_Get();
    ctx.resetCounterValid = true;
    Faults_Render(ctx.errorLog, sizeof(ctx.errorLog), nullptr);
    ctx.errorLogValid = true;
}

// ============================================================================
// Credential Management
// ============================================================================

bool ProvisioningStateMachine::hasStoredCredentials()
{
    if (!_nvsStorage) {
        return false;
    }

    char ssid[33] = {0};
    if (!_nvsStorage->LoadString(NVS_WIFI_SSID_KEY, ssid, sizeof(ssid))) {
        return false;
    }

    return strlen(ssid) > 0;
}

void ProvisioningStateMachine::clearStoredCredentials()
{
    if (!_nvsStorage) return;

    ESP_LOGW(TAG, "Clearing stored WiFi credentials");
    backupCurrentCredentials();

    _nvsStorage->SaveString(NVS_WIFI_SSID_KEY, "");
    _nvsStorage->SaveString(NVS_WIFI_PASS_KEY, "");
    _nvsStorage->Commit();
}

void ProvisioningStateMachine::wipeAllCredentials()
{
    // REQ-PROV-01: hard wipe used on power-on reset. Unlike clearStoredCredentials(),
    // this does NOT call backupCurrentCredentials() and ALSO nulls the backup keys,
    // so a subsequent provisioning timeout / cancel can't restore old creds via
    // restoreBackupCredentials(). Result: device boots fully unprovisioned.
    if (!_nvsStorage) return;

    ESP_LOGW(TAG, "Wiping ALL WiFi credentials (primary + backup) per REQ-PROV-01");
    _nvsStorage->SaveString(NVS_WIFI_SSID_KEY, "");
    _nvsStorage->SaveString(NVS_WIFI_PASS_KEY, "");
    _nvsStorage->SaveString(NVS_BACKUP_SSID_KEY, "");
    _nvsStorage->SaveString(NVS_BACKUP_PASS_KEY, "");
    _nvsStorage->Commit();
}

bool ProvisioningStateMachine::saveCredentialsToNvs(const char* ssid, const char* password)
{
    if (!_nvsStorage) {
        ESP_LOGE(TAG, "NVS not available");
        return false;
    }

    bool ssidSaved = _nvsStorage->SaveString(NVS_WIFI_SSID_KEY, ssid);
    bool passSaved = _nvsStorage->SaveString(NVS_WIFI_PASS_KEY, password);
    _nvsStorage->Commit();

    if (ssidSaved && passSaved) {
        ESP_LOGI(TAG, "Saved credentials for SSID: %s", ssid);
        return true;
    }

    ESP_LOGE(TAG, "Failed to save credentials");
    return false;
}

bool ProvisioningStateMachine::loadCredentialsFromNvs(char* ssid, size_t ssidLen,
                                                      char* password, size_t passLen)
{
    if (!_nvsStorage) {
        return false;
    }

    bool gotSsid = _nvsStorage->LoadString(NVS_WIFI_SSID_KEY, ssid, ssidLen);
    bool gotPass = _nvsStorage->LoadString(NVS_WIFI_PASS_KEY, password, passLen);

    return gotSsid && gotPass && strlen(ssid) > 0;
}

void ProvisioningStateMachine::restoreBackupAfterFailedProvisioning()
{
    if (!_provisioningSessionHadNewCredentials) {
        return;
    }

    esp_err_t err = restoreBackupCredentials();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Restored previous Wi-Fi credentials after failed provisioning attempt");
    } else if (err == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "No previous Wi-Fi credentials to restore after failed provisioning attempt");
    } else {
        ESP_LOGW(TAG, "Failed to restore previous Wi-Fi credentials after provisioning failure: %s",
                 esp_err_to_name(err));
    }
}

void ProvisioningStateMachine::quiesceWifiBeforeProvisioningRetry()
{
    // ESP-IDF rejects provisioning-manager reset calls while STA is still
    // connecting/reconnecting. Disconnect the in-flight STA attempt before
    // asking the provisioning manager to clear credentials or reprovision state.
    // Do not stop Wi-Fi here: the provisioning service still needs the driver
    // running for subsequent BLE scan requests.
    esp_err_t err = esp_wifi_disconnect();
    if (err != ESP_OK && err != ESP_ERR_WIFI_NOT_INIT && err != ESP_ERR_WIFI_NOT_STARTED) {
        ESP_LOGW(TAG, "esp_wifi_disconnect before provisioning retry failed: %s",
                 esp_err_to_name(err));
    }

    vTaskDelay(pdMS_TO_TICKS(750));
}

esp_err_t ProvisioningStateMachine::backupCurrentCredentials()
{
    wifi_config_t current_config;
    esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &current_config);

    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No current WiFi configuration found");
        return err;
    }

    if (current_config.sta.ssid[0] != 0 && _nvsStorage) {
        bool ssidSaved = _nvsStorage->SaveString(NVS_BACKUP_SSID_KEY,
                                                  (const char*)current_config.sta.ssid);
        bool passSaved = _nvsStorage->SaveString(NVS_BACKUP_PASS_KEY,
                                                  (const char*)current_config.sta.password);
        _nvsStorage->Commit();

        if (ssidSaved && passSaved) {
            ESP_LOGI(TAG, "Backed up credentials for SSID: %s", current_config.sta.ssid);
            return ESP_OK;
        }
        return ESP_FAIL;
    }

    return ESP_ERR_NOT_FOUND;
}

esp_err_t ProvisioningStateMachine::restoreBackupCredentials()
{
    if (!_nvsStorage) {
        return ESP_ERR_INVALID_STATE;
    }

    char ssid[33] = {0};
    char password[65] = {0};

    if (_nvsStorage->LoadString(NVS_BACKUP_SSID_KEY, ssid, sizeof(ssid)) &&
        _nvsStorage->LoadString(NVS_BACKUP_PASS_KEY, password, sizeof(password))) {

        if (strlen(ssid) > 0) {
            ESP_LOGI(TAG, "Restoring credentials for SSID: %s", ssid);
            return saveCredentialsToNvs(ssid, password) ? ESP_OK : ESP_FAIL;
        }
    }

    ESP_LOGI(TAG, "No backup credentials found");
    return ESP_ERR_NOT_FOUND;
}

// ============================================================================
// Event Handlers
// ============================================================================

bool ProvisioningStateMachine::registerProvisioningEventHandlers()
{
    if (_eventHandlersRegistered) {
        return true;
    }

    esp_err_t err = esp_event_handler_instance_register(
        WIFI_PROV_EVENT, ESP_EVENT_ANY_ID,
        &ProvisioningStateMachine::wifiProvEventHandler, this, &_wifiProvEventInstance);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WIFI_PROV_EVENT handler: %s", esp_err_to_name(err));
        return false;
    }

    err = esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,
        &ProvisioningStateMachine::wifiProvEventHandler, this, &_wifiEventInstance);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register WIFI_EVENT handler: %s", esp_err_to_name(err));
        unregisterProvisioningEventHandlers();
        return false;
    }

    err = esp_event_handler_instance_register(
        PROTOCOMM_TRANSPORT_BLE_EVENT, ESP_EVENT_ANY_ID,
        &ProvisioningStateMachine::wifiProvEventHandler, this, &_protoBleEventInstance);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register PROTOCOMM_TRANSPORT_BLE_EVENT handler: %s", esp_err_to_name(err));
        unregisterProvisioningEventHandlers();
        return false;
    }

    err = esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP,
        &ProvisioningStateMachine::wifiProvEventHandler, this, &_ipEventInstance);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register IP_EVENT handler: %s", esp_err_to_name(err));
        unregisterProvisioningEventHandlers();
        return false;
    }

    _eventHandlersRegistered = true;
    return true;
}

void ProvisioningStateMachine::unregisterProvisioningEventHandlers()
{
    if (_wifiProvEventInstance != nullptr) {
        esp_event_handler_instance_unregister(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, _wifiProvEventInstance);
        _wifiProvEventInstance = nullptr;
    }
    if (_wifiEventInstance != nullptr) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, _wifiEventInstance);
        _wifiEventInstance = nullptr;
    }
    if (_protoBleEventInstance != nullptr) {
        esp_event_handler_instance_unregister(PROTOCOMM_TRANSPORT_BLE_EVENT, ESP_EVENT_ANY_ID, _protoBleEventInstance);
        _protoBleEventInstance = nullptr;
    }
    if (_ipEventInstance != nullptr) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, _ipEventInstance);
        _ipEventInstance = nullptr;
    }
    _eventHandlersRegistered = false;
}

void ProvisioningStateMachine::registerGapEventListener()
{
    if (_gapListenerRegistered) {
        return;
    }

    int rc = ble_gap_event_listener_register(
        &_gapListener,
        &ProvisioningStateMachine::bleGapEventCb,
        this
    );
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to register BLE GAP event listener: %d", rc);
        return;
    }
    _gapListenerRegistered = true;
}

void ProvisioningStateMachine::unregisterGapEventListener()
{
    if (!_gapListenerRegistered) {
        return;
    }
    ble_gap_event_listener_unregister(&_gapListener);
    _gapListenerRegistered = false;
}

void ProvisioningStateMachine::onBleConnectionReceived()
{
    ESP_LOGI(TAG, "BLE connection received — entering provisioning mode");
    _isBluetoothConnected = true;
    noteProvisioningActivity();
}

void ProvisioningStateMachine::wifiProvEventHandler(void* arg, esp_event_base_t eventBase,
                                                    int32_t eventId, void* eventData)
{
    ProvisioningStateMachine* self = static_cast<ProvisioningStateMachine*>(arg);

    if (eventBase == WIFI_EVENT && eventId == WIFI_EVENT_STA_DISCONNECTED && eventData != nullptr) {
        const wifi_event_sta_disconnected_t* disconnected =
            static_cast<const wifi_event_sta_disconnected_t*>(eventData);
        self->_lastStaDisconnectReason = disconnected->reason;
        ESP_LOGI(TAG, "Provisioning Wi-Fi verification disconnect reason: %d",
                 self->_lastStaDisconnectReason);
        return;
    }

    if (eventBase == WIFI_PROV_EVENT) {
        switch (eventId) {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "Provisioning started");
                break;

            case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t* wifi_sta_cfg = (wifi_sta_config_t*)eventData;
                ESP_LOGI(TAG, "Received credentials for SSID: %s", wifi_sta_cfg->ssid);
                self->noteProvisioningActivity();

                strncpy(self->_pendingSsid, (const char*)wifi_sta_cfg->ssid,
                        sizeof(self->_pendingSsid) - 1);
                strncpy(self->_pendingPassword, (const char*)wifi_sta_cfg->password,
                        sizeof(self->_pendingPassword) - 1);

                ESP_LOGI(TAG, "Verifying credentials against Wi-Fi router...");
                self->_lastStaDisconnectReason = 0;
                self->_credentialsReceived = true;
                self->_provisioningSessionHadNewCredentials = true;
                self->transitionTo(ProvisioningState::ProvisioningState_VerifyCredentials);
                break;
            }

            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning credential verification SUCCESS");
                self->noteProvisioningActivity();
                self->transitionTo(ProvisioningState::ProvisioningState_Success);
                break;

            case WIFI_PROV_CRED_FAIL: {
                if (self->_currentState != ProvisioningState::ProvisioningState_VerifyCredentials) {
                    ESP_LOGW(TAG,
                             "Ignoring late provisioning credential failure outside credential verification (state=%d)",
                             static_cast<int>(self->_currentState));
                    break;
                }

                wifi_prov_sta_fail_reason_t* reason =
                    static_cast<wifi_prov_sta_fail_reason_t*>(eventData);

                bool authFailure = reason != nullptr && *reason == WIFI_PROV_STA_AUTH_ERROR;
                bool apNotFound = reason != nullptr && *reason == WIFI_PROV_STA_AP_NOT_FOUND;

                // ESP-IDF v5.5.2 copies wifi_disconnect_reason into the
                // WIFI_PROV_CRED_FAIL event payload before updating it from
                // WIFI_EVENT_STA_DISCONNECTED, so AP-not-found can arrive here
                // mislabeled as auth failure. Prefer the concrete STA reason
                // captured by this state machine when it is available.
                if (isWifiApNotFoundReason(self->_lastStaDisconnectReason)) {
                    authFailure = false;
                    apNotFound = true;
                } else if (isWifiAuthFailureReason(self->_lastStaDisconnectReason)) {
                    authFailure = true;
                    apNotFound = false;
                }

                ESP_LOGE(TAG, "Provisioning failed: %s (sta_reason=%d)",
                         authFailure ? "Authentication failed" :
                         apNotFound ? "AP not found" : "Wi-Fi verification failed",
                         self->_lastStaDisconnectReason);

                self->noteProvisioningActivity();
                self->_resetProvisioningManagerOnFailure = true;
                self->transitionTo(ProvisioningState::ProvisioningState_Failed);
                break;
            }

            case WIFI_PROV_END:
                ESP_LOGI(TAG, "Provisioning ended");
                break;

            case WIFI_PROV_DEINIT:
                ESP_LOGI(TAG, "Provisioning de-initialized");
                break;
        }
    }
}

int ProvisioningStateMachine::bleGapEventCb(struct ble_gap_event* event, void* arg)
{
    ProvisioningStateMachine* self = static_cast<ProvisioningStateMachine*>(arg);

    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                ESP_LOGI(TAG, "BLE connected, handle=%d", event->connect.conn_handle);
                self->_isBluetoothConnected = true;
                self->_bleConnHandle = event->connect.conn_handle;
                self->_bleConnHandleValid = true;
                self->noteProvisioningActivity();
                self->transitionTo(ProvisioningState::ProvisioningState_Connected);
            } else {
                ESP_LOGW(TAG, "BLE connection failed, status=%d", event->connect.status);
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "BLE disconnected, reason=%d", event->disconnect.reason);
            self->_isBluetoothConnected = false;
            self->_bleConnHandleValid = false;
            self->noteProvisioningActivity();
            break;
    }

    return 0;
}
