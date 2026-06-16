#include "EspBluetoothManager.h"

#include "nvs_flash.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "EspBluetoothManager";

EspBluetoothManager* EspBluetoothManager::s_instance = nullptr;

EspBluetoothManager::EspBluetoothManager() = default;

void EspBluetoothManager::init()
{
    if (_initialized)
    {
        ESP_LOGI(TAG, "Already initialized.");
        return;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    // Register instance for static callbacks
    s_instance = this;

    int rc = nimble_port_init();
    if (rc != 0)
    {
        ESP_LOGE(TAG, "nimble_port_init() failed (rc=%d)", rc);
        ESP_ERROR_CHECK(rc);
    }

    // Hook NimBLE host callbacks
    ble_hs_cfg.reset_cb = &EspBluetoothManager::onResetTrampoline;
    ble_hs_cfg.sync_cb  = &EspBluetoothManager::onSyncTrampoline;

    // Init standard GAP service so name helpers work
    ble_svc_gap_init();

    _initialized = true;
    ESP_LOGI(TAG, "NimBLE initialized.");
}

void EspBluetoothManager::startHost()
{
    if (!_initialized)
    {
        ESP_LOGE(TAG, "Call init() before startHost().");
        return;
    }
    if (_running)
    {
        ESP_LOGI(TAG, "Host already running.");
        return;
    }

    nimble_port_freertos_init(&EspBluetoothManager::hostTask);
    _running = true;
    ESP_LOGI(TAG, "NimBLE host task started.");
}

void EspBluetoothManager::stopHost()
{
    if (!_running)
    {
        return;
    }
    nimble_port_stop();
    ESP_LOGI(TAG, "Stopping NimBLE host...");
    _running = false;
}

void EspBluetoothManager::deinit()
{
    if (!_initialized)
    {
        return;
    }
    if (_running)
    {
        stopHost();
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    nimble_port_deinit();
    _initialized = false;
    _hostSynced  = false;
    _advertising = false;

    ESP_LOGI(TAG, "NimBLE deinitialized.");
}

EspBluetoothManager::~EspBluetoothManager()
{
    stopInactivityTimer();
    stopHost();
    deinit();
}

void EspBluetoothManager::startInactivityTimer()
{
    uint32_t timeout_ms = CONFIG_LOGI_BLE_INACTIVITY_TIMEOUT_MIN * 60 * 1000;

    if (_inactivityTimer == nullptr)
    {
        _inactivityTimer = xTimerCreate(
            "ble_inact",
            pdMS_TO_TICKS(timeout_ms),
            pdFALSE,
            this,
            &EspBluetoothManager::inactivityTimerCb
        );
    }

    if (_inactivityTimer != nullptr)
    {
        xTimerReset(_inactivityTimer, 0);
        ESP_LOGI(TAG, "BLE inactivity timer started (%lu min)", (unsigned long)CONFIG_LOGI_BLE_INACTIVITY_TIMEOUT_MIN);
    }
}

void EspBluetoothManager::stopInactivityTimer()
{
    if (_inactivityTimer != nullptr)
    {
        xTimerStop(_inactivityTimer, 0);
        xTimerDelete(_inactivityTimer, 0);
        _inactivityTimer = nullptr;
    }
}

void EspBluetoothManager::inactivityTimerCb(TimerHandle_t xTimer)
{
    auto* self = static_cast<EspBluetoothManager*>(pvTimerGetTimerID(xTimer));
    if (self)
    {
        ESP_LOGW(TAG, "BLE inactivity timeout — dropping connection (handle=%d)", self->_connHandle);
        ble_gap_terminate(self->_connHandle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

// ===== Public ADV API =====

bool EspBluetoothManager::startAdvertising(const char* name)
{
    if (name && name[0] != '\0')
    {
        // Clamp & copy (keep it simple)
        strncpy(_advName, name, sizeof(_advName) - 1);
        _advName[sizeof(_advName) - 1] = '\0';
    }

    if (!_initialized)
    {
        ESP_LOGE(TAG, "BLE not initialized.");
        return false;
    }

    if (_hostSynced)
    {
        // We can start right away
        if (setAdvData(_advName) != 0)
        {
            ESP_LOGE(TAG, "Failed to set ADV data.");
            return false;
        }
        startAdvertisingInternal();
        _advertising = true;
        return true;
    }
    else
    {
        // Defer until onSync()
        ESP_LOGI(TAG, "Host not synced yet; will start advertising when ready.");
        return true; // request accepted
    }
}

void EspBluetoothManager::stopAdvertising()
{
    if (!_advertising)
    {
        return;
    }
    ble_gap_adv_stop();
    _advertising = false;
}

// ===== Host task =====

void EspBluetoothManager::hostTask(void* /*param*/)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
    vTaskDelete(NULL);
}

// ===== NimBLE callbacks (trampolines) =====

void EspBluetoothManager::onResetTrampoline(int reason)
{
    if (s_instance) s_instance->onReset(reason);
}

void EspBluetoothManager::onSyncTrampoline()
{
    if (s_instance) s_instance->onSync();
}

// ===== Instance handlers =====

void EspBluetoothManager::onReset(int reason)
{
    ESP_LOGW(TAG, "NimBLE reset; reason=%d", reason);
    _hostSynced = false;
    _advertising = false;
}

void EspBluetoothManager::onSync()
{
    _hostSynced = true;

    // Choose address type
    ble_hs_id_infer_auto(0, &_ownAddrType);

    // Set the GAP device name (also used by setAdvData below)
    ble_svc_gap_device_name_set(_advName);

    // If someone requested ADV earlier, kick it off now
    if (setAdvData(_advName) == 0)
    {
        startAdvertisingInternal();
        _advertising = true;
    }
}

// ===== GAP helpers =====

int EspBluetoothManager::setAdvData(const char* name)
{
    struct ble_hs_adv_fields f;
    memset(&f, 0, sizeof(f));

    f.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    f.name = (uint8_t*)name;
    f.name_len = (uint8_t)strlen(name);
    f.name_is_complete = 1;

    f.tx_pwr_lvl_is_present = 1;
    f.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    return ble_gap_adv_set_fields(&f);
}

void EspBluetoothManager::startAdvertisingInternal()
{
    struct ble_gap_adv_params ap;
    memset(&ap, 0, sizeof(ap));

    ap.conn_mode = BLE_GAP_CONN_MODE_UND;
    ap.disc_mode = BLE_GAP_DISC_MODE_GEN;
    // REQ-PROV-02: 1000 ms BLE ADV interval during prov window (matches the
    // power study sweet spot of ~3.79 mA / ~111 days at 1000 ms with light
    // sleep enabled). Was 100-150 ms. BLE units = 0.625 ms; 1000 ms = 0x0640.
    // Per Nick 2026-06-01 #6. Shadow ble_adv_time override (PDEC-013) not yet wired.
    ap.itvl_min  = 0x0640; // 1000 ms
    ap.itvl_max  = 0x0640; // 1000 ms

    ble_gap_adv_start(_ownAddrType, NULL, BLE_HS_FOREVER, &ap,
                      &EspBluetoothManager::gapEventCb, this);
}

// GAP event handler
int EspBluetoothManager::gapEventCb(struct ble_gap_event* event, void* arg)
{
    auto* self = static_cast<EspBluetoothManager*>(arg);

    switch (event->type)
    {
        case BLE_GAP_EVENT_CONNECT:
        {
            if (event->connect.status == 0)
            {
                ESP_LOGI(TAG, "BLE connection established, handle=%d", event->connect.conn_handle);
                if (self)
                {
                    self->_connHandle = event->connect.conn_handle;
                    self->startInactivityTimer();
                    if (self->_connectionCallback)
                    {
                        self->_connectionCallback();
                    }
                }
            }
            else
            {
                ESP_LOGW(TAG, "BLE connection failed, status=%d", event->connect.status);
                if (self) self->startAdvertisingInternal();
            }
            return 0;
        }

        case BLE_GAP_EVENT_DISCONNECT:
        {
            if (self) self->stopInactivityTimer();
            if (self) self->startAdvertisingInternal();
            return 0;
        }

        case BLE_GAP_EVENT_MTU:
        case BLE_GAP_EVENT_CONN_UPDATE:
        case BLE_GAP_EVENT_PHY_UPDATE_COMPLETE:
        case BLE_GAP_EVENT_SUBSCRIBE:
        case BLE_GAP_EVENT_PASSKEY_ACTION:
        default:
        {
            return 0;
        }
    }
}
