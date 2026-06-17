#ifndef LOGI_HARDWARE_DRIVER_H
#define LOGI_HARDWARE_DRIVER_H

#include "logi/ILogiHardwareDriver.h"
#include "LogiSensorData.h"
#include "helpers/MovingAverage.h" 
#include "drivers/TemperatureSensorSht4x.h"
#include "drivers/RgbLedPartNumberIs31fl3193.h"
#include "drivers/AnalogLevelSensor.h"
#include "drivers/GPSModuleYIC51009EBGGB.h"
#include "interfaces/IAdcHal.h"
#include "interfaces/IGpioHal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdint.h>
#include <math.h>

/// <summary>
/// Platform-independent driver coordinating various hardware components
/// for the specific "Logi" device application logic.
/// Implements the ILogiHardwareDriver interface.
/// Replicates original behavior, including implicit float->ushort conversion for temp/humidity filters.
/// </summary>
class LogiHardwareDriver : public ILogiHardwareDriver
{
public:
    /// <summary>
    /// Constructor - dependencies are injected here.
    /// </summary>
    LogiHardwareDriver(
        // Component Drivers
        TemperatureSensorSht4x &temp_sensor,
        RgbLedPartNumberIs31fl3193 &rgb_led,
        AnalogLevelSensor &analog_level_sensor,
        GPSModuleYIC51009EBGGB &gps_sensor,

        // Direct ADC HAL Interfaces (Needed for converting filtered counts back to mV)
        IAdcHal &battery_adc,
        IAdcHal &solar_adc,
        IAdcHal &battery_temp_adc,

        // Direct GPIO HAL Interfaces
        IGpioHal &power_error_gpio,
        IGpioHal &sensor_power_gpio,
        IGpioHal &charging_error_gpio,
        IGpioHal &measure_gpio,
        IGpioHal &gnss_reset_gpio,
        IGpioHal &gnss_enable_gpio);

    virtual ~LogiHardwareDriver() = default;

    // Delete copy constructor/assignment
    LogiHardwareDriver(const LogiHardwareDriver &) = delete;
    LogiHardwareDriver &operator=(const LogiHardwareDriver &) = delete;

    // --- ILogiHardwareDriver Implementation ---
    bool Initialize() override;
    bool SaturateFilters() override;
    void UpdateMeasurements() override;
    void GetLatestSensorData(LogiSensorData &sensorData) override;
    void SetupLed() override;
    void SetLedYellow() override;
    void SetLedGreen() override;
    void SetLedRed() override;
    void TurnLedOn() override;
    void TurnLedOff() override;
    void DisableLed() override;
    void SetLedState(LedState state) override;
    void BlinkLed(LedState state, int count, int onMs, int offMs) override;
    bool IsChargingErrorActive() override;
    bool IsPowerErrorActive() override;
    bool GetGpsData(GpsData_t& gpsData);
    void PrintGpsStatus();
    bool HasValidGpsFix();
    void SetGnssPower(bool on) override;

    /// v1.2.1: optional platform hook to rebuild the I2C bus when transactions
    /// are persistently failing (wedged bus driver). Set by the hardware factory.
    typedef bool (*I2cBusRecoveryHook)();
    void SetI2cBusRecoveryHook(I2cBusRecoveryHook hook) { _i2cBusRecoveryHook = hook; }

private:
    // --- Injected Dependencies ---
    TemperatureSensorSht4x &_tempSensor;
    RgbLedPartNumberIs31fl3193 &_rgbLed;
    AnalogLevelSensor &_analogLevelSensor;
    GPSModuleYIC51009EBGGB &_gps_sensor;
    IAdcHal &_batteryAdc;
    IAdcHal &_solarAdc;
    IAdcHal &_batteryTempAdc;
    IGpioHal &_powerErrorGpio;
    IGpioHal &_sensorPowerGpio;
    IGpioHal &_chargingErrorGpio;
    IGpioHal &_measureGpio;
    IGpioHal &_gnssResetGpio;
    IGpioHal &_gnssEnableGpio;

    // --- Internal State & Helpers ---
    // Filters store raw counts (unsigned short) or truncated floats
    MovingAverage _batteryMovingAverageFilter;
    MovingAverage _solarMovingAverageFilter;
    MovingAverage _batteryTempMovingAverageFilter;
    MovingAverage _tempSensorTempMovingAverageFilter;     
    MovingAverage _tempSensorHumidityMovingAverageFilter; 

