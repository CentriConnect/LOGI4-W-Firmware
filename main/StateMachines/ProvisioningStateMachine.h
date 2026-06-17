#ifndef PROVISIONING_STATE_MACHINE_H
#define PROVISIONING_STATE_MACHINE_H

#include "StatesCommon.h"
#include "hal/EspNvsStorage.h"
#include "hal/EspBluetoothManager.h"
#include "hal/EspPowerManager.h"
#include "hal/EspTimeKeeper.h"
#include "ILogiHardwareDriver.h"
#include "MQTT/AwsIotManager.h"
#include "logi/DeviceSettings.h"  // REQ-BLE-01: BLE name uses first 4 of DeviceID UUID

#include <esp_err.h>
#include <esp_event.h>
#include <wifi_provisioning/manager.h>
#include "host/ble_gap.h"
#include "sdkconfig.h"

class ApplicationStateMachine;

class ProvisioningStateMachine
{
public:
    static constexpr int64_t PROVISIONING_TIMEOUT_MS =
        static_cast<int64_t>(CONFIG_LOGI_PROVISIONING_TIMEOUT_HOURS) * 3600LL * 1000LL;

    static constexpr int64_t SUCCESS_DISPLAY_MS =
        static_cast<int64_t>(CONFIG_LOGI_PROVISIONING_SUCCESS_DISPLAY_MIN) * 60LL * 1000LL;

    // ISS-FW-020 deadman: no TRANSIENT state (anything except Advertising and
    // SuccessDisplay, which have their own bounded exits) may last this long.
    // A state older than this means a lost event / wedged handshake - reboot
    // (SW reset preserves NVS credentials). 10 min is ~100x any legitimate
    // transient duration. This is the catch-all for wedge modes we have NOT
    // discovered yet; bench-earned 2026-06-12 when a lost prov-manager event
    // left the device camped in VerifyCredentials, dark on USB+JTAG+BLE+WiFi,
    // with a healthy-looking blue LED.
    static constexpr int64_t TRANSIENT_STATE_DEADMAN_MS = 10LL * 60LL * 1000LL;

    static const char* NVS_WIFI_SSID_KEY;
    static const char* NVS_WIFI_PASS_KEY;
    static const char* NVS_BACKUP_SSID_KEY;
    static const char* NVS_BACKUP_PASS_KEY;

    ProvisioningStateMachine(EspNvsStorage* nvsStorage,
                             EspBluetoothManager* bleManager,
                             ILogiHardwareDriver* driver,
                             AwsIotManager* awsIotManager,
                             EspPowerManager* powerManager,
                             EspTimeKeeper* timeKeeper,
                             DeviceSettings* deviceSettings);

    ~ProvisioningStateMachine();

    bool init(ProvisioningMode mode);
    void update();
    void transitionTo(ProvisioningState newState);
    void setParentStateMachine(ApplicationStateMachine* parent);

    bool isComplete() const { return _provisioningComplete; }
    bool wasSuccessful() const { return _provisioningSuccess; }
    bool hasStoredCredentials();
    void clearStoredCredentials();
    void wipeAllCredentials();  // REQ-PROV-01: full wipe (primary + backup), no backup-restore
    void onBleConnectionReceived();
    bool saveCredentialsToNvs(const char* ssid, const char* password);

    ProvisioningStateMachine(const ProvisioningStateMachine&) = delete;
    ProvisioningStateMachine& operator=(const ProvisioningStateMachine&) = delete;

private:
    static const char* TAG;

    ProvisioningState _currentState = ProvisioningState::ProvisioningState_Init;
    ProvisioningMode _mode = ProvisioningMode::FirstBoot;

    bool _provisioningComplete = false;
    bool _provisioningSuccess = false;
    bool _isBluetoothConnected = false;
    bool _credentialsReceived = false;
    bool _initialized = false;

    // Timestamp-based timeout (replaces FreeRTOS timer)
    int64_t _provisioningStartTimeMs = 0;
    int64_t _successDisplayStartMs = 0;
    int64_t _stateEnteredMs = 0; // deadman reference: stamped on every transition

    // REQ-FIRSTBOOT-01: sub-step counter for post -> jobs -> shadow -> post
    // sequence. Each update() tick advances one step so the state machine
    // stays non-blocking.
    uint8_t _firstBootSubStep = 0;
    int64_t _firstBootStepStartMs = 0;

    EspNvsStorage* _nvsStorage;
    EspBluetoothManager* _bleManager;
    ILogiHardwareDriver* _driver;
    AwsIotManager* _awsIotManager;
    EspPowerManager* _powerManager;
    EspTimeKeeper* _timeKeeper;
    DeviceSettings* _deviceSettings;        // REQ-BLE-01 (non-const: REQ-FIRSTBOOT-01 applies shadow values)
    ApplicationStateMachine* _parentStateMachine = nullptr;

    ble_gap_event_listener _gapListener;

    char _pendingSsid[33] = {0};
    char _pendingPassword[65] = {0};

    // State handlers
    void ProvisioningStateInit();
    void ProvisioningStateConnected();
    void ProvisioningStateVerifyCredentials();
    void ProvisioningStateSuccess();
    void ProvisioningStatePostJobsShadow();   // REQ-FIRSTBOOT-01
    void ProvisioningStateSuccessDisplay();
    void ProvisioningStateFailed();
    void ProvisioningStateTimeout();

    // Helpers
    bool isProvisioningTimedOut() const;
    esp_err_t startProvisioningService();
    void stopProvisioningService();
    esp_err_t getDeviceServiceName(char* serviceName, size_t maxLen);
    esp_err_t getDevicePop(char* pop, size_t maxLen);
    bool publishFirstBootTelemetry(AwsIotClient* client);
    void populateFirstBootTelemetryContext(TelemetryContext& context, const LogiSensorData& data) const;

    esp_err_t backupCurrentCredentials();
    esp_err_t restoreBackupCredentials();
    bool loadCredentialsFromNvs(char* ssid, size_t ssidLen, char* password, size_t passLen);

    void registerGapEventListener();
    void unregisterGapEventListener();

    static void wifiProvEventHandler(void* arg, esp_event_base_t eventBase,
                                     int32_t eventId, void* eventData);
    static int bleGapEventCb(struct ble_gap_event* event, void* arg);
};

#endif // PROVISIONING_STATE_MACHINE_H
