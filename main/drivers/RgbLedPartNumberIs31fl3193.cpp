/**
 * @file RgbLedPartNumberIs31fl3193.cpp
 * @brief Implementation file for the IS31FL3193 RGB LED Driver IC using generic HAL interfaces.
 * @author mason.melville
 * @date July 17, 2024
 * @updated April 10, 2025
 */

#include "RgbLedPartNumberIs31fl3193.h"

// Platform-specific includes for logging and delays (remain the same)
#ifdef __ZEPHYR__
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
LOG_MODULE_REGISTER(IS31FL3193_Drv, CONFIG_LOG_DEFAULT_LEVEL);
#define IS31FL3193_LOG_ERR(...) LOG_ERR(__VA_ARGS__)
#define IS31FL3193_LOG_WRN(...) LOG_WRN(__VA_ARGS__)
#define IS31FL3193_LOG_DBG(...) LOG_DBG(__VA_ARGS__)
#elif defined(ESP_PLATFORM)
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static const char *TAG_IS31FL3193 = "IS31FL3193_Drv";
#define IS31FL3193_LOG_ERR(...) ESP_LOGE(TAG_IS31FL3193, __VA_ARGS__)
#define IS31FL3193_LOG_WRN(...) ESP_LOGW(TAG_IS31FL3193, __VA_ARGS__)
#define IS31FL3193_LOG_DBG(...) ESP_LOGD(TAG_IS31FL3193, __VA_ARGS__)
#else
#include <stdio.h>
#include <unistd.h>
#define IS31FL3193_LOG_ERR(...)             \
    printf("IS31FL3193 ERR: " __VA_ARGS__); \
    printf("\n")
#define IS31FL3193_LOG_WRN(...)             \
    printf("IS31FL3193 WRN: " __VA_ARGS__); \
    printf("\n")
#define IS31FL3193_LOG_DBG(...)             \
    printf("IS31FL3193 DBG: " __VA_ARGS__); \
    printf("\n")
#endif

/// <summary>
/// Constructor implementation using HAL interfaces.
/// </summary>
RgbLedPartNumberIs31fl3193::RgbLedPartNumberIs31fl3193(II2cHal &i2c_hal_ref, IGpioHal &standby_gpio_hal_ref)
    : i2c(i2c_hal_ref), ledStandbyGpio(standby_gpio_hal_ref) // Updated member name
{
    // Check if HAL objects are initialized
    if (!i2c.IsInitialized())
    {
        IS31FL3193_LOG_WRN("I2C HAL passed to IS31FL3193 driver is not initialized!");
    }
    if (!ledStandbyGpio.IsInitialized())
    { // Use updated member name
        IS31FL3193_LOG_WRN("Standby GPIO HAL passed to IS31FL3193 driver is not initialized!");
    }
    // Initialize local color storage
    RED = 0;
    GREEN = 0;
    BLUE = 0;
}

/// <summary>
/// Platform-agnostic delay implementation (remains the same).
/// </summary>
void RgbLedPartNumberIs31fl3193::DelayMs(uint32_t milliseconds)
{
#ifdef __ZEPHYR__
    k_msleep(milliseconds);
#elif defined(ESP_PLATFORM)
    if (milliseconds == 0)
        return;
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
#else
    if (milliseconds > 0)
    {
        usleep(milliseconds * 1000);
    }
#endif
}

// --- Low-Level Register Access (using II2cHal) ---
// WriteRegister and ReadRegister implementations remain the same as previous update

/// <summary>
/// Writes a single byte to a specified register address using the II2cHal interface.
/// </summary>
bool RgbLedPartNumberIs31fl3193::WriteRegister(uint8_t registerAddress, uint8_t writeValue)
{
    HalI2cError err;
    uint8_t i2c_write_data[] = {registerAddress, writeValue};

    err = i2c.Write(i2c_write_data, sizeof(i2c_write_data)); // Use HAL Write
    if (err != HAL_I2C_OK)
    {
        IS31FL3193_LOG_ERR("WriteRegister failed (Reg 0x%02X, Val 0x%02X), Err: %d", registerAddress, writeValue, err);
        return false;
    }
    DelayMs(REGISTER_WRITE_DELAY_MS);

    IS31FL3193_LOG_DBG("WriteRegister OK (Reg 0x%02X, Val 0x%02X)", registerAddress, writeValue);
    return true;
}

