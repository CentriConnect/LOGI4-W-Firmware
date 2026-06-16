// ----- File: main/hal/EspI2cMasterWrapper.cpp ----- REVISED -----
#include "hal/EspI2cMasterWrapper.h" // Adjust path if needed
#include "esp_log.h"

const char *EspI2cMasterWrapper::TAG = "EspI2cDevWrap";

// Constructor: Store config, initialize state
EspI2cMasterWrapper::EspI2cMasterWrapper(i2c_master_bus_handle_t bus_handle, uint16_t device_address, uint32_t clk_speed_hz)
    : _bus_handle(bus_handle),
      _dev_handle(NULL),
      _device_address(device_address),
      _clk_speed_hz(clk_speed_hz), // Store clock speed
      _initialized(false)
{
    ESP_LOGD(TAG, "Constructor called for device 0x%X. Call Initialize() next.", _device_address);
    if (_bus_handle == NULL)
    {
        ESP_LOGE(TAG, "Constructor failed: Provided bus handle is NULL for device 0x%X", _device_address);
        // Cannot proceed without bus handle, Initialize() will fail later.
    }
}

// Destructor: Remove device if initialized
EspI2cMasterWrapper::~EspI2cMasterWrapper()
{
    ESP_LOGD(TAG, "Destructor called for device 0x%X", _device_address);
    if (_dev_handle != NULL)
    {
        ESP_LOGI(TAG, "Removing I2C device 0x%X from bus.", _device_address);
        esp_err_t err = i2c_master_bus_rm_device(_dev_handle);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to remove I2C device 0x%X: %s", _device_address, esp_err_to_name(err));
        }
        _dev_handle = NULL; // Mark as removed
    }
    _initialized = false; // Ensure state reflects removal/destruction
    // Do NOT delete _bus_handle here, it's owned elsewhere (e.g., factory)
}

// Initialize method: Add device to bus
HalI2cError EspI2cMasterWrapper::Initialize()
{
    if (_initialized)
    {
        ESP_LOGW(TAG, "Already initialized (Device 0x%X).", _device_address);
        return HAL_I2C_OK;
    }
    if (_bus_handle == NULL)
    {
        ESP_LOGE(TAG, "Cannot initialize: Bus handle is NULL for device 0x%X", _device_address);
        return MapError(ESP_ERR_INVALID_STATE); // Or a specific HAL error
    }

    ESP_LOGI(TAG, "Initializing I2C Device 0x%X by adding to bus...", _device_address);

    i2c_device_config_t dev_conf = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = _device_address,
        .scl_speed_hz = _clk_speed_hz,
    };

    // Ensure device handle is null before adding
    if (_dev_handle != NULL)
    {
        ESP_LOGW(TAG, "Device handle not NULL before Initialize, attempting removal first.");
        i2c_master_bus_rm_device(_dev_handle);
        _dev_handle = NULL;
    }

    esp_err_t err = i2c_master_bus_add_device(_bus_handle, &dev_conf, &_dev_handle);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to add I2C device 0x%X: %s", _device_address, esp_err_to_name(err));
        _dev_handle = NULL; // Ensure handle is NULL on failure
        _initialized = false;
        return MapError(err);
    }

    _initialized = true;
    ESP_LOGI(TAG, "I2C Device Wrapper initialized successfully for device 0x%X.", _device_address);
    return HAL_I2C_OK;
}

bool EspI2cMasterWrapper::IsInitialized() const
{
    // Initialized means Initialize() succeeded and we have a device handle
    return _initialized && (_dev_handle != NULL);
}

// --- v1.2.1 bus-recovery support ---

void EspI2cMasterWrapper::Detach()
{
    if (_dev_handle != NULL)
    {
        esp_err_t err = i2c_master_bus_rm_device(_dev_handle);
        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "Detach: rm_device failed for 0x%X: %s (continuing)", _device_address, esp_err_to_name(err));
        }
        _dev_handle = NULL;
    }
    _initialized = false;
}

HalI2cError EspI2cMasterWrapper::Reattach(i2c_master_bus_handle_t new_bus_handle)
{
    Detach();
    _bus_handle = new_bus_handle;
    return Initialize();
}

// --- Error Mapping ---
HalI2cError EspI2cMasterWrapper::MapError(esp_err_t err)
{
    switch (err)
    {
    case ESP_OK:
        return HAL_I2C_OK;
    case ESP_ERR_INVALID_ARG:
        return HAL_I2C_ERR_INVALID_ARG;
    case ESP_ERR_INVALID_STATE:
        return HAL_I2C_ERR_INVALID_STATE;
    case ESP_ERR_TIMEOUT:
        return HAL_I2C_ERR_TIMEOUT;
    case ESP_ERR_NOT_FOUND: // NACK is often mapped to NOT_FOUND or FAIL
    case ESP_FAIL:
    default:
        return HAL_I2C_ERR_FAIL;
        // Consider specific mapping for HAL_I2C_ERR_BUSY, HAL_I2C_ERR_NACK if ESP provides them
        // Also map init failures if needed: return HAL_I2C_ERR_INIT_FAILED;
    }
}

// --- II2cHal Method Implementations --- (Check IsInitialized())

HalI2cError EspI2cMasterWrapper::Read(uint8_t *read_buffer, size_t read_size, int xfer_timeout_ms)
{
    if (!IsInitialized())
    {
        ESP_LOGE(TAG, "Read attempted on uninitialized I2C device 0x%X. _initialized=%d, _dev_handle=%p",
                 _device_address, _initialized, (void*)_dev_handle);
        return MapError(ESP_ERR_INVALID_STATE);
    }
    if (read_buffer == NULL || read_size == 0)
    {
        return MapError(ESP_ERR_INVALID_ARG);
    }

    esp_err_t err = i2c_master_receive(_dev_handle, read_buffer, read_size, xfer_timeout_ms);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C Read Failed (Dev 0x%X): %s", _device_address, esp_err_to_name(err));
    }
    return MapError(err);
}

HalI2cError EspI2cMasterWrapper::Write(const uint8_t *write_buffer, size_t write_size, int xfer_timeout_ms)
{
    if (!IsInitialized())
    {
        ESP_LOGE(TAG, "Write attempted on uninitialized I2C device 0x%X. _initialized=%d, _dev_handle=%p",
                 _device_address, _initialized, (void*)_dev_handle);
        return MapError(ESP_ERR_INVALID_STATE);
    }
    if (write_buffer == NULL || write_size == 0)
    {
        return MapError(ESP_ERR_INVALID_ARG);
    }

    esp_err_t err = i2c_master_transmit(_dev_handle, write_buffer, write_size, xfer_timeout_ms);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C Write Failed (Dev 0x%X): %s", _device_address, esp_err_to_name(err));
    }
    return MapError(err);
}

HalI2cError EspI2cMasterWrapper::WriteRead(const uint8_t *write_buffer, size_t write_size, uint8_t *read_buffer, size_t read_size, int xfer_timeout_ms)
{
    if (!IsInitialized())
    {
        ESP_LOGE(TAG, "WriteRead attempted on uninitialized I2C device 0x%X.", _device_address);
        return MapError(ESP_ERR_INVALID_STATE);
    }
    if (write_buffer == NULL || write_size == 0 || read_buffer == NULL || read_size == 0)
    {
        return MapError(ESP_ERR_INVALID_ARG);
    }

    esp_err_t err = i2c_master_transmit_receive(_dev_handle, write_buffer, write_size, read_buffer, read_size, xfer_timeout_ms);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "I2C WriteRead Failed (Dev 0x%X): %s", _device_address, esp_err_to_name(err));
    }
    return MapError(err);
}