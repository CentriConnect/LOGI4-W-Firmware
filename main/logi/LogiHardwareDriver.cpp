#include "logi/LogiHardwareDriver.h"
#include "logi/Faults.h"
#include "sdkconfig.h"
#include <math.h>
#include <cstring>
#include <limits.h>
#include "esp_attr.h"  // For RTC_DATA_ATTR

// RTC-persistent filtered ADC values (survive deep sleep, reset on power cycle)
static RTC_DATA_ATTR int s_last_battery_mv_filtered = 0;
static RTC_DATA_ATTR int s_last_solar_mv_filtered = 0;
static RTC_DATA_ATTR int s_last_batt_temp_mv_filtered = 0; 
static RTC_DATA_ATTR bool s_last_ambient_valid = false;
static RTC_DATA_ATTR float s_last_ambient_c = 0.0f;
static RTC_DATA_ATTR float s_last_humidity_pct = 0.0f;

// Platform-specific includes for logging and delays
#ifdef ESP_PLATFORM 
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static const char *TAG_LOGI_DRV = "LogiHwDriver";
#define LOGI_DRV_LOG_ERR(...) ESP_LOGE(TAG_LOGI_DRV, __VA_ARGS__)
#define LOGI_DRV_LOG_WRN(...) ESP_LOGW(TAG_LOGI_DRV, __VA_ARGS__)
#define LOGI_DRV_LOG_INF(...) ESP_LOGI(TAG_LOGI_DRV, __VA_ARGS__)
#define LOGI_DRV_LOG_DBG(...) ESP_LOGD(TAG_LOGI_DRV, __VA_ARGS__)
#else // Fallback basic logging
#include <stdio.h>
#include <unistd.h> // For usleep
#define LOGI_DRV_LOG_ERR(...)            \
    printf("LogiDrv ERR: " __VA_ARGS__); \
    printf("\n")
#define LOGI_DRV_LOG_WRN(...)            \
    printf("LogiDrv WRN: " __VA_ARGS__); \
    printf("\n")
#define LOGI_DRV_LOG_INF(...)            \
    printf("LogiDrv INF: " __VA_ARGS__); \
    printf("\n")
#define LOGI_DRV_LOG_DBG(...)            \
    printf("LogiDrv DBG: " __VA_ARGS__); \
    printf("\n")
#endif

// --- Constructor ---
LogiHardwareDriver::LogiHardwareDriver(
    TemperatureSensorSht4x &temp_sensor,
    RgbLedPartNumberIs31fl3193 &rgb_led,
    AnalogLevelSensor &analog_level_sensor,
    GPSModuleYIC51009EBGGB &gps_sensor,
    IAdcHal &battery_adc,
    IAdcHal &solar_adc,
    IAdcHal &battery_temp_adc,
    IGpioHal &power_error_gpio,
    IGpioHal &sensor_power_gpio,
    IGpioHal &charging_error_gpio,
    IGpioHal &measure_gpio,
    IGpioHal &gnss_reset_gpio,
    IGpioHal &gnss_enable_gpio)
    : // Initializer list for injected dependencies
      _tempSensor(temp_sensor),
      _rgbLed(rgb_led),
      _analogLevelSensor(analog_level_sensor),
      _gps_sensor(gps_sensor),
      _batteryAdc(battery_adc),
      _solarAdc(solar_adc),
      _batteryTempAdc(battery_temp_adc),
      _powerErrorGpio(power_error_gpio),
      _sensorPowerGpio(sensor_power_gpio),
      _chargingErrorGpio(charging_error_gpio),
      _measureGpio(measure_gpio),
      _gnssResetGpio(gnss_reset_gpio),
      _gnssEnableGpio(gnss_enable_gpio),
      // Initialize filters (using default capacity from MovingAverage.h)
      _batteryMovingAverageFilter(),
      _solarMovingAverageFilter(),
      _batteryTempMovingAverageFilter(),
      _tempSensorTempMovingAverageFilter(),    // Filter for truncated temperature
      _tempSensorHumidityMovingAverageFilter() // Filter for truncated humidity
{
    LOGI_DRV_LOG_INF("LogiHardwareDriver instance created.");
    // Initialize last known values
    _last_fuel_level_percent = 0;
    _last_fuel_mv = 0;
    _last_fuel_supply_mv = 0;
    _last_battery_mv_filtered = 0;
    _last_solar_mv_filtered = 0;
    _last_batt_temp_mv_filtered = 0;
    // Initial filtered temp/hum values will be 0 until filter populated
}

