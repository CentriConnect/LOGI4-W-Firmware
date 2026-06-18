/*
 * AnalogLevelSensor.cpp
 *
 * Driver for a generic analog level sensor using ratiometric ADC readings.
 * Uses the IAdcHal interface for platform independence.
 *
 * Created on: April 17, 2025 (Adapted from Nordic version)
 * Author: Gemini
 */

#include "drivers/AnalogLevelSensor.h"

// Platform-specific logging includes
#ifdef ESP_PLATFORM // Using ESP-IDF
#include "esp_log.h"
static const char *TAG_ANALOG_LS = "AnalogLSensor";
#define ANALOG_LS_LOG_ERR(...) ESP_LOGE(TAG_ANALOG_LS, __VA_ARGS__)
#define ANALOG_LS_LOG_WRN(...) ESP_LOGW(TAG_ANALOG_LS, __VA_ARGS__)
#define ANALOG_LS_LOG_INF(...) ESP_LOGI(TAG_ANALOG_LS, __VA_ARGS__)
#define ANALOG_LS_LOG_DBG(...) ESP_LOGD(TAG_ANALOG_LS, __VA_ARGS__)
#else // Fallback basic logging
#include <stdio.h>
#define ANALOG_LS_LOG_ERR(...)            \
    printf("AnalogLS ERR: " __VA_ARGS__); \
    printf("\n")
#define ANALOG_LS_LOG_WRN(...)            \
    printf("AnalogLS WRN: " __VA_ARGS__); \
    printf("\n")
#define ANALOG_LS_LOG_INF(...)            \
    printf("AnalogLS INF: " __VA_ARGS__); \
    printf("\n")
#define ANALOG_LS_LOG_DBG(...)            \
    printf("AnalogLS DBG: " __VA_ARGS__); \
    printf("\n")
#endif

// +3.3S supply rail is regulated; with the SPS-LOW design it can't be read back
// over I2C while SPS is pulsed high (the ADS1015 bus clamps), so the ratiometric
// fuel calc uses this nominal instead of an unreliable live read.
static constexpr int NOMINAL_SUPPLY_MV = 3300;

// --- Constructor ---
AnalogLevelSensor::AnalogLevelSensor(IAdcHal &fuel_adc_hal_ref, IAdcHal &supply_adc_hal_ref)
    : fuelAdc(fuel_adc_hal_ref),
      supplyAdc(supply_adc_hal_ref),
      _initialized(false) // Initialize as false
{
    // Check if the provided HAL instances are initialized
    if (!fuelAdc.IsInitialized())
    {
        ANALOG_LS_LOG_ERR("Fuel ADC HAL provided is not initialized!");
    }
    if (!supplyAdc.IsInitialized())
    {
        ANALOG_LS_LOG_ERR("Supply ADC HAL provided is not initialized!");
    }

    // SPS-LOW design uses a nominal supply, so only the fuel ADC must be ready.
    _initialized = fuelAdc.IsInitialized();

    if (_initialized)
    {
        ANALOG_LS_LOG_INF("AnalogLevelSensor driver instance created and ADCs initialized.");
    }
    else
    {
        ANALOG_LS_LOG_ERR("AnalogLevelSensor driver instance created, but one or both ADCs are NOT initialized.");
    }
}

// --- Public Methods ---

bool AnalogLevelSensor::IsInitialized() const
{
    // Return the status determined in the constructor
    // Optionally re-check the underlying HALs if their state can change after construction
    // return _initialized && fuelAdc.IsInitialized() && supplyAdc.IsInitialized();
    return _initialized;
}

bool AnalogLevelSensor::Read(int &level_percent)
{
    int fuel_mv_unused = 0;
    int supply_mv_unused = 0;
    return Read(level_percent, fuel_mv_unused, supply_mv_unused);
}

bool AnalogLevelSensor::Read(int &level_percent, int &fuel_mv, int &supply_mv)
{
    if (!IsInitialized())
    {
        ANALOG_LS_LOG_ERR("Read failed: Sensor not initialized.");
        level_percent = 0;
        fuel_mv = 0;
        supply_mv = 0;
        return false;
    }

    fuel_mv = 0;
    supply_mv = 0;
    HalAdcError fuel_err;

    // Read fuel sensor voltage (internal ADC; caller has pulsed SPS HIGH so the
    // head is powered). No I2C here -> unaffected by the SPS-high bus clamp.
    fuel_err = fuelAdc.GetMillivolts(&fuel_mv);
    if (fuel_err != HAL_ADC_OK)
    {
        ANALOG_LS_LOG_ERR("Failed to read fuel ADC (Error: %d)", fuel_err);
        level_percent = 0;
        fuel_mv = 0;
        supply_mv = 0;
        return false;
    }

    // SPS-LOW design: +3.3S can't be read over I2C while SPS is high (ADS1015
    // bus clamps), so use the regulated nominal for the ratiometric calc.
    supply_mv = NOMINAL_SUPPLY_MV;

    // Calculate ratiometric level percentage
    // Ensure intermediate calculation uses floating-point
    float ratio = static_cast<float>(fuel_mv) / static_cast<float>(supply_mv);
    int calculated_level = static_cast<int>(ratio * 100.0f);

    // Clamp result to 0-100%
    if (calculated_level < 0)
    {
        level_percent = 0;
    }
    else if (calculated_level > 100)
    {
        level_percent = 100;
    }
    else
    {
        level_percent = calculated_level;
    }

    ANALOG_LS_LOG_DBG("Read successful. Fuel: %d mV, Supply: %d mV, Level: %d %%",
                      fuel_mv, supply_mv, level_percent);

    return true;
}