/// <summary>
/// Reads a single byte from a specified register address using the II2cHal interface.
/// </summary>
bool RgbLedPartNumberIs31fl3193::ReadRegister(uint8_t registerAddress, uint8_t &readValue)
{
    HalI2cError err;

    err = i2c.Write(&registerAddress, 1); // Write the register address to read from
    if (err != HAL_I2C_OK)
    {
        IS31FL3193_LOG_ERR("ReadRegister failed (Write Addr 0x%02X), Err: %d", registerAddress, err);
        return false;
    }

    DelayMs(REGISTER_WRITE_DELAY_MS);

    err = i2c.Read(&readValue, 1); // Read the data byte
    if (err != HAL_I2C_OK)
    {
        IS31FL3193_LOG_ERR("ReadRegister failed (Read Data from 0x%02X), Err: %d", registerAddress, err);
        return false;
    }

    IS31FL3193_LOG_DBG("ReadRegister OK (Reg 0x%02X, Val 0x%02X)", registerAddress, readValue);
    return true;
}

// --- Private Register-Specific Operations (remain the same) ---

/// <summary>
/// Writes the PWM value to a specific channel's register.
/// </summary>
bool RgbLedPartNumberIs31fl3193::WritePwmChannel(uint8_t channel, uint8_t value)
{
    if (channel > 2)
    {
        IS31FL3193_LOG_ERR("Invalid PWM channel: %d", channel);
        return false;
    }
    uint8_t reg_addr = 0;
    switch (channel)
    {
    case 0:
        reg_addr = IS31FL3193_REG_PWM_CH0;
        break;
    case 1:
        reg_addr = IS31FL3193_REG_PWM_CH1;
        break;
    case 2:
        reg_addr = IS31FL3193_REG_PWM_CH2;
        break;
    }
    return WriteRegister(reg_addr, value);
}

/// <summary>
/// Writes the desired state to the LED Control Register (enables/disables channels).
/// </summary>
bool RgbLedPartNumberIs31fl3193::WriteLedControlRegister(uint8_t value)
{
    return WriteRegister(IS31FL3193_REG_LED_CONTROL, value);
}

/// <summary>
/// Writes to the Data Update Register to latch PWM and LED Control register changes.
/// </summary>
bool RgbLedPartNumberIs31fl3193::TriggerDataUpdate()
{
    return WriteRegister(IS31FL3193_REG_DATA_UPDATE, 0x00);
}

/// <summary>
/// Writes to the Time Update Register to latch breathing timing changes.
/// </summary>
bool RgbLedPartNumberIs31fl3193::TriggerTimeUpdate()
{
    return WriteRegister(IS31FL3193_REG_TIME_UPDATE, 0x00);
}

/// <summary>
/// Configures breathing timing registers for a specific channel.
/// </summary>
bool RgbLedPartNumberIs31fl3193::ConfigureBreathingTiming(uint8_t channel, uint8_t t0, uint8_t t1t2, uint8_t t3t4)
{
    if (channel > 2)
    {
        IS31FL3193_LOG_ERR("Invalid channel for breathing timing: %d", channel);
        return false;
    }

    uint8_t t0_reg = IS31FL3193_REG_T0_CH0 + channel;
    uint8_t t1t2_reg = IS31FL3193_REG_T1T2_CH0 + channel;
    uint8_t t3t4_reg = IS31FL3193_REG_T3T4_CH0 + channel;

    bool success = true;
    success &= WriteRegister(t0_reg, t0);
    success &= WriteRegister(t1t2_reg, t1t2);
    success &= WriteRegister(t3t4_reg, t3t4);

    return success;
}

// --- Public Device Control Functions (using HAL) ---

