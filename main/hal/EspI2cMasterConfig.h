#ifndef ESP_I2C_MASTER_CONFIG_H
#define ESP_I2C_MASTER_CONFIG_H

#include "driver/i2c_master.h" // For i2c_port_t
#include "driver/gpio.h"       // For gpio_num_t
#include <stdint.h>            // For uint32_t, uint16_t

/// <summary>
/// Configuration structure for the EspI2cMasterWrapper class.
/// Encapsulates parameters needed to initialize an I2C master bus and a specific device on it.
/// </summary>
struct EspI2cMasterConfig
{
    /// <summary>
    /// The I2C port number to use (e.g., I2C_NUM_0).
    /// </summary>
    i2c_port_t port = I2C_NUM_0; // Default Port 0

    /// <summary>
    /// The GPIO pin number assigned to the SDA (Data) line.
    /// Use GPIO_NUM_NC if not used or configured externally.
    /// </summary>
    gpio_num_t sda_io_num = GPIO_NUM_NC;

    /// <summary>
    /// The GPIO pin number assigned to the SCL (Clock) line.
    /// Use GPIO_NUM_NC if not used or configured externally.
    /// </summary>
    gpio_num_t scl_io_num = GPIO_NUM_NC;

    /// <summary>
    /// I2C master clock frequency in Hz. Common values: 100000 (Standard), 400000 (Fast).
    /// </summary>
    uint32_t clk_speed_hz = 400000; // Default Fast Mode

    /// <summary>
    /// Enable internal pull-up resistor for the SDA line. Defaults to true.
    /// </summary>
    bool sda_pullup_en = true;

    /// <summary>
    /// Enable internal pull-up resistor for the SCL line. Defaults to true.
    /// </summary>
    bool scl_pullup_en = true;

    /// <summary>
    /// The 7-bit I2C device address for the target peripheral.
    /// </summary>
    uint16_t device_address = 0;

    // Note: Add other bus or device config options here if needed,
    // e.g., clk_flags, intr_priority, trans_queue_depth
};

#endif // ESP_I2C_MASTER_CONFIG_H