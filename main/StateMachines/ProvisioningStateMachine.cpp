#include "ProvisioningStateMachine.h"
#include "ApplicationStateMachine.h"

#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_timer.h>
#include <esp_sleep.h>
#include <wifi_provisioning/scheme_ble.h>
#include <ctime>
#include <cstring>
#include "logi/ResetCounter.h"

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
    _provisioningStartTimeMs = 0;
    _successDisplayStartMs = 0;
    _stateEnteredMs = esp_timer_get_time() / 1000; // arm deadman from init

    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_PROV_EVENT, ESP_EVENT_ANY_ID,
        &ProvisioningStateMachine::wifiProvEventHandler, this, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID,
        &ProvisioningStateMachine::wifiProvEventHandler, this, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        PROTOCOMM_TRANSPORT_BLE_EVENT, ESP_EVENT_ANY_ID,
        &ProvisioningStateMachine::wifiProvEventHandler, this, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP,
        &ProvisioningStateMachine::wifiProvEventHandler, this, NULL));

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
    // (bounded by the 48 h timeout) and SuccessDisplay (bounded by its 30 min
    // window) are exempt; everything else older than the deadman means a lost
    // event or wedged handshake -> reboot. SW reset preserves credentials, so
    // a provisioned device lands in duty cycle and an unprovisioned one
    // re-enters a fresh provisioning window.
    if (_currentState != ProvisioningState::ProvisioningState_Advertising &&
        _currentState != ProvisioningState::ProvisioningState_SuccessDisplay) {
        int64_t in_state_ms = (esp_timer_get_time() / 1000) - _stateEnteredMs;
        if (_stateEnteredMs != 0 && in_state_ms > TRANSIENT_STATE_DEADMAN_MS) {
            ESP_LOGE(TAG, "DEADMAN: state %d stuck for %lld ms (limit %lld) - rebooting (creds preserved)",
                     static_cast<int>(_currentState), in_state_ms, TRANSIENT_STATE_DEADMAN_MS);
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
                transitionTo(ProvisioningState::ProvisioningState_Timeout);
                break;
            }
            ProvisioningStateConnected();
            break;

        case ProvisioningState::ProvisioningState_VerifyCredentials:
            ESP_LOGI(TAG, "State: VerifyCredentials");
            // ISS-FW-020: this state camps on an event that can be lost (seen
            // 2026-06-12 under prov-window light sleep) - without a timeout
            // check the device is unreachable forever (no adv, no USB, JTAG
            // dead in light sleep). The 48 h timeout must escape EVERY
            // non-terminal state.
            if (isProvisioningTimedOut()) {
                transitionTo(ProvisioningState::ProvisioningState_Timeout);
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

// ============================================================================
// State Handlers
// ============================================================================

void ProvisioningStateMachine::ProvisioningStateInit()
{
    // Blue LED blink during provisioning
    if (_driver) {
        _driver->SetLedState(LedState::LedState_BlueBlink);
    }

    // Backup existing credentials before starting (Req #7)
    backupCurrentCredentials();

    esp_err_t err = startProvisioningService();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start provisioning service: %s", esp_err_to_name(err));
        transitionTo(ProvisioningState::ProvisioningState_Failed);
        return;
    }

    // Record start time for 48hr timeout
    _provisioningStartTimeMs = esp_timer_get_time() / 1000;

    ESP_LOGI(TAG, "Provisioning started with %d hour timeout",
             CONFIG_LOGI_PROVISIONING_TIMEOUT_HOURS);

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
            transitionTo(ProvisioningState::ProvisioningState_Failed);
            return;
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
            transitionTo(ProvisioningState::ProvisioningState_Failed);
            return;
        }
    }

    ESP_LOGI(TAG, "Testing AWS IoT connection...");
    if (_awsIotManager && _awsIotManager->Connect()) {
        ESP_LOGI(TAG, "AWS IoT connection verified — keeping connection for first-boot sequence");
    } else {
        ESP_LOGE(TAG, "AWS IoT connection failed — staying in provisioning");
        transitionTo(ProvisioningState::ProvisioningState_Failed);
        return;
    }

    // All three checks passed: creds received, AWS connected, NVS write succeeded
    stopProvisioningService();

    _provisioningComplete = true;
    _provisioningSuccess = true;

    // Green LED blink starts now (covers both the first-boot sequence and the
    // 30-min display window — single transition for the user-visible cue).
    ESP_LOGI(TAG, "Provisioning successful — running first-boot sequence then %d-min display",
             CONFIG_LOGI_PROVISIONING_SUCCESS_DISPLAY_MIN);

    // Green LED on from AWS-connect so it blinks 1 Hz for the WHOLE window (the
    // first-boot cycle + the 30-min success-display hold), per the activation spec.
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
            // V13-010: sample ONCE here and cache it; the [4/4] confirm post
            // reuses this snapshot (re-measuring back-to-back makes the 2nd
            // fuel read fail -> ful/raw/supv zeros).
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
            if (shadow.fill_dwell_time != 0) _deviceSettings->setFillDwellTime(shadow.fill_dwell_time);
            if (shadow.lte_timeout      != 0) _deviceSettings->setLteTimeout(shadow.lte_timeout);
            if (shadow.fill_alarm_delta != 0) _deviceSettings->setFillAlarmDelta(shadow.fill_alarm_delta);
            if (shadow.post_dwell_time  != 0) _deviceSettings->setPostDwellTime(shadow.post_dwell_time);
            if (shadow.ble_adv_time     != 0) _deviceSettings->setBleAdvTime(shadow.ble_adv_time);
            ESP_LOGI(TAG, "REQ-FIRSTBOOT-01 [2/4]: shadow applied");
        } else {
            ESP_LOGW(TAG, "REQ-FIRSTBOOT-01 [2/4]: shadow fetch %s",
                     ok ? "ok but no DeviceSettings to apply" : "FAILED");
        }
        _firstBootSubStep = 2;
        break;
    }

    case 2: {
        ESP_LOGI(TAG, "REQ-FIRSTBOOT-01 [3/4]: request pending jobs");
        if (jobs) {
            bool ok = jobs->RequestPendingJobs();
            ESP_LOGI(TAG, "REQ-FIRSTBOOT-01 [3/4]: jobs %s", ok ? "requested" : "FAILED");
        } else {
            ESP_LOGW(TAG, "REQ-FIRSTBOOT-01 [3/4]: no jobs handler — skipped");
        }
        _firstBootSubStep = 3;
        break;
    }

    case 3: {
        ESP_LOGI(TAG, "REQ-FIRSTBOOT-01 [4/4]: confirming POST after shadow apply");
        if (client) {
            bool ok = publishFirstBootTelemetry(client);
            ESP_LOGI(TAG, "REQ-FIRSTBOOT-01 [4/4]: POST %s", ok ? "ok" : "FAILED");
        }
        _firstBootSubStep = 4;
        break;
    }

    case 4:
    default: {
        ESP_LOGI(TAG, "REQ-FIRSTBOOT-01: sequence complete — disconnecting AWS");
        _awsIotManager->Disconnect();
        _successDisplayStartMs = esp_timer_get_time() / 1000;
        if (_driver) {
            _driver->SetLedState(LedState::LedState_GreenBlink);
        }
        transitionTo(ProvisioningState::ProvisioningState_SuccessDisplay);
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

        if (_powerManager) {
            _powerManager->Reboot();
        } else {
            esp_restart();
        }
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
}

