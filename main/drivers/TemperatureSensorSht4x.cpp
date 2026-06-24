/*
 * SHT4X.cpp
 *
 * Created on: Jul 15, 2024
 * Author: mason.melville
 * Updated: April 10, 2025
 */

#include "TemperatureSensorSht4x.h"

// Platform-specific includes for logging and delays
#ifdef __ZEPHYR__ // Using Zephyr / nRF Connect SDK
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
LOG_MODULE_REGISTER(TemperatureSensorSht4x, CONFIG_LOG_DEFAULT_LEVEL); // Adjust log level if needed
#define SHT4X_LOG_ERR(...) LOG_ERR(__VA_ARGS__)
#define SHT4X_LOG_WRN(...) LOG_WRN(__VA_ARGS__)
#define SHT4X_LOG_DBG(...) LOG_DBG(__VA_ARGS__)
#elif defined(ESP_PLATFORM) // Using ESP-IDF
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static const char *TAG_SHT4X = "SHT4X";
#define SHT4X_LOG_ERR(...) ESP_LOGE(TAG_SHT4X, __VA_ARGS__)
#define SHT4X_LOG_WRN(...) ESP_LOGW(TAG_SHT4X, __VA_ARGS__)
#define SHT4X_LOG_DBG(...) ESP_LOGD(TAG_SHT4X, __VA_ARGS__)
#else
// Basic fallback logging / delay if no platform detected
#include <stdio.h>
#include <unistd.h> // For usleep (might not be available everywhere)
#define SHT4X_LOG_ERR(...)             \
    printf("SHT4X ERR: " __VA_ARGS__); \
    printf("\n")
#define SHT4X_LOG_WRN(...)             \
    printf("SHT4X WRN: " __VA_ARGS__); \
    printf("\n")
#define SHT4X_LOG_DBG(...)             \
    printf("SHT4X DBG: " __VA_ARGS__); \
    printf("\n")
#endif

// Constructor implementation
TemperatureSensorSht4x::TemperatureSensorSht4x(II2cHal &i2c_hal_ref) : i2c(i2c_hal_ref)
{
    if (!i2c.IsInitialized())
    {
        SHT4X_LOG_WRN("I2C HAL passed to SHT4x driver is not initialized!");
    }
}

// Platform-agnostic delay implementation
void TemperatureSensorSht4x::DelayMs(uint32_t milliseconds)
{
#ifdef __ZEPHYR__
    k_msleep(milliseconds);
#elif defined(ESP_PLATFORM)
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
#else
    // Basic fallback using usleep (may be inaccurate)
    usleep(milliseconds * 1000);
#endif
}

uint8_t TemperatureSensorSht4x::CalculateCrc(uint8_t *crc_source)
{
    uint8_t crc = TEMPHUM24_INIT_VALUE;
    for (uint8_t byte_cnt = 0; byte_cnt < 2; byte_cnt++)
    {
        crc ^= crc_source[byte_cnt];
        for (uint8_t bit_cnt = 0; bit_cnt < 8; bit_cnt++)
        {
            if (crc & 0x80)
            {
                crc = (uint8_t)((crc << 1) ^ TEMPHUM24_POLYNOM);
            }
            else
            {
                crc <<= 1;
            }
        }
    }
    return crc;
}

bool TemperatureSensorSht4x::ReadRaw(uint16_t &temperature, uint16_t &humidity)
{
    HalI2cError err;
    uint8_t buffer[6] = {0};
    uint8_t cmd[1] = {HIGH_PRECISION_MEAURE_CMD};

    err = i2c.Write(cmd, sizeof(cmd));
    if (err != HAL_I2C_OK)
    {
        SHT4X_LOG_ERR("Write command failed: %d", err);
        return false;
    }

    DelayMs(SHT4X_HIGH_PREC_MEAS_TIME_MS);

    err = i2c.Read(buffer, sizeof(buffer));
    if (err != HAL_I2C_OK)
    {
        SHT4X_LOG_DBG("Read data failed: %d", err);
        return false;
    }

    // Calculate and check the two CRCs.
    const uint8_t crc_temp = CalculateCrc(&buffer[0]);
    const uint8_t crc_hum = CalculateCrc(&buffer[3]);

    if (crc_temp == buffer[2] && crc_hum == buffer[5])
    {
        // Store the raw temperature.
        temperature = (static_cast<uint16_t>(buffer[0]) << 8) | buffer[1];
        // Store the raw humidity.
        humidity = (static_cast<uint16_t>(buffer[3]) << 8) | buffer[4];

        // Basic sanity check (optional)
        if (temperature == 0 && humidity == 0)
        {
            SHT4X_LOG_WRN("Read zero values after CRC check.");
            // Consider if this should be an error depending on application
        }
        return true; // Success
    }
    else
    {
        SHT4X_LOG_ERR("CRC mismatch! T_CRC_Got: %02X (Calc %02X), H_CRC_Got: %02X (Calc %02X)",
                      buffer[2], crc_temp, buffer[5], crc_hum);
        return false; // CRC Error
    }
}

