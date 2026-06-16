/*
 * SHT4X.h
 *
 * Created on: Jul 15, 2024
 * Author: mason.melville
 * Updated: April 10, 2025
 *
 * Datasheet: https://www.mouser.com/datasheet/2/682/Datasheet_SHT4x-1917879.pdf
 *
 * This driver now uses the II2cHal interface for platform independence.
 * The appropriate I2C wrapper (e.g., EspI2cMasterWrapper or nRF I2c)
 * must be instantiated in the application and passed to the constructor.
 */

#ifndef SHT4X_H_
#define SHT4X_H_

#include "II2cHal.h" // Include the common HAL interface
#include <assert.h>
#include <stdint.h>

// SHT4x Command Codes
#define HIGH_PRECISION_MEAURE_CMD 0xFD       // High precision measurement
#define MED_PRECISION_MEASURE_CMD 0xF6       // Medium precision measurement
#define LOW_PRECISION_MEASURE_CMD 0xE0       // Lowest precision measurement
#define READ_SERIAL_NUM_CMD 0x89             // Read serial number
#define SOFT_RESET_CMD 0x94                  // Soft reset
#define ACTIVATE_HEATER_200MW_1SEC_CMD 0x39  // Heater high power, 1s duration
#define ACTIVATE_HEATER_200MW_01SEC_CMD 0x32 // Heater high power, 0.1s duration
#define ACTIVATE_HEATER_110MW_1SEC_CMD 0x2F  // Heater medium power, 1s duration
#define ACTIVATE_HEATER_110MW_01SEC_CMD 0x24 // Heater medium power, 0.1s duration
#define ACTIVATE_HEATER_20MW_1SEC_CMD 0x1E   // Heater low power, 1s duration
#define ACTIVATE_HEATER_20MW_01SEC_CMD 0x15  // Heater low power, 0.1s duration

// CRC Parameters
#define TEMPHUM24_POLYNOM 0x31
#define TEMPHUM24_INIT_VALUE 0xFF

// Measurement duration estimates (add margin)
#define SHT4X_HIGH_PREC_MEAS_TIME_MS 10 // Datasheet: 8.5ms typ
#define SHT4X_MED_PREC_MEAS_TIME_MS 5   // Datasheet: 4.5ms typ
#define SHT4X_LOW_PREC_MEAS_TIME_MS 2   // Datasheet: 1.5ms typ
#define SHT4X_RESET_TIME_MS 1           // Datasheet: < 1ms

class TemperatureSensorSht4x
{
public:
    /**
     * @brief Constructor for the SHT4x sensor driver.
     * @param i2c_hal_ref A reference to an initialized object implementing the II2cHal interface.
     */
    TemperatureSensorSht4x(II2cHal &i2c_hal_ref);

    /**
     * @brief Reads the raw temperature and humidity ADC counts from the sensor.
     * Uses high precision measurement command.
     * @param temperature Reference to store the raw temperature value.
     * @param humidity Reference to store the raw humidity value.
     * @return true on successful read and CRC check, false otherwise.
     */
    bool ReadRaw(uint16_t &temperature, uint16_t &humidity);

    /**
     * @brief Reads temperature in °C and humidity in % RH from the sensor.
     * Uses high precision measurement command.
     * @param temperature Reference to store the temperature in °C.
     * @param humidity Reference to store the relative humidity in %.
     * @return true on successful read and conversion, false otherwise.
     */
    bool Read(float &temperature, float &humidity);

    /**
     * @brief Sends a soft reset command to the sensor.
     * @return true on successful command write, false otherwise.
     */
    bool Reset();

    /**
     * @brief Activates the internal heater with a specified power and duration.
     * Note: This function only sends the command. The caller may need to wait
     * for the heater duration plus measurement time afterwards.
     * @param heater_command The command code corresponding to desired power/duration
     * (e.g., ACTIVATE_HEATER_20MW_1SEC_CMD).
     * @return true on successful command write, false otherwise.
     */
    bool ActivateHeater(uint8_t heater_command = ACTIVATE_HEATER_20MW_01SEC_CMD);

    /**
     * @brief Reads the unique serial number from the sensor.
     * @param serial_number Reference to store the 32-bit serial number.
     * @return true on successful read and CRC check, false otherwise.
     */
    bool ReadSerialNumber(uint32_t &serial_number);

private:
    II2cHal &i2c; // Reference to the platform-agnostic I2C HAL

    /**
     * @brief Calculates the CRC8 checksum for SHT4x data.
     * @param crc_source Pointer to the 2-byte data array.
     * @return The calculated 8-bit CRC value.
     */
    uint8_t CalculateCrc(uint8_t *crc_source);

    /**
     * @brief Platform-agnostic delay function.
     * @param milliseconds Duration to delay in milliseconds.
     */
    void DelayMs(uint32_t milliseconds);
};

#endif /* SHT4X_H_ */