void ProvisioningStateMachine::ProvisioningStateFailed()
{
    ESP_LOGW(TAG, "Credential verification failed — allowing retry");

    // Return to blue blink (Req #4: failed credentials keep device in provisioning)
    if (_driver) {
        _driver->SetLedState(LedState::LedState_BlueBlink);
    }

    wifi_prov_mgr_reset_sm_state_on_failure();

    _credentialsReceived = false;

    if (_isBluetoothConnected) {
        transitionTo(ProvisioningState::ProvisioningState_Connected);
    } else {
        transitionTo(ProvisioningState::ProvisioningState_Advertising);
    }
}

void ProvisioningStateMachine::ProvisioningStateTimeout()
{
    ESP_LOGW(TAG, "Provisioning timeout reached (%d hours)",
             CONFIG_LOGI_PROVISIONING_TIMEOUT_HOURS);

    stopProvisioningService();

    if (_driver) {
        _driver->SetLedState(LedState::LedState_Off);
    }

    _provisioningComplete = true;
    _provisioningSuccess = false;

    if (hasStoredCredentials()) {
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
        // No valid creds — deep sleep indefinitely (Req #6)
        ESP_LOGW(TAG, "No valid credentials — entering indefinite deep sleep");
        vTaskDelay(pdMS_TO_TICKS(100));

        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        esp_deep_sleep_start();
    }
}

// ============================================================================
// Provisioning Service Management
// ============================================================================

