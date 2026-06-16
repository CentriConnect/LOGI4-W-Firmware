// ----- File: main/drivers/LevelSensorI2c9705.cpp ----- NEW -----
/*
 * LevelSensorI2c9705.cpp
 *
 * Driver for the Rochester Sensors 9705 I2C Level Sensor using HAL interfaces.
 *
 * Created on: April 17, 2025 (Adapted from Nordic version)
 * Author: Gemini
 */

#include "drivers/LevelSensorI2c9705.h"
#include <math.h>  // For atan2, round
#include <cstring> // For memset

// Platform-specific includes for logging and delays
// Using the same style as TemperatureSensorSht4x.cpp for consistency
#ifdef __ZEPHYR__ // Using Zephyr / nRF Connect SDK (if needed for delays/logs)
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
LOG_MODULE_REGISTER(LevelSensorI2c9705, CONFIG_LOG_DEFAULT_LEVEL); // Adjust log level if needed
#define SENSOR_9705_LOG_ERR(...) LOG_ERR(__VA_ARGS__)
#define SENSOR_9705_LOG_WRN(...) LOG_WRN(__VA_ARGS__)
#define SENSOR_9705_LOG_INF(...) LOG_INF(__VA_ARGS__)
#define SENSOR_9705_LOG_DBG(...) LOG_DBG(__VA_ARGS__)
#elif defined(ESP_PLATFORM) // Using ESP-IDF
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static const char *TAG_9705 = "LSensor9705";
#define SENSOR_9705_LOG_ERR(...) ESP_LOGE(TAG_9705, __VA_ARGS__)
#define SENSOR_9705_LOG_WRN(...) ESP_LOGW(TAG_9705, __VA_ARGS__)
#define SENSOR_9705_LOG_INF(...) ESP_LOGI(TAG_9705, __VA_ARGS__)
#define SENSOR_9705_LOG_DBG(...) ESP_LOGD(TAG_9705, __VA_ARGS__)
#else
// Basic fallback logging / delay if no platform detected
#include <stdio.h>
#include <unistd.h> // For usleep (might not be available everywhere)
#define SENSOR_9705_LOG_ERR(...)      \
    printf("9705 ERR: " __VA_ARGS__); \
    printf("\n")
#define SENSOR_9705_LOG_WRN(...)      \
    printf("9705 WRN: " __VA_ARGS__); \
    printf("\n")
#define SENSOR_9705_LOG_INF(...)      \
    printf("9705 INF: " __VA_ARGS__); \
    printf("\n")
#define SENSOR_9705_LOG_DBG(...)      \
    printf("9705 DBG: " __VA_ARGS__); \
    printf("\n")
#endif