/// <summary>
/// Initializes the device: ensures GPIO is active, exits I2C shutdown, sets default current.
/// </summary>
bool RgbLedPartNumberIs31fl3193::InitializeDevice()
{
    IS31FL3193_LOG_DBG("Initializing IS31FL3193...");
    bool success = true;

    // Configure standby GPIO as Output and set to Active state
    // IS31FL3193 SDB pin: HIGH = active (normal operation), LOW = shutdown/standby
    HalGpioError gpio_err = ledStandbyGpio.Configure(IGpioHal::Direction::Output, IGpioHal::Resistance::None);
    if (gpio_err == HAL_GPIO_OK)
    {
        gpio_err = ledStandbyGpio.Write(true); // HIGH = active state (exit shutdown)
    }
    if (gpio_err != HAL_GPIO_OK)
    {
        IS31FL3193_LOG_ERR("Failed to configure/set standby GPIO to active state! Err: %d", gpio_err);
        success = false; // Treat GPIO failure as initialization failure
    }

    DelayMs(5); // Allow time for hardware wakeup

    // Exit software shutdown mode via I2C
    success &= WriteRegister(IS31FL3193_REG_SHUTDOWN, IS31FL3193_SHUTDOWN_SSD_NORMAL);

    // Explicitly disable breathing mode to clear any residual state from previous boot
    success &= WriteRegister(IS31FL3193_REG_BREATHING, 0x00);
    success &= WriteRegister(IS31FL3193_REG_LED_MODE, IS31FL3193_LED_MODE_PWM);

    // Clear all PWM channels to ensure no residual colors
    success &= WriteRegister(IS31FL3193_REG_PWM_CH0, 0x00);
    success &= WriteRegister(IS31FL3193_REG_PWM_CH1, 0x00);
    success &= WriteRegister(IS31FL3193_REG_PWM_CH2, 0x00);
    success &= WriteRegister(IS31FL3193_REG_LED_CONTROL, 0x00);
    success &= TriggerDataUpdate();

    // Set a default global current
    success &= SetGlobalCurrent(0x50); // ~10mA

    // Set initial color to off
    success &= SetRGB(0, 0, 0);

    if (success)
    {
        IS31FL3193_LOG_DBG("Device Initialized Successfully.");
    }
    else
    {
        IS31FL3193_LOG_ERR("Device Initialization Failed!");
    }
    return success;
}

/// <summary>
/// Puts the device into software shutdown mode via I2C.
/// </summary>
bool RgbLedPartNumberIs31fl3193::ShutdownDevice()
{
    IS31FL3193_LOG_DBG("Entering software shutdown via I2C.");
    return WriteRegister(IS31FL3193_REG_SHUTDOWN, IS31FL3193_SHUTDOWN_SSD_SOFT_SHUTDOWN);
    // Note: Hardware standby via SetLedStandby(true) might also be desired.
}

/// <summary>
/// Resets device registers to default values via I2C.
/// </summary>
bool RgbLedPartNumberIs31fl3193::ResetDevice()
{
    IS31FL3193_LOG_DBG("Resetting device registers via I2C.");
    bool success = WriteRegister(IS31FL3193_REG_RESET, IS31FL3193_RESET_VALUE);
    DelayMs(2); // Allow time for reset
    // Re-initialize after reset to ensure operational state
    success &= InitializeDevice();
    return success;
}

/// <summary>
/// Sets the global output current limit via I2C.
/// </summary>
bool RgbLedPartNumberIs31fl3193::SetGlobalCurrent(uint8_t current_code)
{
    IS31FL3193_LOG_DBG("Setting global current code to 0x%02X", current_code);
    return WriteRegister(IS31FL3193_REG_OUTPUT_CURRENT, current_code);
}

// --- Public Higher-Level Color Setters (remain the same) ---

/// <summary>
/// Sets the RGB color using a 24-bit hex code (0xRRGGBB).
/// </summary>
bool RgbLedPartNumberIs31fl3193::SetColorCode(uint32_t colorCode)
{
    IS31FL3193_LOG_DBG("Setting color code: 0x%06lX", colorCode);
    uint8_t r = (colorCode >> 16) & 0xFF;
    uint8_t g = (colorCode >> 8) & 0xFF;
    uint8_t b = (colorCode) & 0xFF;
    return SetRGB(r, g, b);
}