esp_err_t ProvisioningStateMachine::startProvisioningService()
{
    ESP_LOGI(TAG, "Starting WiFi provisioning service");
    // REQ-PROV-02 (v1.3.0): auto light sleep during the 48 h prov window with
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

    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM
    };

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

    // V13-010: REUSE the sensor snapshot taken at the [1/4] ack post instead of
    // re-measuring (two back-to-back UpdateMeasurements zero the 2nd fuel read).
    // GPS is the exception: merge the latest fix here so the final/confirm post
    // carries coordinates if one was acquired during shadow/jobs (0 if not),
    // then power GNSS down. Shadow-applied settings come via the rebuilt context.
    if (_driver) {
        _driver->GetGpsData(_firstBootSensorSnapshot.GPSData);
        _driver->SetGnssPower(false);
    }

    TelemetryContext context{};
    populateFirstBootTelemetryContext(context, _firstBootSensorSnapshot);
    return client->PublishTelemetryLogi4Format(_firstBootSensorSnapshot, context);
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
        ctx.bleAdvTime = _deviceSettings->getBleAdvTime();
        ctx.bleAdvTimeValid = true;
        ctx.fillPostDeltaValue = _deviceSettings->getFillAlarmDelta();
        ctx.fillPostDeltaValueValid = true;
        ctx.postDwellTime = _deviceSettings->getPostDwellTime();
        ctx.postDwellTimeValid = true;
    }

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

    snprintf(ctx.deviceVersion, sizeof(ctx.deviceVersion), "%d.%d,%d.%d,%d.%d.%d",
             CONFIG_LOGI_HARDWARE_VERSION_MAJOR,
             CONFIG_LOGI_HARDWARE_VERSION_MINOR,
             CONFIG_LOGI_SOFTWARE_VERSION_MAJOR,
             CONFIG_LOGI_SOFTWARE_VERSION_MINOR,
             CONFIG_LOGI_MQTT_VERSION_MAJOR,
             CONFIG_LOGI_MQTT_VERSION_MINOR,
             CONFIG_LOGI_MQTT_VERSION_REVISION);
    ctx.deviceVersionValid = true;

    wifi_ap_record_t apInfo;
    if (esp_wifi_sta_get_ap_info(&apInfo) == ESP_OK) {
        ctx.lteSignalQuality = apInfo.rssi;
        ctx.lteSignalQualityValid = true;
    }

    ctx.chargerStatus = data.PublishedBatteryLevel;
    ctx.chargerStatusValid = true;
    ctx.resetCounter = LogiResetCounter_Get();
    ctx.resetCounterValid = true;
    ctx.errorLog[0] = '\0';
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

void ProvisioningStateMachine::registerGapEventListener()
{
    int rc = ble_gap_event_listener_register(
        &_gapListener,
        &ProvisioningStateMachine::bleGapEventCb,
        this
    );
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to register BLE GAP event listener: %d", rc);
    }
}

void ProvisioningStateMachine::unregisterGapEventListener()
{
    ble_gap_event_listener_unregister(&_gapListener);
}

void ProvisioningStateMachine::onBleConnectionReceived()
{
    ESP_LOGI(TAG, "BLE connection received — entering provisioning mode");
    _isBluetoothConnected = true;
}

void ProvisioningStateMachine::wifiProvEventHandler(void* arg, esp_event_base_t eventBase,
                                                    int32_t eventId, void* eventData)
{
    ProvisioningStateMachine* self = static_cast<ProvisioningStateMachine*>(arg);

    if (eventBase == WIFI_PROV_EVENT) {
        switch (eventId) {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "Provisioning started");
                break;

            case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t* wifi_sta_cfg = (wifi_sta_config_t*)eventData;
                ESP_LOGI(TAG, "Received credentials for SSID: %s", wifi_sta_cfg->ssid);

                strncpy(self->_pendingSsid, (const char*)wifi_sta_cfg->ssid,
                        sizeof(self->_pendingSsid) - 1);
                strncpy(self->_pendingPassword, (const char*)wifi_sta_cfg->password,
                        sizeof(self->_pendingPassword) - 1);

                self->_credentialsReceived = true;
                self->transitionTo(ProvisioningState::ProvisioningState_VerifyCredentials);
                break;
            }

            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning credential verification SUCCESS");
                self->transitionTo(ProvisioningState::ProvisioningState_Success);
                break;

            case WIFI_PROV_CRED_FAIL: {
                wifi_prov_sta_fail_reason_t* reason =
                    static_cast<wifi_prov_sta_fail_reason_t*>(eventData);
                ESP_LOGE(TAG, "Provisioning failed: %s",
                         (*reason == WIFI_PROV_STA_AUTH_ERROR)
                         ? "Authentication failed"
                         : "AP not found");

                wifi_prov_mgr_reset_sm_state_on_failure();
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
                self->transitionTo(ProvisioningState::ProvisioningState_Connected);
            } else {
                ESP_LOGW(TAG, "BLE connection failed, status=%d", event->connect.status);
            }
            break;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "BLE disconnected, reason=%d", event->disconnect.reason);
            self->_isBluetoothConnected = false;
            break;
    }

    return 0;
}