// --- Platform-agnostic Delay ---
void LogiHardwareDriver::DelayMs(uint32_t milliseconds)
{
#ifdef ESP_PLATFORM
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

bool LogiHardwareDriver::InitializeLedWithRecovery(const char* context)
{
    for (int attempt = 1; attempt <= LED_INIT_MAX_ATTEMPTS; ++attempt)
    {
        if (_rgbLed.InitializeDevice())
        {
            if (attempt > 1)
            {
                LOGI_DRV_LOG_INF("%s: RGB LED initialized after I2C recovery", context);
            }
            return true;
        }

        LOGI_DRV_LOG_ERR("%s: RGB LED initialization failed (attempt %d/%d)",
                         context, attempt, LED_INIT_MAX_ATTEMPTS);

        if (attempt >= LED_INIT_MAX_ATTEMPTS || _i2cBusRecoveryHook == nullptr)
        {
            break;
        }

        LOGI_DRV_LOG_WRN("%s: attempting I2C bus recovery before retrying RGB LED init", context);
        if (!_i2cBusRecoveryHook())
        {
            LOGI_DRV_LOG_ERR("%s: I2C bus recovery failed", context);
            break;
        }
        DelayMs(20);
    }

    LOGI_DRV_LOG_ERR("%s: RGB LED is required but did not initialize", context);
    return false;
}

// --- Private Helper Methods ---

// Helper to get filtered mV from raw count filter (Implementation needs refinement - see previous comment)
int LogiHardwareDriver::GetFilteredMillivolts(MovingAverage &filter, IAdcHal &adcHal)
{
    unsigned short filtered_count = 0;
    int filtered_mv = 0;

    filter.getOutput(filtered_count);

    // TODO: Replace this hacky conversion with a better method
    // This attempts to scale based on a current reading, which is inaccurate for non-linear calibration.
    // A CountsToMillivolts method in IAdcHal is the proper solution.
    int raw_now;
    int mv_now;
    if (adcHal.GetCounts(&raw_now) == HAL_ADC_OK && raw_now != 0 && adcHal.GetMillivolts(&mv_now) == HAL_ADC_OK)
    {
        filtered_mv = static_cast<int>(static_cast<double>(filtered_count) * static_cast<double>(mv_now) / static_cast<double>(raw_now));
    }
    else
    {
        LOGI_DRV_LOG_WRN("Could not get ADC conversion factor, using rough estimate for filtered mV.");
        int scalar = (_batteryAdc.IsInitialized() && &adcHal == &_batteryAdc) ? 2 : ((_solarAdc.IsInitialized() && &adcHal == &_solarAdc) ? 2 : 1);
        filtered_mv = (static_cast<int32_t>(filtered_count) * 3300 * scalar) / 4095; // Rough 12-bit, 3.3V estimate
    }
    // --- End HACK ---

    return filtered_mv;
}

void LogiHardwareDriver::UpdateAdcReadingsAndFilters()
{
    int raw_count = 0; // Use int for GetCounts return, then cast
    HalAdcError adcErr;

    // Read battery voltage counts
    adcErr = _batteryAdc.GetCounts(&raw_count);
    if (adcErr == HAL_ADC_OK)
    {
        // Clamp and cast before adding to filter
        if (raw_count < 0)
            raw_count = 0;
        if (raw_count > UINT16_MAX)
            raw_count = UINT16_MAX;
        _batteryMovingAverageFilter.addSample(static_cast<unsigned short>(raw_count));
    }
    else
    {
        LOGI_DRV_LOG_ERR("Failed to read Battery ADC counts: %d", adcErr);
        Faults_Set(FAULT_ADC);
    }

    // Read solar voltage counts
    adcErr = _solarAdc.GetCounts(&raw_count);
    if (adcErr == HAL_ADC_OK)
    {
        if (raw_count < 0)
            raw_count = 0;
        if (raw_count > UINT16_MAX)
            raw_count = UINT16_MAX;
        _solarMovingAverageFilter.addSample(static_cast<unsigned short>(raw_count));
    }
    else
    {
        LOGI_DRV_LOG_ERR("Failed to read Solar ADC counts: %d", adcErr);
        Faults_Set(FAULT_ADC);
    }

    // Read battery temp voltage counts
    adcErr = _batteryTempAdc.GetCounts(&raw_count);
    if (adcErr == HAL_ADC_OK)
    {
        if (raw_count < 0)
            raw_count = 0;
        if (raw_count > UINT16_MAX)
            raw_count = UINT16_MAX;
        _batteryTempMovingAverageFilter.addSample(static_cast<unsigned short>(raw_count));
    }
    else
    {
        LOGI_DRV_LOG_ERR("Failed to read Battery Temp ADC counts: %d", adcErr);
        Faults_Set(FAULT_ADC);
    }

    // Fuel: the head needs the +3.3S rail, so pulse SPS HIGH only for THIS read.
    // bat/sol/temp above were read with SPS LOW (reliable I2C). The fuel read is
    // the internal ADC (no I2C), so the SPS-high bus clamp doesn't matter; supply
    // is the regulated nominal (can't be read over I2C with SPS high).
    _sensorPowerGpio.Write(true);
    DelayMs(50);  // settle the +3.3S rail + fuel head
    if (!_analogLevelSensor.Read(_last_fuel_level_percent, _last_fuel_mv, _last_fuel_supply_mv))
    {
        LOGI_DRV_LOG_ERR("Failed to read Analog Level Sensor");
        Faults_Set(FAULT_FUEL);
        _last_fuel_mv = 0;
        _last_fuel_supply_mv = 0;
    }
    _sensorPowerGpio.Write(false);

    // Update last known filtered mV values AFTER updating filters
    // Use fresh reading if valid, otherwise fall back to RTC-persisted value
    int new_battery_mv = GetFilteredMillivolts(_batteryMovingAverageFilter, _batteryAdc);
    int new_solar_mv = GetFilteredMillivolts(_solarMovingAverageFilter, _solarAdc);
    int new_batt_temp_mv = GetFilteredMillivolts(_batteryTempMovingAverageFilter, _batteryTempAdc);

    // Use new value if valid, otherwise keep RTC-persisted value
    _last_battery_mv_filtered = (new_battery_mv > 0) ? new_battery_mv : s_last_battery_mv_filtered;
    _last_solar_mv_filtered = (new_solar_mv > 0) ? new_solar_mv : s_last_solar_mv_filtered;
    _last_batt_temp_mv_filtered = (new_batt_temp_mv > 0) ? new_batt_temp_mv : s_last_batt_temp_mv_filtered;

    // Persist valid readings to RTC memory for deep sleep survival
    if (new_battery_mv > 0) s_last_battery_mv_filtered = new_battery_mv;
    if (new_solar_mv > 0) s_last_solar_mv_filtered = new_solar_mv;
    if (new_batt_temp_mv > 0) s_last_batt_temp_mv_filtered = new_batt_temp_mv;

    LOGI_DRV_LOG_DBG("ADC Update: Batt %d mV, Solar %d mV, BattTmp %d mV, Level %d %% (Filtered)",
                     _last_battery_mv_filtered, _last_solar_mv_filtered, _last_batt_temp_mv_filtered, _last_fuel_level_percent);
}

void LogiHardwareDriver::UpdateI2cReadingsAndFilters()
{ 
    float temp = 0.0f;
    float hum = 0.0f;

    if (_tempSensor.Read(temp, hum))
    {
        if (temp < -40.0f || temp > 85.0f || hum < 0.0f || hum > 100.0f)
        {
            LOGI_DRV_LOG_WRN("Ignoring out-of-range SHT4x reading: temp %.2f C, hum %.2f%%", temp, hum);
            return;
        }

        // INTENTIONAL: Truncate float temp/hum to unsigned short before filtering.
        // Precision loss is acceptable for this application's filtering needs.
        unsigned short temp_truncated = static_cast<unsigned short>(temp);
        unsigned short hum_truncated = static_cast<unsigned short>(hum);

        _tempSensorTempMovingAverageFilter.addSample(temp_truncated);
        _tempSensorHumidityMovingAverageFilter.addSample(hum_truncated);
        s_last_ambient_valid = true;
        s_last_ambient_c = temp;
        s_last_humidity_pct = hum;

        LOGI_DRV_LOG_DBG("I2C Update: Temp %.2f (Truncated: %u), Hum %.2f (Truncated: %u)",
                         temp, temp_truncated, hum, hum_truncated);
    }
    else
    {
        LOGI_DRV_LOG_ERR("Failed to read Temperature Sensor (SHT4x)");
        Faults_Set(FAULT_AMB);
        // Do not add samples if read failed
    }
}

// --- ILogiHardwareDriver Implementation ---

bool LogiHardwareDriver::Initialize()
{
    LOGI_DRV_LOG_INF("Initializing Logi Hardware Driver...");
    bool success = true;

    // Reset filters
    _batteryMovingAverageFilter.clear();
    _solarMovingAverageFilter.clear();
    _batteryTempMovingAverageFilter.clear();
    _tempSensorTempMovingAverageFilter.clear();
    _tempSensorHumidityMovingAverageFilter.clear();

    if (!_analogLevelSensor.IsInitialized())
    {
        LOGI_DRV_LOG_ERR("Analog Level Sensor ADC(s) not initialized!");
        success = false;
    }
    if (!_batteryAdc.IsInitialized() || !_solarAdc.IsInitialized() || !_batteryTempAdc.IsInitialized())
    {
        LOGI_DRV_LOG_ERR("One or more direct ADCs not initialized!");
        success = false;
    }
    bool ledOk = InitializeLedWithRecovery("Initialize");
    if (!ledOk)
    {
        success = false;
    }

    if (success)
    {
        _measureGpio.Write(false);
        _gnssResetGpio.Write(true);   // GNSS_RESET_N is active LOW - drive HIGH for normal operation
        _gnssEnableGpio.Write(false);  // Start with GNSS power off
        if (ledOk)
        {
            DisableLed();
        }

        // Keep SPS low during startup/provisioning. On this rev-4.1 bench unit,
        // raising SPS here makes the LED's I2C device unreachable even after a
        // full bus rebuild. Measurement paths enable SPS only while sampling.
        _sensorPowerGpio.Write(false);
        LOGI_DRV_LOG_INF("SPS (sensor power) held LOW after init; measurement paths enable it on demand.");
    }

    LOGI_DRV_LOG_INF("Logi Hardware Driver Initialization complete (Success: %d, LED: %d).", success, ledOk);
    return success;
}

bool LogiHardwareDriver::SaturateFilters()
{
    LOGI_DRV_LOG_INF("Saturating filters (Capacity: %d)...", MovingAverage::CAPACITY);
    // SPS stays LOW for the I2C/ADS1015 reads (SPS-high clamps the bus); the fuel
    // read pulses SPS high itself (UpdateAdcReadingsAndFilters). No per-cycle I2C
    // teardown either (it broke back-to-back reads).
    DelayMs(50);
    _measureGpio.Write(true);
    DelayMs(50);  // Allow I2C sensors (SHT4x) to stabilize after power-on
    LOGI_DRV_LOG_INF("Enabling GNSS power (GPIO HIGH)...");
    HalGpioError gnssErr = _gnssEnableGpio.Write(true);  // Enable GNSS power for GPS acquisition
    if (gnssErr != HAL_GPIO_OK)
    {
        LOGI_DRV_LOG_ERR("Failed to enable GNSS GPIO! Error: %d", gnssErr);
    }
    else
    {
        LOGI_DRV_LOG_INF("GNSS GPIO set HIGH successfully");
    }

    // Ensure GNSS_RESET_N is HIGH (active LOW, so HIGH = not in reset)
    _gnssResetGpio.Write(true);
    LOGI_DRV_LOG_INF("GNSS Reset released (HIGH)");

    DelayMs(100);  // Wait for GPS module power stabilization

    // Wake up GPS module from standby mode (if in standby)
    // _gps_sensor.WakeUp();  // DISABLED - testing if this interferes with GPS init

    if (_batteryMovingAverageFilter.isFull())
    {
       LOGI_DRV_LOG_INF("Filters already saturated.");
       _measureGpio.Write(false);
       _sensorPowerGpio.Write(false);
       // REQ-I2C-01 / SPS: do NOT toggle SPS LOW — keeping it HIGH continuously
       // is required for reliable SHT4x I2C reads (LOW->HIGH transition mid-op
       // breaks the bus). See hardware_test.cpp:94 reference pattern.
       _gnssEnableGpio.Write(false);
       return true;
    }

    //LOGI_DRV_LOG_INF("Starting saturation loop...");
    int samples_taken = 0;
    while (!_batteryMovingAverageFilter.isFull())
    {
        UpdateAdcReadingsAndFilters();
        UpdateI2cReadingsAndFilters();
        DelayMs(20); 
        samples_taken++;
        if (samples_taken > MovingAverage::CAPACITY + 50)
        {
            LOGI_DRV_LOG_INF("Filter saturation loop timed out!");
            _measureGpio.Write(false);
            _sensorPowerGpio.Write(false);
            // REQ-I2C-01 / SPS: keep SPS HIGH (see above)
            _gnssEnableGpio.Write(false);
            return false;
        }
    }
    LOGI_DRV_LOG_INF("Saturation loop finished after %d samples.", samples_taken);

    _measureGpio.Write(false);
    _sensorPowerGpio.Write(false);
    // REQ-I2C-01 / SPS: keep SPS HIGH (see above)
    // Note: Keep GNSS enabled - it will be disabled after GPS data is read in GetLatestSensorData

    LOGI_DRV_LOG_INF("Filters saturated.");
    // Log final filtered values (keep this)
    unsigned short filt_temp_u16, filt_hum_u16;
    _tempSensorTempMovingAverageFilter.getOutput(filt_temp_u16);
    _tempSensorHumidityMovingAverageFilter.getOutput(filt_hum_u16);
    LOGI_DRV_LOG_INF("Final Filtered: Batt %d mV, Solar %d mV, BTemp %d mV, Level %d, Temp %u, Hum %u",
                     _last_battery_mv_filtered, _last_solar_mv_filtered, _last_batt_temp_mv_filtered, _last_fuel_level_percent, filt_temp_u16, filt_hum_u16);

    return true;
}

void LogiHardwareDriver::UpdateMeasurements()
{
    LOGI_DRV_LOG_DBG("Updating measurements...");
    // SPS stays LOW for the I2C/ADS1015 reads (SPS-high clamps the bus -> bad
    // bat/sol/supply). UpdateAdcReadingsAndFilters pulses SPS high only for the
    // fuel internal-ADC read. No per-cycle I2C teardown either (it broke back-to-back).
    DelayMs(50);
    _measureGpio.Write(true);
    DelayMs(50);  // Allow I2C sensors (SHT4x) to stabilize after power-on
    _gnssEnableGpio.Write(true);  // Enable GNSS power for GPS acquisition
    _gnssResetGpio.Write(true);   // Ensure GNSS_RESET_N is HIGH (not in reset)
    DelayMs(100);  // Wait for GPS module power stabilization

    // Read I2C sensor FIRST before any GPS activity
    UpdateI2cReadingsAndFilters(); // Now filters temp/hum (with truncation)

    UpdateAdcReadingsAndFilters();

    _measureGpio.Write(false);
    _sensorPowerGpio.Write(false);
    // REQ-I2C-01 / SPS: keep SPS HIGH (see SaturateFilters comment)
    // Note: Keep GNSS enabled - it will be disabled after GPS data is read
    LOGI_DRV_LOG_DBG("Measurements updated.");

    // Log final filtered values for debugging
    unsigned short filt_temp_u16, filt_hum_u16;
    _tempSensorTempMovingAverageFilter.getOutput(filt_temp_u16);
    _tempSensorHumidityMovingAverageFilter.getOutput(filt_hum_u16);
    LOGI_DRV_LOG_DBG("Updated Values: Batt %d mV, Solar %d mV, BTemp %d mV, Level %d, Temp %u, Hum %u",
                     _last_battery_mv_filtered, _last_solar_mv_filtered, _last_batt_temp_mv_filtered, _last_fuel_level_percent, filt_temp_u16, filt_hum_u16);
}

float LogiHardwareDriver::NtcMillivoltsToTemperature(int voltage_mv)
{
    if (voltage_mv <= 0)
    {
        LOGI_DRV_LOG_WRN("NTC voltage reading is zero or negative (%d mV), cannot calculate temperature.", voltage_mv);
        return -273.15f;
    }

    // Using filtered battery voltage as supply for divider
    double v_measured = static_cast<double>(voltage_mv) / 1000.0;
    double v_supply = static_cast<double>(_last_battery_mv_filtered) / 1000.0;

    if (v_supply <= 0 || v_measured >= v_supply)
    {
        LOGI_DRV_LOG_WRN("Invalid NTC voltage: Vmeas %f V, Vsupply %f V", v_measured, v_supply);
        return -273.15f;
    }

    double ntc_resistance = (v_measured * FIXED_RESISTOR) / (v_supply - v_measured);

    if (ntc_resistance <= 0)
    {
        LOGI_DRV_LOG_WRN("Calculated NTC resistance is zero or negative (%f Ohm).", ntc_resistance);
        return -273.15f;
    }

    double temp_kelvin = 1.0 / ((1.0 / NTC_T25_KELVIN) +
                                (1.0 / NTC_B_CONSTANT) *
                                    log(ntc_resistance / NTC_R25));

    double temp_celsius = temp_kelvin - 273.15;

    return static_cast<float>(temp_celsius);
}

void LogiHardwareDriver::GetLatestSensorData(LogiSensorData &sensorData)
{
    // Populate the structure with the latest processed values

    sensorData.elapsedTimeStampS = 0; // Placeholder - Needs ITimeKeeper

    // --- Fuel ---
    sensorData.PublishedFuelLevel = static_cast<uint8_t>(_last_fuel_level_percent);
    // Use actual voltage readings from the AnalogLevelSensor
    sensorData.AnalogFuelVoltage = static_cast<float>(_last_fuel_mv) / 1000.0f;
    sensorData.SensorSupplyVoltage = static_cast<float>(_last_fuel_supply_mv) / 1000.0f;

    // --- Battery ---
    sensorData.AnalogBatteryVoltage = static_cast<float>(_last_battery_mv_filtered) / 1000.0f;
    float batt_lvl = ((sensorData.AnalogBatteryVoltage - 3.0f) / (4.2f - 3.0f)) * 100.0f; // Example LiPo scale
    if (batt_lvl < 0)
        batt_lvl = 0;
    if (batt_lvl > 100)
        batt_lvl = 100;
    sensorData.PublishedBatteryLevel = static_cast<uint8_t>(round(batt_lvl));
    sensorData.BatteryTemperatureC = NtcMillivoltsToTemperature(_last_batt_temp_mv_filtered);

    // --- Solar ---
    sensorData.SolarVoltage = static_cast<float>(_last_solar_mv_filtered) / 1000.0f;

    // --- Environment ---
    // Retrieve filtered (truncated) counts and cast back to float
    unsigned short filt_temp_u16 = 0;
    unsigned short filt_hum_u16 = 0;
    bool tempOk = _tempSensorTempMovingAverageFilter.getOutput(filt_temp_u16);
    bool humOk = _tempSensorHumidityMovingAverageFilter.getOutput(filt_hum_u16);
    if (tempOk && humOk)
    {
        // WARNING: Casting back to float does not recover lost precision.
        sensorData.MeasuredTemperatureC = static_cast<float>(filt_temp_u16);
        sensorData.MeasuredHumidityPercentage = static_cast<float>(filt_hum_u16);
    }
    else if (s_last_ambient_valid)
    {
        sensorData.MeasuredTemperatureC = s_last_ambient_c;
        sensorData.MeasuredHumidityPercentage = s_last_humidity_pct;
    }
    else
    {
        LOGI_DRV_LOG_WRN("No valid ambient sample available; reporting ambient as 0");
        sensorData.MeasuredTemperatureC = 0.0f;
        sensorData.MeasuredHumidityPercentage = 0.0f;
    }

    _gps_sensor.GetGpsData(sensorData.GPSData);

    // Disable GNSS power after reading GPS data to save power
    _gnssEnableGpio.Write(false);

    // Latch power-fault + low-battery into the per-post fault accumulator while the
    // snapshot is assembled. This runs every measurement cycle, so the faults ride
    // every post (including activation). CHG (IO23) is a charging-status line, not
    // an error, so it is intentionally NOT mapped to a fault.
    if (IsPowerErrorActive())
    {
        Faults_Set(FAULT_PWR);
    }
    const float lowbat_threshold_v = static_cast<float>(CONFIG_LOGI_BATTERY_VOLTAGE_POST_THRESHOLD_10X) / 10.0f;
    if (sensorData.AnalogBatteryVoltage > 0.0f && sensorData.AnalogBatteryVoltage < lowbat_threshold_v)
    {
        Faults_Set(FAULT_LOWBAT);
    }
}

void LogiHardwareDriver::SetGnssPower(bool on)
{
    // Power the GNSS module independently of a measurement cycle so it can
    // acquire a fix in the background (e.g. across the activation cycle).
    _gnssEnableGpio.Write(on);
    if (on)
    {
        _gnssResetGpio.Write(true);  // release GNSS_RESET_N so the module runs
    }
    LOGI_DRV_LOG_DBG("GNSS power %s", on ? "ON" : "OFF");
}

// --- LED Control --- (Implementation remains the same as previous version)
void LogiHardwareDriver::SetupLed()
{
    LOGI_DRV_LOG_DBG("Setting up LED...");
    if (!InitializeLedWithRecovery("SetupLed"))
    {
        return;
    }
    if (!_rgbLed.SetGlobalCurrent(LED_OUTPUT_CURRENT_REG_VALUE))
    {
        LOGI_DRV_LOG_ERR("Failed to set LED current in SetupLed");
    }
    LOGI_DRV_LOG_DBG("LED Setup Complete.");
}
void LogiHardwareDriver::SetLedYellow()
{
    LOGI_DRV_LOG_DBG("Setting LED Yellow");
    _rgbLed.SetRGB(LED_BRIGHTNESS_YELLOW_RED, LED_BRIGHTNESS_YELLOW_GREEN, 0);
}
void LogiHardwareDriver::SetLedGreen()
{
    LOGI_DRV_LOG_DBG("Setting LED Green");
    _rgbLed.SetRGB(0, LED_BRIGHTNESS_GREEN, 0);
}
void LogiHardwareDriver::SetLedRed()
{
    LOGI_DRV_LOG_DBG("Setting LED Red");
    _rgbLed.SetRGB(LED_BRIGHTNESS_RED, 0, 0);
}
void LogiHardwareDriver::TurnLedOn()
{
    LOGI_DRV_LOG_DBG("Turning LED On");
    _rgbLed.SetLedStandby(false);
}
void LogiHardwareDriver::TurnLedOff()
{
    LOGI_DRV_LOG_DBG("Turning LED Off");
    _rgbLed.SetLedStandby(true);
}
void LogiHardwareDriver::DisableLed()
{
    LOGI_DRV_LOG_DBG("Disabling LED");
    _rgbLed.SetRGB(0, 0, 0);
    _rgbLed.ShutdownDevice();
    _rgbLed.SetLedStandby(true);
}

void LogiHardwareDriver::SetLedState(LedState state)
{
    LOGI_DRV_LOG_DBG("SetLedState: %d", static_cast<int>(state));

    uint8_t r = 0, g = 0, b = 0;
    switch (state)
    {
        case LedState::LedState_GreenBlink:
            g = LED_BRIGHTNESS_GREEN;
            break;
        case LedState::LedState_YellowBlink:
            r = LED_BRIGHTNESS_YELLOW_RED;
            g = LED_BRIGHTNESS_YELLOW_GREEN;
            break;
        case LedState::LedState_BlueBlink:
            b = LED_BRIGHTNESS_BLUE;
            break;
        case LedState::LedState_RedBlink:
        case LedState::LedState_RedFastBlink:
        case LedState::LedState_RedSolid:
            r = LED_BRIGHTNESS_RED;
            break;
        case LedState::LedState_Off:
            break;
        default:
            LOGI_DRV_LOG_WRN("Unknown LED state: %d", static_cast<int>(state));
            return;
    }

    if (state == LedState::LedState_Off)
    {
        _rgbLed.SetBreathingMode(false);
        _rgbLed.SetRGB(0, 0, 0);
        _rgbLed.SetLedStandby(true);
    }
    else
    {
        _rgbLed.SetLedStandby(false);
        if (!InitializeLedWithRecovery("SetLedState"))
        {
            return;
        }
        if (!_rgbLed.SetBreathingMode(false))
        {
            LOGI_DRV_LOG_ERR("SetLedState: failed to disable LED breathing mode");
            return;
        }
    }

    _ledTaskR = r;
    _ledTaskG = g;
    _ledTaskB = b;
    _ledTaskState = state;
    EnsureLedTask();
}

void LogiHardwareDriver::EnsureLedTask()
{
    if (_ledTaskHandle == nullptr)
    {
        xTaskCreate(LedTaskTrampoline, "led_blink", LED_TASK_STACK_BYTES, this, 3, &_ledTaskHandle);
    }
}

void LogiHardwareDriver::LedTaskTrampoline(void* arg)
{
    static_cast<LogiHardwareDriver*>(arg)->LedTaskLoop();
}

void LogiHardwareDriver::LedTaskLoop()
{
    bool ledOn = false;
    LedState lastSeen = LedState::LedState_Off;
    int consecutiveFailures = 0;
    bool i2cLatched = false;      // v1.2.1: LED unreachable - stop touching the bus
    bool recoveryAttempted = false; // v1.2.1: one bus-recovery attempt per pattern

    while (true)
    {
        LedState state = _ledTaskState;
        uint8_t r = _ledTaskR, g = _ledTaskG, b = _ledTaskB;

        if (state != lastSeen)
        {
            ledOn = false;
            lastSeen = state;
            // New pattern requested: allow a fresh attempt even if previously latched.
            consecutiveFailures = 0;
            i2cLatched = false;
            recoveryAttempted = false;
        }

        if (i2cLatched)
        {
            // v1.2.1: LED I2C writes failed LED_MAX_CONSECUTIVE_I2C_FAILURES
            // times in a row (dead/clamped bus). Idle until the pattern changes
            // instead of spamming the bus + error log forever (the unbounded
            // error path previously overflowed this task's stack -> panic loop).
            vTaskDelay(pdMS_TO_TICKS(LED_IDLE_POLL_MS));
            continue;
        }

        const bool isBlinking =
            (state == LedState::LedState_GreenBlink) ||
            (state == LedState::LedState_YellowBlink) ||
            (state == LedState::LedState_BlueBlink)   ||
            (state == LedState::LedState_RedBlink)    ||
            (state == LedState::LedState_RedFastBlink);
        const bool isSolid = (state == LedState::LedState_RedSolid);

        bool writeOk = true;
        if (isBlinking)
        {
            if (ledOn)
            {
                writeOk = _rgbLed.SetRGB(0, 0, 0);
                ledOn = false;
                vTaskDelay(pdMS_TO_TICKS(LED_BLINK_OFF_MS));
            }
            else
            {
                writeOk = _rgbLed.SetRGB(r, g, b);
                ledOn = true;
                vTaskDelay(pdMS_TO_TICKS(LED_BLINK_ON_MS));
            }
        }
        else if (isSolid)
        {
            if (!ledOn)
            {
                writeOk = _rgbLed.SetRGB(r, g, b);
                ledOn = true;
            }
            vTaskDelay(pdMS_TO_TICKS(LED_IDLE_POLL_MS));
        }
        else
        {
            // Off / unknown — task leaves the LED alone; SetLedState(Off) already drove it.
            ledOn = false;
            vTaskDelay(pdMS_TO_TICKS(LED_IDLE_POLL_MS));
        }

        if (!writeOk)
        {
            consecutiveFailures++;
            if (consecutiveFailures >= LED_MAX_CONSECUTIVE_I2C_FAILURES)
            {
                // v1.2.1: try one full bus rebuild before giving up on the LED.
                if (_i2cBusRecoveryHook != nullptr && !recoveryAttempted)
                {
                    recoveryAttempted = true;
                    LOGI_DRV_LOG_ERR("led_blink: %d consecutive LED I2C failures - attempting I2C bus recovery",
                                     consecutiveFailures);
                    if (_i2cBusRecoveryHook())
                    {
                        consecutiveFailures = 0;
                        if (!InitializeLedWithRecovery("led_blink"))
                        {
                            i2cLatched = true;
                        }
                        continue;
                    }
                }
                LOGI_DRV_LOG_ERR("led_blink: %d consecutive LED I2C failures - disabling LED updates until pattern changes (state=%d)",
                                 consecutiveFailures, static_cast<int>(state));
                i2cLatched = true;
            }
        }
        else
        {
            consecutiveFailures = 0;
        }
    }
}

void LogiHardwareDriver::BlinkLed(LedState state, int count, int onMs, int offMs)
{
    LOGI_DRV_LOG_DBG("BlinkLed: state=%d, count=%d, onMs=%d, offMs=%d",
                     static_cast<int>(state), count, onMs, offMs);

    // Ensure LED is initialized and breathing mode is off
    _rgbLed.SetLedStandby(false);
    if (!InitializeLedWithRecovery("BlinkLed"))
    {
        return;
    }
    if (!_rgbLed.SetBreathingMode(false))
    {
        LOGI_DRV_LOG_ERR("BlinkLed: failed to disable LED breathing mode");
        return;
    }

    // Set color based on state
    bool colorOk = true;
    switch (state)
    {
        case LedState::LedState_GreenBlink:
            colorOk = _rgbLed.SetRGB(0, LED_BRIGHTNESS_GREEN, 0);
            break;
        case LedState::LedState_YellowBlink:
            colorOk = _rgbLed.SetRGB(LED_BRIGHTNESS_YELLOW_RED, LED_BRIGHTNESS_YELLOW_GREEN, 0);
            break;
        case LedState::LedState_BlueBlink:
            colorOk = _rgbLed.SetRGB(0, 0, LED_BRIGHTNESS_BLUE);
            break;
        case LedState::LedState_RedBlink:
        case LedState::LedState_RedFastBlink:
        case LedState::LedState_RedSolid:
            colorOk = _rgbLed.SetRGB(LED_BRIGHTNESS_RED, 0, 0);
            break;
        default:
            LOGI_DRV_LOG_WRN("BlinkLed: unsupported state %d", static_cast<int>(state));
            return;
    }
    if (!colorOk)
    {
        LOGI_DRV_LOG_ERR("BlinkLed: failed to set LED color - skipping blink request");
        return;
    }

    // Simple blocking blink loop
    for (int i = 0; i < count; i++)
    {
        // LED on
        _rgbLed.SetLedStandby(false);
        DelayMs(static_cast<uint32_t>(onMs));

        // LED off
        _rgbLed.SetLedStandby(true);
        DelayMs(static_cast<uint32_t>(offMs));
    }

    // Leave LED off when done
    _rgbLed.SetLedStandby(true);
}

// --- GPIO Input Reading --- (Implementation remains the same)
bool LogiHardwareDriver::IsChargingErrorActive()
{
    return _chargingErrorGpio.Read() == 1;
}
bool LogiHardwareDriver::IsPowerErrorActive()
{
    // Active-low fault: the power-error input idles HIGH (normal) and is pulled LOW
    // on a fault (PCBA checkout confirms pin level 1 == NORMAL).
    return _powerErrorGpio.Read() == 0;
}

bool LogiHardwareDriver::GetGpsData(GpsData_t& gpsData)
{
    return _gps_sensor.GetGpsData(gpsData);
}

void LogiHardwareDriver::PrintGpsStatus()
{
    _gps_sensor.PrintStatus();
}

bool LogiHardwareDriver::HasValidGpsFix()
{
    return _gps_sensor.HasValidFix();
}
