#ifndef ESPBLUETOOTHMANAGER_H
#define ESPBLUETOOTHMANAGER_H

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "freertos/timers.h"
#include "sdkconfig.h"

#include <cstddef>
#include <cstdint>
#include <functional>

class EspBluetoothManager
{
public:
    /// Callback type for BLE connection events
    using ConnectionCallback = std::function<void()>;

    EspBluetoothManager();

    void init();
    void startHost();
    void stopHost();
    void deinit();

    /// Start advertising with a device name (safe to call before host sync; it will defer)
    bool startAdvertising(const char* name, uint32_t intervalMs = 1000);

    /// Start advertising with custom manufacturer data and optional scan response.
    bool startAdvertisingWithManufacturerData(const char* name,
                                              uint32_t intervalMs,
                                              const uint8_t* manufacturerData,
                                              size_t manufacturerDataLen,
                                              const uint8_t* scanResponseManufacturerData = nullptr,
                                              size_t scanResponseManufacturerDataLen = 0,
                                              bool connectable = true);

    /// Stop advertising (no-op if not advertising)
    void stopAdvertising();

    /// Drop an active repair-trigger connection before switching BLE owners.
    void disconnect(uint8_t reason = BLE_ERR_REM_USER_CONN_TERM);

    bool isInitialized() const { return _initialized; }
    bool isAdvertising() const { return _advertising || _advertisingRequested; }
    bool isConnected() const { return _connected; }

    /// Register a callback to be invoked when a BLE connection is received.
    /// Used to trigger provisioning mode from ApplicationStateMachine.
    void setConnectionCallback(ConnectionCallback callback) { _connectionCallback = callback; }

    ~EspBluetoothManager();

    EspBluetoothManager(const EspBluetoothManager&) = delete;
    EspBluetoothManager& operator=(const EspBluetoothManager&) = delete;
    EspBluetoothManager(EspBluetoothManager&&) = delete;
    EspBluetoothManager& operator=(EspBluetoothManager&&) = delete;

private:
    // Host task
    static void hostTask(void*);

    // NimBLE callbacks (static trampolines -> instance)
    static void onResetTrampoline(int reason);
    static void onSyncTrampoline();

    // Instance handlers
    void onReset(int reason);
    void onSync();

    // GAP helpers
    int  setAdvData(const char* name);
    int  setManufacturerAdvData(const char* name,
                                const uint8_t* manufacturerData,
                                size_t manufacturerDataLen,
                                const uint8_t* scanResponseManufacturerData,
                                size_t scanResponseManufacturerDataLen);
    void startAdvertisingInternal();

    // State
    bool _initialized = false;
    bool _running     = false;
    bool _hostSynced  = false;
    bool _advertising = false;
    bool _advertisingRequested = false;
    bool _connected = false;

    // BLE inactivity timeout
    TimerHandle_t _inactivityTimer = nullptr;
    uint16_t _connHandle = 0;
    void startInactivityTimer();
    void stopInactivityTimer();
    static void inactivityTimerCb(TimerHandle_t xTimer);

    // Requested name to use for ADV when ready
    char _advName[32] = "Autient-ESP";
    uint32_t _advIntervalMs = 1000;
    bool _advConnectable = true;
    bool _useManufacturerAdvData = false;
    uint8_t _manufacturerAdvData[26] = {0};
    size_t _manufacturerAdvDataLen = 0;
    uint8_t _scanResponseManufacturerData[29] = {0};
    size_t _scanResponseManufacturerDataLen = 0;

    // Own address type chosen at runtime
    uint8_t _ownAddrType = 0;

    // Callback for BLE connection events (triggers provisioning mode)
    ConnectionCallback _connectionCallback;

    // Global “current instance” pointer for static callbacks
    static EspBluetoothManager* s_instance;

    // GAP event handler
    static int gapEventCb(struct ble_gap_event* event, void* arg);
};

#endif // ESPBLUETOOTHMANAGER_H
