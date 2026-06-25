#ifndef ESPBLUETOOTHMANAGER_H
#define ESPBLUETOOTHMANAGER_H

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "freertos/timers.h"
#include "sdkconfig.h"

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

    /// Stop advertising (no-op if not advertising)
    void stopAdvertising();

    bool isInitialized() const { return _initialized; }
    bool isAdvertising() const { return _advertising; }

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
    void startAdvertisingInternal();

    // State
    bool _initialized = false;
    bool _running     = false;
    bool _hostSynced  = false;
    bool _advertising = false;

    // BLE inactivity timeout
    TimerHandle_t _inactivityTimer = nullptr;
    uint16_t _connHandle = 0;
    void startInactivityTimer();
    void stopInactivityTimer();
    static void inactivityTimerCb(TimerHandle_t xTimer);

    // Requested name to use for ADV when ready
    char _advName[32] = "Autient-ESP";
    uint32_t _advIntervalMs = 1000;

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
