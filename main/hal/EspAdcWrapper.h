#ifndef ESP_ADC_WRAPPER_H
#define ESP_ADC_WRAPPER_H

#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_log.h"
#include "EspAdcWrapperConfig.h"
#include "interfaces/IAdcHal.h"
#include <stdint.h>

class EspAdcWrapper : public IAdcHal
{
public:
    /// <summary>
    /// Constructor. Stores configuration and shared ADC unit handle.
    /// Does not initialize the channel yet. Call Initialize() separately.
    /// </summary>
    /// <param name="unit_handle">Handle to the already initialized ADC unit.</param>
    /// <param name="config">Configuration specific to this channel (channel, atten, bitwidth, scalar).</param>
    EspAdcWrapper(adc_oneshot_unit_handle_t unit_handle, const EspAdcWrapperConfig &config);

    virtual ~EspAdcWrapper();

    // --- IAdcHal Interface Implementation ---
    /// <summary>
    /// Initializes the channel on the unit.
    /// </summary>
    HalAdcError Initialize() override;

    /// <summary>
    /// Checks if the channel has been initialized.
    /// </summary>
    /// <returns>True if initialized, false otherwise.</returns>
    bool IsInitialized() const override;

    /// <summary>
    /// Gets raw ADC counts.
    /// </summary>
    /// <param name="out_raw">Pointer to store the raw count value.</param>
    /// <returns>HalAdcError code.</returns>
    HalAdcError GetCounts(int *out_raw) override;

    /// <summary>
    /// Gets voltage in millivolts.
    /// </summary>
    /// <param name="out_voltage">Pointer to store the voltage value.</param>
    /// <returns>HalAdcError code.</returns>
    HalAdcError GetMillivolts(int *out_voltage) override;

    // --- EspAdcWrapper Specific Methods ---
    /// <summary>
    /// Sets the millivolt scalar value.
    /// </summary>
    /// <param name="scalar">New scalar value.</param>
    /// <returns>The set scalar value (might be adjusted if invalid).</returns>
    uint8_t SetMillivoltScalar(uint8_t scalar);

    /// <summary>
    /// Gets the current millivolt scalar value.
    /// </summary>
    /// <returns>Current scalar value.</returns>
    uint8_t GetMillivoltScalar() const;

    /// <summary>
    /// Checks if calibration is supported for this ADC channel.
    /// </summary>
    /// <returns>True if calibration is supported, false otherwise.</returns>
    bool IsCalibrationSupported() const;

private:
    // Store config values
    EspAdcWrapperConfig config;

    // Handles and state
    adc_oneshot_unit_handle_t unitHandle; // Shared handle (not owned)
    adc_cali_handle_t caliHandle;         // Calibration handle (owned)
    bool calibrationAttempted;
    bool calibrationSupported;
    bool initialized; // Track channel initialization success

    static const char *TAG;

    /// <summary>
    /// Initializes calibration for the ADC channel.
    /// </summary>
    /// <param name="atten">Attenuation setting.</param>
    /// <param name="bitwidth">Bit width setting.</param>
    /// <param name="chan">Channel number.</param>
    /// <returns>True if calibration is supported and initialized, false otherwise.</returns>
    bool InitializeCalibration(adc_atten_t atten, adc_bitwidth_t bitwidth, adc_channel_t chan);

    /// <summary>
    /// Cleans up the calibration handle if it exists.
    /// </summary>
    void CleanupCalibrationHandle();

    /// <summary>
    /// Maps ESP error codes to HAL ADC error codes.
    /// </summary>
    /// <param name="err">ESP error code.</param>
    /// <returns>Corresponding HAL ADC error code.</returns>
    static HalAdcError MapError(esp_err_t err);
};

#endif // ESP_ADC_WRAPPER_H