// --- Lookup Table Definition ---
// (Copy the JrSr5_95_horizontal_ array definition here from the Nordic cpp file)
const int LevelSensorI2c9705::JrSr5_95_horizontal_[361] =
    {
        100, 100, 99, 99, 99, 99, 99, 98, 98, 98, // 0-9
        98, 98, 98, 98, 97, 97, 97, 97, 97, 96,   // 10-19
        96, 96, 96, 96, 96, 96, 95, 95, 95, 95,   // 20-29
        95, 94, 94, 94, 94, 94, 94, 94, 93, 93,   // 30-39
        93, 93, 93, 92, 92, 92, 92, 92, 92, 92,   // 40-49
        91, 91, 91, 91, 91, 90, 90, 90, 90, 90,   // 50-59
        90, 90, 90, 89, 89, 89, 89, 88, 88, 88,   // 60-69
        88, 87, 87, 87, 87, 86, 86, 86, 86, 85,   // 70-79
        85, 85, 85, 84, 84, 84, 84, 83, 83, 83,   // 80-89
        83, 83, 82, 82, 82, 82, 81, 81, 81, 81,   // 90-99
        80, 80, 80, 80, 80, 79, 79, 78, 78, 78,   // 100-109
        78, 77, 77, 76, 76, 76, 75, 75, 75, 74,   // 110-119
        74, 74, 73, 73, 72, 72, 72, 72, 71, 71,   // 120-129
        70, 70, 70, 70, 69, 69, 68, 68, 67, 67,   // 130-139
        67, 66, 66, 65, 65, 65, 64, 64, 63, 63,   // 140-149
        62, 62, 61, 61, 61, 60, 60, 60, 59, 59,   // 150-159
        58, 58, 57, 57, 56, 56, 56, 55, 55, 54,   // 160-169
        54, 53, 53, 52, 52, 51, 51, 50, 50, 50,   // 170-179
        49, 49, 49, 48, 48, 47, 47, 47, 46, 46,   // 180-189
        45, 45, 44, 44, 43, 43, 43, 42, 42, 41,   // 190-199
        41, 40, 40, 40, 39, 39, 38, 38, 38, 37,   // 200-209
        37, 36, 36, 35, 35, 34, 34, 34, 33, 33,   // 210-219
        32, 32, 32, 31, 31, 30, 30, 30, 29, 29,   // 220-229
        29, 28, 28, 27, 27, 27, 27, 26, 26, 25,   // 230-239
        25, 25, 24, 24, 24, 23, 23, 23, 22, 22,   // 240-249
        21, 21, 21, 21, 20, 20, 20, 19, 19, 19,   // 250-259
        19, 18, 18, 18, 18, 17, 17, 17, 17, 16,   // 260-269
        16, 16, 16, 15, 15, 15, 15, 14, 14, 14,   // 270-279
        14, 13, 13, 13, 13, 12, 12, 12, 12, 11,   // 280-289
        11, 11, 11, 10, 10, 10, 10, 10, 9, 9,     // 290-299
        9, 9, 9, 8, 8, 8, 8, 7, 7, 7,             // 300-309
        7, 7, 7, 6, 6, 6, 5, 5, 5, 5,             // 310-319
        5, 5, 4, 4, 4, 4, 4, 4, 3, 3,             // 320-329
        3, 3, 3, 3, 3, 3, 3, 3, 2, 2,             // 330-339
        2, 2, 2, 2, 1, 1, 1, 1, 1, 1,             // 340-349
        1, 1, 1, 0, 0, 0, 0, 0, 0, 0,             // 350-359
        0                                         // 360
};

// --- Constructor ---
LevelSensorI2c9705::LevelSensorI2c9705(II2cHal &i2c_hal_ref)
    : i2c(i2c_hal_ref),
      SensorResolutionZeroValue(0), // Will be set by Sensor_Set_Resolution
      dialDegree(0)
{
    if (!i2c.IsInitialized())
    {
        SENSOR_9705_LOG_WRN("I2C HAL passed to LevelSensorI2c9705 driver is not initialized!");
    }
    SENSOR_9705_LOG_INF("LevelSensorI2c9705 driver instance created.");
}

// --- Platform-agnostic Delay ---
void LevelSensorI2c9705::DelayMs(uint32_t milliseconds)
{
#ifdef __ZEPHYR__
    k_msleep(milliseconds);
#elif defined(ESP_PLATFORM)
    if (milliseconds == 0)
        return; // Handle 0ms delay
    vTaskDelay(pdMS_TO_TICKS(milliseconds));
#else
    // Basic fallback using usleep (may be inaccurate or unavailable)
    if (milliseconds > 0)
    {
        usleep(milliseconds * 1000);
    }
#endif
}

// --- Public Functions ---

bool LevelSensorI2c9705::Sensor_Initialize()
{
    SENSOR_9705_LOG_INF("Initializing 9705 Sensor...");
    // Start by ensuring sensor is in a known state (send Exit command)
    if (!Sensor_Send_Exit_Command())
    {
        SENSOR_9705_LOG_ERR("Initial Exit command failed.");
        // return false; // Optional: Decide if failure here is fatal
    }
    DelayMs(10); // Short delay after exit command

    // Set recommended gain setting
    bool success = Sensor_Set_Gain(3, 0x0C);
    if (!success)
    {
        SENSOR_9705_LOG_ERR("Failed to set sensor gain.");
        return false;
    }
    DelayMs(10);

    // Set recommended resolution (14-bit)
    success = Sensor_Set_Resolution(3);
    if (!success)
    {
        SENSOR_9705_LOG_ERR("Failed to set sensor resolution.");
        return false;
    }
    DelayMs(10);

    // End in Burst Mode for continuous reads
    success = Sensor_Send_Burst_Mode_Command();
    if (!success)
    {
        SENSOR_9705_LOG_ERR("Failed to enter Burst Mode.");
        return false;
    }

    // Wait for sensor to stabilize after initialization commands
    DelayMs(200);
    SENSOR_9705_LOG_INF("Sensor Initialized Successfully.");
    return true;
}

