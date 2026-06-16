/*
 * AnalogLevelSensor.h
 *
 * Driver for a generic analog level sensor using ratiometric ADC readings.
 * Uses the IAdcHal interface for platform independence.
 *
 * Created on: April 17, 2025 (Adapted from Nordic version)
 * Author: Gemini
 */

#ifndef DRIVERS_ANALOG_LEVEL_SENSOR_H_
#define DRIVERS_ANALOG_LEVEL_SENSOR_H_

#include "interfaces/IAdcHal.h" // Use HAL ADC interface
#include <stdint.h>

// Platform-agnostic logging includes will be in the .cpp file

/// <summary>
/// Represents a generic analog level sensor that outputs a voltage proportional
/// to the level, typically measured against its supply voltage (ratiometric).
/// Requires two ADC channels: one for the sensor signal, one for the supply voltage.
/// </summary>
class AnalogLevelSensor
{
public:
    /// <summary>
    /// Initializes a new instance of the AnalogLevelSensor class.
    /// </summary>
    /// <param name="fuel_adc_hal_ref">Reference to an initialized IAdcHal object for the fuel sensor signal.</param>
    /// <param name="supply_adc_hal_ref">Reference to an initialized IAdcHal object for the sensor's supply voltage.</param>
    AnalogLevelSensor(IAdcHal &fuel_adc_hal_ref, IAdcHal &supply_adc_hal_ref);

    // Make destructor virtual if this class might be inherited from
    virtual ~AnalogLevelSensor() = default;

    // Delete copy constructor and assignment operator
    AnalogLevelSensor(const AnalogLevelSensor &) = delete;
    AnalogLevelSensor &operator=(const AnalogLevelSensor &) = delete;

    /// <summary>
    /// Reads the sensor signal and supply voltage via ADC, calculates the ratiometric level,
    /// and returns it as a percentage (0-100).
    /// </summary>
    /// <param name="level_percent">Reference to store the calculated level percentage (0-100).</param>
    /// <returns>true if both ADC reads were successful and level was calculated, false otherwise.</returns>
    bool Read(int &level_percent);

    /// <summary>
    /// Reads the sensor signal and supply voltage via ADC, calculates the ratiometric level,
    /// and returns both the percentage and the raw voltage readings.
    /// </summary>
    /// <param name="level_percent">Reference to store the calculated level percentage (0-100).</param>
    /// <param name="fuel_mv">Reference to store the fuel sensor voltage in millivolts.</param>
    /// <param name="supply_mv">Reference to store the supply voltage in millivolts.</param>
    /// <returns>true if both ADC reads were successful and level was calculated, false otherwise.</returns>
    bool Read(int &level_percent, int &fuel_mv, int &supply_mv);

    /// <summary>
    /// Checks if both underlying ADC HAL wrappers are initialized.
    /// </summary>
    /// <returns>true if both ADCs are initialized, false otherwise.</returns>
    bool IsInitialized() const;

private:
    /// <summary>
    /// Reference to the HAL ADC implementation for the fuel sensor signal.
    /// </summary>
    IAdcHal &fuelAdc;

    /// <summary>
    /// Reference to the HAL ADC implementation for the sensor's supply voltage.
    /// </summary>
    IAdcHal &supplyAdc;

    /// <summary>
    /// Tracks the overall initialization status based on underlying ADC HALs.
    /// </summary>
    bool _initialized;
};

#endif /* DRIVERS_ANALOG_LEVEL_SENSOR_H_ */