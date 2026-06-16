#ifndef ESP_ADC_WRAPPER_CONFIG_H
#define ESP_ADC_WRAPPER_CONFIG_H

#include "esp_adc/adc_oneshot.h" // For adc_unit_t, adc_channel_t, adc_atten_t, adc_bitwidth_t
#include <stdint.h>              // For uint8_t

/// <summary>
/// Configuration structure for the EspAdcWrapper class.
/// Encapsulates the parameters needed to initialize an ADC channel wrapper.
/// </summary>
struct EspAdcWrapperConfig
{
    /// <summary>
    /// The ADC unit to use (e.g., ADC_UNIT_1).
    /// </summary>
    adc_unit_t unit = ADC_UNIT_1; // Default ADC_UNIT_1

    /// <summary>
    /// The specific ADC channel on the unit to configure.
    /// </summary>
    adc_channel_t channel = ADC_CHANNEL_0; // Default Channel 0

    /// <summary>
    /// The attenuation level for the channel. Determines the measurable voltage range.
    /// (e.g., ADC_ATTEN_DB_0, ADC_ATTEN_DB_2_5, ADC_ATTEN_DB_6, ADC_ATTEN_DB_11).
    /// </summary>
    adc_atten_t attenuation = ADC_ATTEN_DB_11; // Default max attenuation/range

    /// <summary>
    /// The desired bitwidth for the ADC readings.
    /// (e.g., ADC_BITWIDTH_DEFAULT, ADC_BITWIDTH_12).
    /// </summary>
    adc_bitwidth_t bitwidth = ADC_BITWIDTH_DEFAULT; // Default bitwidth

    /// <summary>
    /// An optional scalar applied to the final millivolt reading after calibration.
    /// Defaults to 1 (no scaling).
    /// </summary>
    float millivoltScalar = 1.0f;
};

#endif // ESP_ADC_WRAPPER_CONFIG_H