bool LevelSensorI2c9705::Read(float *temperature, uint16_t *level)
{
    if (temperature == nullptr || level == nullptr)
    {
        SENSOR_9705_LOG_ERR("Read() called with null pointers.");
        return false;
    }

    HalI2cError err;
    uint8_t buffer[7];                                     // Buffer: Status + Temp(2) + X(2) + Y(2)
    uint8_t cmd[1] = {READ_MEASUREMENTS_TXY_COMMAND_CODE}; // Command to read T, X, Y
    uint8_t status = STATUS_ERROR;                         // Initialize status to indicate error state
    bool read_success = false;

    for (int retry = 0; retry < LEVEL_SENSOR_READ_MAX_RETRIES; ++retry)
    {
        // Clear buffer before read
        memset(buffer, 0, sizeof(buffer));

        // Perform I2C WriteRead using HAL interface
        err = i2c.WriteRead(cmd, sizeof(cmd), buffer, sizeof(buffer));

        if (err != HAL_I2C_OK)
        {
            SENSOR_9705_LOG_ERR("I2C transaction failed during Read (err: %d). Aborting read.", err);
            return false; // Exit function on I2C error
        }

        // Check the sensor status byte (first byte received)
        status = buffer[0];
        if (!(status & STATUS_ERROR))
        {
            // Success: Error bit is clear, data is likely valid
            read_success = true;
            SENSOR_9705_LOG_DBG("Sensor Read succeeded on retry %d. Status: 0x%02X", retry, status);
            break; // Exit the retry loop
        }
        else
        {
            // Error bit is set: Data might not be ready or valid yet
            SENSOR_9705_LOG_DBG("Sensor Read Error Status: 0x%02X on retry %d. Retrying...", status, retry);
            DelayMs(LEVEL_SENSOR_READ_RETRY_DELAY_MS); // Wait before the next attempt
        }
    } // End retry loop

    // Check if the read was ultimately unsuccessful after all retries
    if (!read_success)
    {
        SENSOR_9705_LOG_ERR("Sensor Read failed after %d retries. Last status: 0x%02X", LEVEL_SENSOR_READ_MAX_RETRIES, status);
        *temperature = 0.0f; // Indicate error state
        *level = 0;          // Indicate error state
        return false;
    }

    // --- Parse data only if read_success is true ---
    uint16_t readValueTemperature = (static_cast<uint16_t>(buffer[1]) << 8) | buffer[2];
    uint16_t readValueX = (static_cast<uint16_t>(buffer[3]) << 8) | buffer[4];
    uint16_t readValueY = (static_cast<uint16_t>(buffer[5]) << 8) | buffer[6];

    // Ensure SensorResolutionZeroValue is valid before adjusting
    if (SensorResolutionZeroValue == 0)
    {
        SENSOR_9705_LOG_ERR("Resolution Zero Value not set, cannot adjust X/Y. Call Sensor_Initialize() or Sensor_Set_Resolution() first.");
        *temperature = 0.0f;
        *level = 0;
        return false;
    }

    // Adjust X and Y relative to the zero point based on resolution
    int16_t adjustedX = static_cast<int16_t>(readValueX) - static_cast<int16_t>(SensorResolutionZeroValue);
    int16_t adjustedY = static_cast<int16_t>(readValueY) - static_cast<int16_t>(SensorResolutionZeroValue);

    // Convert rectangular coordinates (adjustedX, adjustedY) to polar angle (degrees)
    dialDegree = RectangularToPolar(adjustedX, adjustedY);

    // Calculate final temperature and level values
    // Temperature formula from datasheet
    *temperature = 25.0f + (static_cast<float>(readValueTemperature) - 46244.0f) / 45.2f;
    // Level obtained from lookup table using the calculated angle
    if (dialDegree >= 0 && dialDegree <= 360)
    {
        *level = JrSr5_95_horizontal_[dialDegree];
    }
    else
    {
        SENSOR_9705_LOG_ERR("Invalid dialDegree (%d) calculated. Setting level to 0.", dialDegree);
        *level = 0; // Error case
    }

    SENSOR_9705_LOG_DBG("Read success. TempRaw: %u, XRaw: %u, YRaw: %u, AdjX: %d, AdjY: %d, Deg: %d, Temp: %.2f, Level: %u",
                        readValueTemperature, readValueX, readValueY, adjustedX, adjustedY, dialDegree, *temperature, *level);

    return true; // Read and parse successful
}

// --- Private Helper Functions ---