/// <summary>
/// Sets the RGB color using individual 8-bit values (0-255).
/// </summary>
bool RgbLedPartNumberIs31fl3193::SetRGB(uint8_t R, uint8_t G, uint8_t B)
{
    IS31FL3193_LOG_DBG("Setting RGB: R=%u, G=%u, B=%u", R, G, B);
    bool success = true;
    this->RED = R;
    this->GREEN = G;
    this->BLUE = B;
    success &= WritePwmChannel(0, R);
    success &= WritePwmChannel(1, G);
    success &= WritePwmChannel(2, B);
    uint8_t ledControl = 0;
    if (R > 0)
        ledControl |= IS31FL3193_LED_CONTROL_CH0_EN;
    if (G > 0)
        ledControl |= IS31FL3193_LED_CONTROL_CH1_EN;
    if (B > 0)
        ledControl |= IS31FL3193_LED_CONTROL_CH2_EN;
    success &= WriteLedControlRegister(ledControl);
    success &= TriggerDataUpdate();
    if (!success)
    {
        IS31FL3193_LOG_ERR("Failed to set RGB values completely.");
    }
    return success;
}

/// <summary>
/// Sets only the Red PWM value.
/// </summary>
bool RgbLedPartNumberIs31fl3193::SetRed(uint8_t R)
{
    IS31FL3193_LOG_DBG("Setting Red: %u", R);
    this->RED = R;
    return SetRGB(this->RED, this->GREEN, this->BLUE);
}

/// <summary>
/// Sets only the Green PWM value.
/// </summary>
bool RgbLedPartNumberIs31fl3193::SetGreen(uint8_t G)
{
    IS31FL3193_LOG_DBG("Setting Green: %u", G);
    this->GREEN = G;
    return SetRGB(this->RED, this->GREEN, this->BLUE);
}

/// <summary>
/// Sets only the Blue PWM value.
/// </summary>
bool RgbLedPartNumberIs31fl3193::SetBlue(uint8_t B)
{
    IS31FL3193_LOG_DBG("Setting Blue: %u", B);
    this->BLUE = B;
    return SetRGB(this->RED, this->GREEN, this->BLUE);
}

// --- Breathing Mode Control ---

