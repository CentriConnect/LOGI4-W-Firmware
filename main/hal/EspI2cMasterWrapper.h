#ifndef ESP_I2C_MASTER_WRAPPER_H
#define ESP_I2C_MASTER_WRAPPER_H

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "interfaces/II2cHal.h"
#include <stdint.h>
#include <stddef.h>

class EspI2cMasterWrapper : public II2cHal
{
public:
    /// <summary>
    /// Constructor. Stores device config and bus handle. Does not initialize device on bus yet.
    /// Call Initialize() separately.
    /// </summary>
    EspI2cMasterWrapper(i2c_master_bus_handle_t bus_handle, uint16_t device_address, uint32_t clk_speed_hz);

    virtual ~EspI2cMasterWrapper();

    // Delete copy/assignment
    EspI2cMasterWrapper(const EspI2cMasterWrapper &) = delete;
    EspI2cMasterWrapper &operator=(const EspI2cMasterWrapper &) = delete;

    // --- II2cHal Implementation ---
    HalI2cError Initialize() override;
    bool IsInitialized() const override;

    // --- v1.2.1 bus-recovery support ---
    /// <summary>
    /// Removes this device from its bus (best effort) and marks it uninitialized.
    /// Required before the owning bus can be deleted for recovery.
    /// </summary>
    void Detach();

    /// <summary>
    /// Re-binds this wrapper to a (re)created master bus and re-adds the device.
    /// </summary>
    HalI2cError Reattach(i2c_master_bus_handle_t new_bus_handle);
    HalI2cError Read(uint8_t *read_buffer, size_t read_size, int xfer_timeout_ms = 100) override;
    HalI2cError Write(const uint8_t *write_buffer, size_t write_size, int xfer_timeout_ms = 100) override;
    HalI2cError WriteRead(const uint8_t *write_buffer, size_t write_size, uint8_t *read_buffer, size_t read_size, int xfer_timeout_ms = 100) override;

private:
    i2c_master_bus_handle_t _bus_handle; 
    i2c_master_dev_handle_t _dev_handle; 
    uint16_t _device_address;
    uint32_t _clk_speed_hz; 
    bool _initialized;      

    static const char *TAG;

    static HalI2cError MapError(esp_err_t err);
};

#endif // ESP_I2C_MASTER_WRAPPER_H