bool LevelSensorI2c9705::Sensor_Send_Exit_Command()
{
    HalI2cError err;
    uint8_t i2c_read_data[1]; // Buffer to read status byte
    uint8_t cmd[1] = {EXIT_MODE_COMMAND_CODE};

    err = i2c.WriteRead(cmd, sizeof(cmd), i2c_read_data, sizeof(i2c_read_data));

    if (err != HAL_I2C_OK)
    {
        SENSOR_9705_LOG_ERR("I2C error sending Exit command: %d", err);
        return false;
    }
    uint8_t status = i2c_read_data[0];
    if (status & STATUS_ERROR)
    {
        SENSOR_9705_LOG_WRN("Sensor status error after Exit command: 0x%02X", status);
        // Depending on severity, you might still return true or false here
    }
    // Command considered successful if I2C worked, even if status bit is set?
    // Let's return based on status bit for stricter checking.
    return !(status & STATUS_ERROR);
}

bool LevelSensorI2c9705::Sensor_Send_Burst_Mode_Command()
{
    HalI2cError err;
    uint8_t i2c_read_data[1] = {0}; // Buffer to read status byte
    uint8_t cmd[1] = {BURST_MODE_TXY_COMMAND_CODE};

    err = i2c.WriteRead(cmd, sizeof(cmd), i2c_read_data, sizeof(i2c_read_data));

    if (err != HAL_I2C_OK)
    {
        SENSOR_9705_LOG_ERR("I2C error sending Burst Mode command: %d", err);
        return false;
    }
    uint8_t status = i2c_read_data[0];
    if (status & STATUS_ERROR)
    {
        SENSOR_9705_LOG_WRN("Sensor status error after Burst Mode command: 0x%02X", status);
    }
    return !(status & STATUS_ERROR); // Return true if error bit is NOT set
}

bool LevelSensorI2c9705::Sensor_Set_Resolution(int arResXYZ)
{
    if (arResXYZ < 0 || arResXYZ > 3)
    {
        SENSOR_9705_LOG_ERR("Invalid resolution setting: %d (must be 0-3).", arResXYZ);
        return false;
    }

    HalI2cError err;
    uint8_t i2c_read_buf[3] = {0, 0, 0}; // Status, RegHigh, RegLow
    uint16_t registerValue;
    uint8_t cmd_read[2] = {READ_REGISTER_COMMAND_CODE, RESOLUTION_REGISTER_ADDR << 2};

    // Read current resolution register value
    err = i2c.WriteRead(cmd_read, sizeof(cmd_read), i2c_read_buf, sizeof(i2c_read_buf));
    if (err != HAL_I2C_OK)
    {
        SENSOR_9705_LOG_ERR("I2C error reading Resolution register: %d", err);
        return false;
    }
    uint8_t status = i2c_read_buf[0];
    if (status & STATUS_ERROR)
    {
        SENSOR_9705_LOG_WRN("Sensor status error reading Resolution register: 0x%02X", status);
        return false; // Don't proceed if read failed
    }

    registerValue = (static_cast<uint16_t>(i2c_read_buf[1]) << 8) | i2c_read_buf[2];

    // Modify the register value
    registerValue &= (~0x07E0); // Clear bits 5-10 (XYZ resolution bits)
    // Replicate arResXYZ (0-3) for X, Y, Z resolution fields
    uint16_t resBits = (arResXYZ & 0x03) | ((arResXYZ & 0x03) << 2) | ((arResXYZ & 0x03) << 4);
    registerValue |= (resBits << 5); // Set the new resolution bits (bits 5,6 for Z; 7,8 for Y; 9,10 for X)

    // Write the modified value back to the register
    uint8_t i2c_write_data[] = {WRITE_REGISTER_COMMAND_CODE,
                                static_cast<uint8_t>(registerValue >> 8),   // High byte
                                static_cast<uint8_t>(registerValue & 0xFF), // Low byte
                                RESOLUTION_REGISTER_ADDR << 2};             // Register address

    uint8_t i2c_status_read[1]; // Buffer for status response after write
    err = i2c.WriteRead(i2c_write_data, sizeof(i2c_write_data), i2c_status_read, sizeof(i2c_status_read));
    if (err != HAL_I2C_OK)
    {
        SENSOR_9705_LOG_ERR("I2C error writing Resolution register: %d", err);
        return false;
    }
    status = i2c_status_read[0];
    if (status & STATUS_ERROR)
    {
        SENSOR_9705_LOG_WRN("Sensor status error writing Resolution register: 0x%02X", status);
        return false; // Write command resulted in an error status
    }

    // Update the internal zero value based on the successfully set resolution
    switch (arResXYZ)
    {
    case 0:
        SensorResolutionZeroValue = 1 << 8;
        break; // 8-bit
    case 1:
        SensorResolutionZeroValue = 1 << 10;
        break; // 10-bit
    case 2:
        SensorResolutionZeroValue = 1 << 12;
        break; // 12-bit
    case 3:
        SensorResolutionZeroValue = 1 << 14;
        break; // 14-bit
        // No default needed due to check at function start
    }
    SENSOR_9705_LOG_INF("Resolution set to %d. Zero value: %d", arResXYZ, SensorResolutionZeroValue);
    return true; // Resolution set successfully
}

