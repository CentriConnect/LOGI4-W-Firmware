/**
 * @file RgbLedPartNumberIs31fl3193.h
 * @brief Driver for the IS31FL3193 RGB LED Driver IC.
 * @author mason.melville
 * @date July 18, 2024
 * @updated April 10, 2025
 *
 * @details Datasheet: https://www.lumissil.com/assets/pdf/core/IS31FL3193_DS.pdf
 *
 * This driver utilizes platform-agnostic interfaces:
 * - II2cHal for I2C communication.
 * - IGpioHal for hardware standby pin control.
 * Appropriate HAL wrappers must be instantiated and passed during construction.
 */

#ifndef _RGBLED_IS31FL3193_H_
#define _RGBLED_IS31FL3193_H_

#include "II2cHal.h"  // Include the common I2C HAL interface
#include "IGpioHal.h" // Include the common GPIO HAL interface
#include <stdint.h>
#include <stddef.h>

// Default I2C Address for IS31FL3193 (Confirm with hardware setup)
#define IS31FL3193_DEFAULT_ADDRESS 0x68

// Register definitions (remain the same)
#define IS31FL3193_REG_SHUTDOWN 0x00
#define IS31FL3193_REG_BREATHING 0x01
#define IS31FL3193_REG_LED_MODE 0x02
#define IS31FL3193_REG_OUTPUT_CURRENT 0x03
#define IS31FL3193_REG_PWM_CH0 0x04 // Red
#define IS31FL3193_REG_PWM_CH1 0x05 // Green
#define IS31FL3193_REG_PWM_CH2 0x06 // Blue
#define IS31FL3193_REG_DATA_UPDATE 0x07
#define IS31FL3193_REG_T0_CH0 0x0A
#define IS31FL3193_REG_T0_CH1 0x0B
#define IS31FL3193_REG_T0_CH2 0x0C
#define IS31FL3193_REG_T1T2_CH0 0x10
#define IS31FL3193_REG_T1T2_CH1 0x11
#define IS31FL3193_REG_T1T2_CH2 0x12
#define IS31FL3193_REG_T3T4_CH0 0x16
#define IS31FL3193_REG_T3T4_CH1 0x17
#define IS31FL3193_REG_T3T4_CH2 0x18
#define IS31FL3193_REG_TIME_UPDATE 0x1C

// Breathing mode bit masks
#define IS31FL3193_BREATHING_ENABLE_CH0 0x01
#define IS31FL3193_BREATHING_ENABLE_CH1 0x02
#define IS31FL3193_BREATHING_ENABLE_CH2 0x04

// LED Mode register values (0x02)
#define IS31FL3193_LED_MODE_PWM 0x00
#define IS31FL3193_LED_MODE_BREATHING 0x20
#define IS31FL3193_REG_LED_CONTROL 0x1D
#define IS31FL3193_REG_RESET 0x2F

// Bit masks for register values (remain the same)
#define IS31FL3193_SHUTDOWN_SSD_SOFT_SHUTDOWN 0b00000000
#define IS31FL3193_SHUTDOWN_SSD_NORMAL 0b00100000
#define IS31FL3193_LED_CONTROL_CH0_EN 0b00000001
#define IS31FL3193_LED_CONTROL_CH1_EN 0b00000010
#define IS31FL3193_LED_CONTROL_CH2_EN 0b00000100
#define IS31FL3193_RESET_VALUE 0x00

class RgbLedPartNumberIs31fl3193
{
public:
    /// <summary>
    /// Constructor for the IS31FL3193 driver.
    /// </summary>
    /// <param name="i2c_hal_ref">A reference to an initialized object implementing the II2cHal interface.</param>
    /// <param name="standby_gpio_hal_ref">A reference to an initialized object implementing the IGpioHal interface for the hardware standby pin.</param>
    RgbLedPartNumberIs31fl3193(II2cHal &i2c_hal_ref, IGpioHal &standby_gpio_hal_ref); // Updated constructor

    // --- Public methods remain the same ---
    bool InitializeDevice();
    bool ShutdownDevice();
    bool ResetDevice();
    bool SetGlobalCurrent(uint8_t current_code);
    bool SetRGB(uint8_t R, uint8_t G, uint8_t B);
    bool SetColorCode(uint32_t colorCode);
    bool SetRed(uint8_t R);
    bool SetGreen(uint8_t G);
    bool SetBlue(uint8_t B);
    bool GetLedStandby();
    void SetLedStandby(bool standby);
    bool WriteRegister(uint8_t registerAddress, uint8_t writeValue);
    bool ReadRegister(uint8_t registerAddress, uint8_t &readValue);

    /// <summary>
    /// Configures breathing (blink) mode for all active LED channels.
    /// </summary>
    /// <param name="enable">True to enable breathing mode, false for constant PWM.</param>
    /// <param name="periodMs">Breathing period in milliseconds (approximate).</param>
    /// <returns>true if configuration was successful.</returns>
    bool SetBreathingMode(bool enable, uint16_t periodMs = 1000);

    // Storage for current RGB values (remain the same)
    uint8_t RED = 0;
    uint8_t GREEN = 0;
    uint8_t BLUE = 0;

private:
    II2cHal &i2c;             // Reference to the platform-agnostic I2C HAL
    IGpioHal &ledStandbyGpio; // Reference to the platform-agnostic GPIO HAL

    // --- Private methods remain the same ---
    bool WritePwmChannel(uint8_t channel, uint8_t value);
    bool WriteLedControlRegister(uint8_t value);
    bool TriggerDataUpdate();
    bool TriggerTimeUpdate();
    bool ConfigureBreathingTiming(uint8_t channel, uint8_t t0, uint8_t t1t2, uint8_t t3t4);
    void DelayMs(uint32_t milliseconds);

    // Internal helper to manage delays after register writes if needed
    static const uint32_t REGISTER_WRITE_DELAY_MS = 1; // Small delay after writes
};

#endif /* _RGBLED_IS31FL3193_H_ */