/// <summary>
/// Configures breathing (blink) mode for all active LED channels.
/// Uses IS31FL3193 hardware breathing for power-efficient blinking.
/// </summary>
bool RgbLedPartNumberIs31fl3193::SetBreathingMode(bool enable, uint16_t periodMs)
{
    IS31FL3193_LOG_DBG("SetBreathingMode: enable=%d, periodMs=%u", enable, periodMs);
    bool success = true;

    if (!enable)
    {
        // Disable breathing mode - return to constant PWM
        success &= WriteRegister(IS31FL3193_REG_BREATHING, 0x00);
        success &= WriteRegister(IS31FL3193_REG_LED_MODE, IS31FL3193_LED_MODE_PWM);
        IS31FL3193_LOG_DBG("Breathing mode disabled");
        return success;
    }

    // IS31FL3193 timing codes: 0=0.13s, 1=0.26s, 2=0.52s, 3=1.04s, 4=2.08s, 5=4.16s, 6=8.32s, 7=16.64s
    // T1 = rise time (fade in), T2 = hold at max, T3 = fall time (fade out), T4 = hold at off
    // For smooth breathing: use longer rise/fall times for gradual fade effect

    uint8_t t1_code, t2_code, t3_code, t4_code;

    if (periodMs <= 300)
    {
        // Fast blink for errors (~1.5Hz) - still somewhat smooth
        t1_code = 0; // 0.13s rise
        t2_code = 0; // 0.13s hold on
        t3_code = 0; // 0.13s fall
        t4_code = 1; // 0.26s hold off
        // Total period: ~0.65s
    }
    else if (periodMs <= 600)
    {
        // Medium breathing (~1Hz)
        t1_code = 1; // 0.26s rise
        t2_code = 0; // 0.13s hold on
        t3_code = 1; // 0.26s fall
        t4_code = 1; // 0.26s hold off
        // Total period: ~0.91s
    }
    else
    {
        // Normal smooth breathing (~0.6Hz) - nice slow pulse
        t1_code = 2; // 0.52s rise (gradual fade in)
        t2_code = 0; // 0.13s hold on (brief peak)
        t3_code = 2; // 0.52s fall (gradual fade out)
        t4_code = 2; // 0.52s hold off (pause before next breath)
        // Total period: ~1.69s
    }

    // T1T2 register: T1[6:4] | T2[2:0]
    uint8_t t1t2_val = ((t1_code & 0x07) << 4) | (t2_code & 0x07);
    // T3T4 register: T3[6:4] | T4[2:0]
    uint8_t t3t4_val = ((t3_code & 0x07) << 4) | (t4_code & 0x07);
    uint8_t t0_val = 0; // No initial delay

    // Configure timing for all three channels
    for (uint8_t ch = 0; ch < 3; ch++)
    {
        success &= ConfigureBreathingTiming(ch, t0_val, t1t2_val, t3t4_val);
    }

    // Trigger time update to latch timing registers
    success &= TriggerTimeUpdate();

    // Enable breathing on active channels
    uint8_t breathingEnable = 0;
    if (RED > 0) breathingEnable |= IS31FL3193_BREATHING_ENABLE_CH0;
    if (GREEN > 0) breathingEnable |= IS31FL3193_BREATHING_ENABLE_CH1;
    if (BLUE > 0) breathingEnable |= IS31FL3193_BREATHING_ENABLE_CH2;

    success &= WriteRegister(IS31FL3193_REG_BREATHING, breathingEnable);

    // Set LED mode to breathing
    success &= WriteRegister(IS31FL3193_REG_LED_MODE, IS31FL3193_LED_MODE_BREATHING);

    if (success)
    {
        IS31FL3193_LOG_DBG("Breathing mode enabled with T1T2=0x%02X, T3T4=0x%02X", t1t2_val, t3t4_val);
    }
    else
    {
        IS31FL3193_LOG_ERR("Failed to configure breathing mode");
    }

    return success;
}

// --- Hardware Standby Control (using IGpioHal) ---

/// <summary>
/// Reads the current state of the hardware standby GPIO pin using the IGpioHal interface.
/// IS31FL3193 SDB pin: HIGH = active (normal operation), LOW = shutdown/standby
/// </summary>
bool RgbLedPartNumberIs31fl3193::GetLedStandby()
{
    int level = ledStandbyGpio.Read(); // Use HAL Read
    if (level < 0)
    {
        IS31FL3193_LOG_ERR("Failed to read standby GPIO state!");
        // Return a default state or handle error appropriately
        return true; // Assume standby on error
    }
    // SDB pin: HIGH = active, LOW = standby
    // So level==0 (LOW) means standby=true, level==1 (HIGH) means standby=false (active)
    bool standby_state = (level == 0);
    IS31FL3193_LOG_DBG("Getting LED standby state: %s (GPIO Level: %d)", standby_state ? "Standby" : "Active", level);
    return standby_state;
}

/// <summary>
/// Sets the state of the hardware standby GPIO pin using the IGpioHal interface.
/// IS31FL3193 SDB pin: HIGH = active (normal operation), LOW = shutdown/standby
/// </summary>
void RgbLedPartNumberIs31fl3193::SetLedStandby(bool standby)
{
    IS31FL3193_LOG_DBG("Setting LED standby state: %s", standby ? "Standby" : "Active");
    // Ensure the GPIO is configured as output before writing
    HalGpioError err = ledStandbyGpio.Configure(IGpioHal::Direction::Output, IGpioHal::Resistance::None);
    if (err == HAL_GPIO_OK)
    {
        // SDB pin: HIGH = active, LOW = standby
        // So when standby=true, we write LOW; when standby=false (active), we write HIGH
        err = ledStandbyGpio.Write(!standby);
    }

    if (err != HAL_GPIO_OK)
    {
        IS31FL3193_LOG_ERR("Failed to set standby GPIO state! Err: %d", err);
    }
    else
    {
        // Add a small delay to allow hardware to react if needed
        DelayMs(1);
    }
}