bool LevelSensorI2c9705::Sensor_Set_Gain(int newGain, int newConf)
{
    if (newGain < 0 || newGain > 7 || newConf < 0 || newConf > 0x0F)
    {
        SENSOR_9705_LOG_ERR("Invalid Gain (%d) or Conf (0x%X) setting.", newGain, newConf);
        return false;
    }

    HalI2cError err;
    uint8_t buffer[3] = {0, 0, 0}; // Status, RegHigh, RegLow
    uint16_t registerValue = 0;
    uint8_t cmd_read[2] = {READ_REGISTER_COMMAND_CODE, GAIN_REGISTER_ADDR << 2};

    // Read current gain register value
    err = i2c.WriteRead(cmd_read, sizeof(cmd_read), buffer, sizeof(buffer));
    if (err != HAL_I2C_OK)
    {
        SENSOR_9705_LOG_ERR("I2C error reading Gain register: %d", err);
        return false;
    }
    uint8_t status = buffer[0];
    if (status & STATUS_ERROR)
    {
        SENSOR_9705_LOG_WRN("Sensor status error reading Gain register: 0x%02X", status);
        return false; // Don't proceed if read failed
    }

    registerValue = (static_cast<uint16_t>(buffer[1]) << 8) | buffer[2];

    // Modify the register value
    registerValue &= (~0x007F);               // Clear bits 0-6 (Conf and Gain bits)
    registerValue |= ((newGain & 0x07) << 4); // Set gain bits (4-6)
    registerValue |= (newConf & 0x0F);        // Set conf bits (0-3)

    // Write the modified value back to the register
    uint8_t i2c_write_data[] = {WRITE_REGISTER_COMMAND_CODE,
                                static_cast<uint8_t>(registerValue >> 8),   // High byte
                                static_cast<uint8_t>(registerValue & 0xFF), // Low byte
                                GAIN_REGISTER_ADDR << 2};                   // Register address

    uint8_t i2c_status_read[1]; // Buffer for status response after write
    err = i2c.WriteRead(i2c_write_data, sizeof(i2c_write_data), i2c_status_read, sizeof(i2c_status_read));
    if (err != HAL_I2C_OK)
    {
        SENSOR_9705_LOG_ERR("I2C error writing Gain register: %d", err);
        return false;
    }
    status = i2c_status_read[0];
    if (status & STATUS_ERROR)
    {
        SENSOR_9705_LOG_WRN("Sensor status error writing Gain register: 0x%02X", status);
        return false; // Write command resulted in an error status
    }

    SENSOR_9705_LOG_INF("Gain set to %d, Conf set to 0x%02X.", newGain, newConf);
    return true; // Gain/Conf set successfully
}

int LevelSensorI2c9705::RectangularToPolar(int16_t x, int16_t y)
{
    // Use atan2 for correct quadrant handling and to avoid division by zero
    double rad = atan2(static_cast<double>(y), static_cast<double>(x));

    // Convert radians to degrees
    double deg_double = (180.0 * rad) / PI;

    // Normalize degrees to 0-359 range
    if (deg_double < 0.0)
    {
        deg_double += 360.0;
    }

    // Round to nearest integer degree
    int degree = static_cast<int>(round(deg_double));

    // Handle edge case where degree might be exactly 360 after rounding/normalization
    if (degree >= 360)
    {
        degree = 0;
    }
    // Ensure degree is not negative after rounding (shouldn't happen with atan2 logic, but safe check)
    if (degree < 0)
    {
        degree = 0;
    }

    return degree;
}