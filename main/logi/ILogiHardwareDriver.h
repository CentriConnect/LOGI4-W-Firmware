#ifndef I_LOGI_HARDWARE_DRIVER_H
#define I_LOGI_HARDWARE_DRIVER_H

#include "LogiSensorData.h"
#include "StateMachines/StatesCommon.h"
#include <stdbool.h>

/// <summary>
/// Interface for the main hardware orchestration driver.
/// Defines platform-independent methods for initializing hardware,
/// reading sensors, and controlling outputs like LEDs.
/// </summary>
class ILogiHardwareDriver
{
public:
    virtual ~ILogiHardwareDriver() = default;

    /// <summary>
    /// Initializes all necessary underlying hardware drivers and components.
    /// </summary>
    /// <returns>true if initialization was successful, false otherwise.</returns>
    virtual bool Initialize() = 0;

    /// <summary>
    /// Reads sensors repeatedly until moving average filters are full.
    /// Typically called once after initialization.
    /// </summary>
    /// <returns>true if filters were successfully saturated, false otherwise.</returns>
    virtual bool SaturateFilters() = 0;

    /// <summary>
    /// Performs a single update cycle: powers sensors, reads ADC and I2C sensors,
    /// updates filters, and powers down sensors.
    /// </summary>
    virtual void UpdateMeasurements() = 0;

    /// <summary>
    /// Populates the provided LogiSensorData structure with the latest
    /// processed sensor readings from the filters and calculations.
    /// </summary>
    /// <param name="sensorData">Reference to the data structure to fill.</param>
    virtual void GetLatestSensorData(LogiSensorData &sensorData) = 0;

    /// <summary>
    /// Sets up the RGB LED for use (e.g., sets current limit).
    /// </summary>
    virtual void SetupLed() = 0;

    /// <summary>
    /// Sets the RGB LED color to Yellow.
    /// </summary>
    virtual void SetLedYellow() = 0;

    /// <summary>
    /// Sets the RGB LED color to Green.
    /// </summary>
    virtual void SetLedGreen() = 0;

    /// <summary>
    /// Sets the RGB LED color to Red.
    /// </summary>
    virtual void SetLedRed() = 0;

    /// <summary>
    /// Turns the RGB LED on (exits standby).
    /// </summary>
    virtual void TurnLedOn() = 0;

    /// <summary>
    /// Turns the RGB LED off (enters standby).
    /// </summary>
    virtual void TurnLedOff() = 0;

    /// <summary>
    /// Disables the RGB LED completely (turns off channels, enters shutdown/standby).
    /// </summary>
    virtual void DisableLed() = 0;

    /// <summary>
    /// Sets the LED to a semantic state (handles color and blink pattern).
    /// </summary>
    /// <param name="state">The desired LED state.</param>
    virtual void SetLedState(LedState state) = 0;

    /// <summary>
    /// Blinks the LED a specified number of times with simple on/off pattern.
    /// This is a blocking call.
    /// </summary>
    /// <param name="state">The LED color state to blink.</param>
    /// <param name="count">Number of blinks.</param>
    /// <param name="onMs">On duration in milliseconds.</param>
    /// <param name="offMs">Off duration in milliseconds.</param>
    virtual void BlinkLed(LedState state, int count, int onMs, int offMs) = 0;

    /// <summary>
    /// Reads the state of the charging error GPIO pin.
    /// </summary>
    /// <returns>true if charging error is active, false otherwise.</returns>
    virtual bool IsChargingErrorActive() = 0;

    /// <summary>
    /// Reads the state of the power error GPIO pin.
    /// </summary>
    /// <returns>true if power error is active, false otherwise.</returns>
    virtual bool IsPowerErrorActive() = 0;

    virtual bool GetGpsData(GpsData_t& gpsData) = 0;
    virtual void PrintGpsStatus() = 0;
    virtual bool HasValidGpsFix() = 0;

    /// <summary>
    /// Powers the GNSS module on/off independently of a measurement cycle, so it
    /// can acquire a fix in the background (e.g. across the activation cycle).
    /// </summary>
    virtual void SetGnssPower(bool on) = 0;
};

#endif // I_LOGI_HARDWARE_DRIVER_H