bool TemperatureSensorSht4x::Read(float &temperature, float &humidity)
{
    uint16_t rawTemperature = 0U;
    uint16_t rawHumidity = 0U;

    if (ReadRaw(rawTemperature, rawHumidity))
    {
        // Convert temperature from raw ADC to °C: T = -45 + 175 * (S_T / 65535)
        temperature = -45.0f + 175.0f * (static_cast<float>(rawTemperature) / 65535.0f);

        // Convert humidity from raw ADC to RH %: RH = -6 + 125 * (S_RH / 65535)
        // Clamp humidity to 0-100% range as per datasheet recommendation
        float rh = -6.0f + 125.0f * (static_cast<float>(rawHumidity) / 65535.0f);
        if (rh > 100.0f)
        {
            humidity = 100.0f;
        }
        else if (rh < 0.0f)
        {
            humidity = 0.0f;
        }
        else
        {
            humidity = rh;
        }
        return true;
    }

    // ReadRaw failed
    return false;
}

bool TemperatureSensorSht4x::Reset()
{
    HalI2cError err;
    uint8_t cmd[1] = {SOFT_RESET_CMD};

    err = i2c.Write(cmd, sizeof(cmd));
    if (err != HAL_I2C_OK)
    {
        SHT4X_LOG_ERR("Soft Reset command failed: %d", err);
        return false;
    }

    // Wait for reset duration
    DelayMs(SHT4X_RESET_TIME_MS);
    SHT4X_LOG_DBG("Soft Reset command sent.");
    return true;
}

bool TemperatureSensorSht4x::ActivateHeater(uint8_t heater_command)
{
    HalI2cError err;
    uint8_t cmd[1] = {heater_command}; // Use the provided command code

    // Check if the command code is valid (basic check)
    switch (heater_command)
    {
    case ACTIVATE_HEATER_200MW_1SEC_CMD:
    case ACTIVATE_HEATER_200MW_01SEC_CMD:
    case ACTIVATE_HEATER_110MW_1SEC_CMD:
    case ACTIVATE_HEATER_110MW_01SEC_CMD:
    case ACTIVATE_HEATER_20MW_1SEC_CMD:
    case ACTIVATE_HEATER_20MW_01SEC_CMD:
        break; // Valid command
    default:
        SHT4X_LOG_ERR("Invalid heater command code: 0x%02X", heater_command);
        return false;
    }

    err = i2c.Write(cmd, sizeof(cmd));
    if (err != HAL_I2C_OK)
    {
        SHT4X_LOG_ERR("Heater command (0x%02X) failed: %d", heater_command, err);
        return false;
    }

    SHT4X_LOG_DBG("Heater command (0x%02X) sent.", heater_command);
    // Note: Application needs to handle waiting after this command if necessary.
    return true;
}

bool TemperatureSensorSht4x::ReadSerialNumber(uint32_t &serial_number)
{
    HalI2cError err;
    uint8_t buffer[6] = {0};
    uint8_t cmd[1] = {READ_SERIAL_NUM_CMD};

    err = i2c.Write(cmd, sizeof(cmd));
    if (err != HAL_I2C_OK)
    {
        SHT4X_LOG_ERR("Write Read Serial Number command failed: %d", err);
        return false;
    }

    // Delay - check datasheet if needed, typically short for read commands
    DelayMs(1); // Small delay

    err = i2c.Read(buffer, sizeof(buffer));
    if (err != HAL_I2C_OK)
    {
        SHT4X_LOG_ERR("Read Serial Number data failed: %d", err);
        return false;
    }

    // Check CRCs for both 16-bit words
    const uint8_t crc_word1 = CalculateCrc(&buffer[0]); // SN[31:16]
    const uint8_t crc_word2 = CalculateCrc(&buffer[3]); // SN[15:0]

    if (crc_word1 == buffer[2] && crc_word2 == buffer[5])
    {
        // Combine the two 16-bit words into the 32-bit serial number
        uint16_t sn_msb = (static_cast<uint16_t>(buffer[0]) << 8) | buffer[1];
        uint16_t sn_lsb = (static_cast<uint16_t>(buffer[3]) << 8) | buffer[4];
        serial_number = (static_cast<uint32_t>(sn_msb) << 16) | sn_lsb;
        return true;
    }
    else
    {
        SHT4X_LOG_ERR("Serial Number CRC mismatch! W1_CRC: %02X (Calc %02X), W2_CRC: %02X (Calc %02X)",
                      buffer[2], crc_word1, buffer[5], crc_word2);
        return false;
    }
}
