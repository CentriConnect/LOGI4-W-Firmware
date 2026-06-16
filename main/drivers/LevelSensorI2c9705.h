// ----- File: main/drivers/LevelSensorI2c9705.h ----- NEW -----
/*
 * LevelSensorI2c9705.h
 *
 * Driver for the Rochester Sensors 9705 I2C Level Sensor using HAL interfaces.
 *
 * Created on: April 17, 2025 (Adapted from Nordic version)
 * Author: Gemini
 *
 * Datasheet: https://rochestersensors.com/wp-content/uploads/DS-02014.pdf
 */

#ifndef DRIVERS_LEVEL_SENSOR_I2C_9705_H_
#define DRIVERS_LEVEL_SENSOR_I2C_9705_H_

#include "interfaces/II2cHal.h" // Use HAL I2C interface
#include <stdint.h>
#include <stddef.h>
#include <math.h> // For PI, atan2, round

// Platform-agnostic logging/delay includes will be in the .cpp file

#define PI 3.14159

// Command Codes
#define EXIT_MODE_COMMAND_CODE 0x80
#define BURST_MODE_TXY_COMMAND_CODE 0x17 // 0b0001zyxt
#define READ_REGISTER_COMMAND_CODE 0x50
#define WRITE_REGISTER_COMMAND_CODE 0x60
#define READ_MEASUREMENTS_TXY_COMMAND_CODE 0x47 // 0b0100zyxt

// Register Addresses
#define GAIN_REGISTER_ADDR 0x00
#define RESOLUTION_REGISTER_ADDR 0x02

// Status Bits
#define STATUS_ERROR 0b00010000 // Error bit in status byte

// Retry parameters for Read function
#define LEVEL_SENSOR_READ_MAX_RETRIES 10
#define LEVEL_SENSOR_READ_RETRY_DELAY_MS 5

/// <summary>
/// Represents the Rochester Sensors 9705 I2C Level Sensor.
/// Provides methods for initializing the sensor, reading temperature and level data.
/// Uses the II2cHal interface for platform independence.
/// </summary>
class LevelSensorI2c9705
{
public:
    /// <summary>
    /// Initializes a new instance of the LevelSensorI2c9705 class.
    /// </summary>
    /// <param name="i2c_hal_ref">A reference to an initialized object implementing the II2cHal interface.</param>
    LevelSensorI2c9705(II2cHal &i2c_hal_ref);

    // Make destructor virtual if this class might be inherited from
    virtual ~LevelSensorI2c9705() = default;

    // Delete copy constructor and assignment operator
    LevelSensorI2c9705(const LevelSensorI2c9705 &) = delete;
    LevelSensorI2c9705 &operator=(const LevelSensorI2c9705 &) = delete;

    /// <summary>
    /// Initializes the sensor with recommended settings (Gain 3, Resolution 3) and puts it into Burst Mode.
    /// </summary>
    /// <returns><c>true</c> if initialization was successful, <c>false</c> otherwise.</returns>
    bool Sensor_Initialize();

    /// <summary>
    /// Reads the temperature and level measurements from the sensor.
    /// Assumes the sensor is in Burst Mode or TXY mode. Includes retry logic for sensor status errors.
    /// </summary>
    /// <param name="temperature">Pointer to store the temperature reading in degrees Celsius.</param>
    /// <param name="level">Pointer to store the level reading (based on lookup table, 0-100%).</param>
    /// <returns><c>true</c> if the read was successful, <c>false</c> otherwise (e.g., I2C error or persistent sensor error status).</returns>
    bool Read(float *temperature, uint16_t *level);

private:
    /// <summary>
    /// Reference to the platform-agnostic I2C HAL implementation.
    /// </summary>
    II2cHal &i2c;

    /// <summary>
    /// The zero value offset based on the selected sensor resolution.
    /// </summary>
    int SensorResolutionZeroValue = 0;

    /// <summary>
    /// Stores the calculated angle in degrees (0-359) after converting from rectangular coordinates.
    /// </summary>
    int dialDegree;

    /// <summary>
    /// Sends the Exit Mode command to the sensor.
    /// </summary>
    /// <returns><c>true</c> if the command was sent successfully and the status indicates no error, <c>false</c> otherwise.</returns>
    bool Sensor_Send_Exit_Command();

    /// <summary>
    /// Sends the Burst Mode TXY command to the sensor.
    /// </summary>
    /// <returns><c>true</c> if the command was sent successfully and the status indicates no error, <c>false</c> otherwise.</returns>
    bool Sensor_Send_Burst_Mode_Command();

    /// <summary>
    /// Sets the sensor's resolution for X, Y, and Z axes.
    /// Updates SensorResolutionZeroValue internally.
    /// </summary>
    /// <param name="arResXYZ">The desired resolution setting (0=8bit, 1=10bit, 2=12bit, 3=14bit).</param>
    /// <returns><c>true</c> if the resolution was set successfully, <c>false</c> otherwise.</returns>
    bool Sensor_Set_Resolution(int arResXYZ);

    /// <summary>
    /// Sets the sensor's gain and configuration.
    /// </summary>
    /// <param name="newGain">The desired gain setting (0-7).</param>
    /// <param name="newConf">The desired configuration setting (0x00 - 0x0F).</param>
    /// <returns><c>true</c> if the gain and configuration were set successfully, <c>false</c> otherwise.</returns>
    bool Sensor_Set_Gain(int newGain, int newConf);

    /// <summary>
    /// Converts rectangular coordinates (X, Y) to a polar angle in degrees.
    /// </summary>
    /// <param name="x">The adjusted X coordinate reading.</param>
    /// <param name="y">The adjusted Y coordinate reading.</param>
    /// <returns>The angle in degrees (0-359).</returns>
    int RectangularToPolar(int16_t x, int16_t y);

    /// <summary>
    /// Platform-agnostic delay function.
    /// </summary>
    /// <param name="milliseconds">Duration to delay in milliseconds.</param>
    void DelayMs(uint32_t milliseconds);

    /// <summary>
    /// Lookup table for converting degrees to level percentage (JrSr5_95_horizontal).
    /// Maps angles (0-360 degrees) to level percentages.
    /// </summary>
    static const int JrSr5_95_horizontal_[361];
};

#endif /* DRIVERS_LEVEL_SENSOR_I2C_9705_H_ */