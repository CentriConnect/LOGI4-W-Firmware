#ifndef I_I2C_HAL_H
#define I_I2C_HAL_H

#include <stddef.h> // For size_t
#include <stdint.h> // For uint8_t

// Define a common error type or use platform-specific ones conditionally
typedef int HalI2cError;
#define HAL_I2C_OK 0
#define HAL_I2C_ERR_INVALID_ARG -1
#define HAL_I2C_ERR_INVALID_STATE -2
#define HAL_I2C_ERR_TIMEOUT -3
#define HAL_I2C_ERR_BUSY -4
#define HAL_I2C_ERR_NACK -5
#define HAL_I2C_ERR_FAIL -6        // Generic failure
#define HAL_I2C_ERR_INIT_FAILED -7 // Initialization failure

/// <summary> 
/// Interface for Hardware Abstraction Layer for I2C communication.
/// Defines a platform-independent way to read and write I2C values.
/// </summary>
class II2cHal
{
public:
    virtual ~II2cHal() = default; // Virtual destructor

    /// <summary>
    /// Initializes the I2C device on the bus according to configuration
    /// provided during construction. Must be called before other operations.
    /// Assumes the underlying bus is already initialized.
    /// </summary>
    /// <returns>HAL_I2C_OK on success, or a HalI2cError code on failure.</returns>
    virtual HalI2cError Initialize() = 0;

    /// <summary>
    /// Checks if the I2C device communication wrapper has been successfully initialized.
    /// </summary>
    /// <returns>true if Initialize() succeeded, false otherwise.</returns>
    virtual bool IsInitialized() const = 0;

    /// <summary>
    /// Reads bytes from the associated I2C device.
    /// Requires Initialize() to have succeeded.
    /// </summary>
    virtual HalI2cError Read(uint8_t *readBuffer, size_t readSize, int timeoutMs = 100) = 0;

    /// <summary>
    /// Writes bytes to the associated I2C device.
    /// Requires Initialize() to have succeeded.
    /// </summary>
    virtual HalI2cError Write(const uint8_t *writeBuffer, size_t writeSize, int timeoutMs = 100) = 0;

    /// <summary>
    /// Performs an I2C write operation followed by a read operation.
    /// Requires Initialize() to have succeeded.
    /// </summary>
    virtual HalI2cError WriteRead(const uint8_t *writeBuffer, size_t writeSize, uint8_t *readBuffer, size_t readSize, int timeoutMs = 100) = 0;
};

#endif // I_I2C_HAL_H