#ifndef I_ADC_HAL_H
#define I_ADC_HAL_H

#include <stdint.h> // For standard types

// Define a common error type. Using int for simplicity, mapping to ESP_OK (0) for success.
typedef int HalAdcError;
#define HAL_ADC_OK 0
#define HAL_ADC_ERR_NOT_SUPPORTED -1 // Example: For unsupported features like calibration
#define HAL_ADC_ERR_INVALID_STATE -2 // Example: Not initialized
#define HAL_ADC_ERR_READ_FAILED -3   // Example: Hardware read error
#define HAL_ADC_ERR_INVALID_ARG -4   // Example: Bad input pointer/value
#define HAL_ADC_ERR_INIT_FAILED -5   // Example: Initialization failed

/// <summary>
/// Interface for Hardware Abstraction Layer for an Analog-to-Digital Converter channel.
/// Defines a platform-independent way to read ADC values.
/// </summary>
class IAdcHal
{
public:
    /// <summary>
    /// Virtual destructor.
    /// </summary>
    virtual ~IAdcHal() = default;

    /// <summary>
    /// Initializes the underlying ADC hardware/channel according to configuration
    /// provided during construction. Must be called before other operations.
    /// </summary>
    /// <returns>HAL_ADC_OK on success, or a HalAdcError code on failure.</returns>
    virtual HalAdcError Initialize() = 0;

    /// <summary>
    /// Checks if the ADC channel wrapper has been successfully initialized.
    /// </summary>
    /// <returns>true if Initialize() succeeded, false otherwise.</returns>
    virtual bool IsInitialized() const = 0;

    /// <summary>
    /// Reads the raw ADC value (counts) from the configured channel.
    /// Requires Initialize() to have succeeded.
    /// </summary>
    /// <param name="out_raw">Reference to store the raw ADC reading.</param>
    /// <returns>HAL_ADC_OK on success, or a HalAdcError code on failure.</returns>
    virtual HalAdcError GetCounts(int *outRaw) = 0;

    /// <summary>
    /// Reads the ADC value and converts it to calibrated millivolts, if calibration is supported.
    /// Requires Initialize() to have succeeded.
    /// If calibration is not supported or fails, behavior depends on implementation.
    /// </summary>
    /// <param name="out_voltage">Reference to store the calculated voltage in millivolts.</param>
    /// <returns>HAL_ADC_OK on success, HAL_ADC_ERR_NOT_SUPPORTED if calibration unavailable,
    /// or another HalAdcError code on read/conversion failure.</returns>
    virtual HalAdcError GetMillivolts(int *outVoltage) = 0;
};

#endif // I_ADC_HAL_H