    // Store latest PROCESSED values needed across methods
    int _last_fuel_level_percent = 0;
    int _last_fuel_mv = 0;           // Actual fuel sensor ADC voltage
    int _last_fuel_supply_mv = 0;    // Actual fuel sensor supply voltage
    int _last_battery_mv_filtered = 0;
    int _last_solar_mv_filtered = 0;
    int _last_batt_temp_mv_filtered = 0;
    // No need to store latest float temp/hum separately if using filters

    /// <summary>
    /// Reads raw ADC counts, updates filters.
    /// Assumes sensors are powered ON. Also calls AnalogLevelSensor::Read.
    /// </summary>
    void UpdateAdcReadingsAndFilters();

    /// <summary>
    /// Reads I2C sensor values (temp/humidity), truncates floats to unsigned short,
    /// and updates filters. Assumes sensors are powered ON.
    /// </summary>
    void UpdateI2cReadingsAndFilters(); // Renamed to reflect filtering
    
    /// <summary>
    /// Converts NTC ADC voltage reading (in millivolts) to temperature.
    /// </summary>
    float NtcMillivoltsToTemperature(int voltage_mv);

    /// <summary>
    /// Platform-agnostic delay function.
    /// </summary>
    void DelayMs(uint32_t milliseconds);

    /// <summary>
    /// Helper to get filtered mV value from raw counts filter.
    /// </summary>
    int GetFilteredMillivolts(MovingAverage &filter, IAdcHal &adcHal);

    // NTC calculation constants
    static constexpr double NTC_B_CONSTANT = 3435.0;
    static constexpr double NTC_R25 = 10000.0;
    static constexpr double NTC_T25_KELVIN = 298.15;
    static constexpr double FIXED_RESISTOR = 10000.0;

    // LED constants
    static const uint8_t LED_BRIGHTNESS_YELLOW_RED = 200;
    static const uint8_t LED_BRIGHTNESS_YELLOW_GREEN = 200;
    static const uint8_t LED_BRIGHTNESS_GREEN = 200;
    static const uint8_t LED_BRIGHTNESS_RED = 200;
    static const uint8_t LED_BRIGHTNESS_BLUE = 200;
    static const uint8_t LED_OUTPUT_CURRENT_REG_VALUE = 0x50;

    // Software-driven LED blink (replaces IS31FL3193 hardware breathing).
    // Hard 100 ms on / 900 ms off square wave at 1 Hz for every blink state;
    // solid states stay solid.
    static constexpr uint32_t LED_BLINK_ON_MS = 100;
    static constexpr uint32_t LED_BLINK_OFF_MS = 900;
    static constexpr uint32_t LED_IDLE_POLL_MS = 250;

    // v1.2.1: led_blink task stack. 2048 overflowed (C6 HW stack guard panic,
    // Nick bug report 2026-06-11) when SetRGB failures cascaded into the deep
    // i2c.master error/clear-bus/logging path.
    static constexpr uint32_t LED_TASK_STACK_BYTES = 4096;
    // v1.2.1: after this many consecutive failed SetRGB attempts the task
    // latches the LED off and stops touching the I2C bus until the requested
    // pattern changes. Prevents an unreachable LED (dead/clamped bus) from
    // spamming errors forever and starving/crashing the system.
    static constexpr int LED_MAX_CONSECUTIVE_I2C_FAILURES = 3;
    static constexpr int LED_INIT_MAX_ATTEMPTS = 2;

    volatile LedState _ledTaskState = LedState::LedState_Off;
    volatile uint8_t _ledTaskR = 0;
    volatile uint8_t _ledTaskG = 0;
    volatile uint8_t _ledTaskB = 0;
    TaskHandle_t _ledTaskHandle = nullptr;
    I2cBusRecoveryHook _i2cBusRecoveryHook = nullptr;

    void EnsureLedTask();
    bool InitializeLedWithRecovery(const char* context);
    static void LedTaskTrampoline(void* arg);
    void LedTaskLoop();
};

#endif // LOGI_HARDWARE_DRIVER_H
