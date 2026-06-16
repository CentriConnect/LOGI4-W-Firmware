#ifndef I_GPIO_HAL_H
#define I_GPIO_HAL_H

#include <stdint.h> // For standard types

// Define a common error type. Using int for simplicity.
typedef int HalGpioError;
#define HAL_GPIO_OK 0
#define HAL_GPIO_ERR_INVALID_ARG -1
#define HAL_GPIO_ERR_INVALID_STATE -2
#define HAL_GPIO_ERR_INIT_FAILED -3

/// <summary>
/// Interface for Hardware Abstraction Layer for a GPIO.
/// Defines a platform-independent way to read and write GPIO values.
/// </summary>
class IGpioHal
{
public:

    /// <summary>
    /// Enumeration for GPIO pin direction.
    /// </summary>
    enum class Direction
    {
        Input,
        Output,
        InputOutput,
        Disconnected
    };

    /// <summary>
    /// Enumeration for GPIO internal pull-up/pull-down resistance.
    /// </summary>
    enum class Resistance
    {
        None,
        PullUp,
        PullDown
    };

    virtual ~IGpioHal() = default;

    /// <summary>
    /// Initializes the underlying GPIO pin according to configuration
    /// provided during construction. Must be called before other operations.
    /// </summary>
    /// <returns>HAL_GPIO_OK on success, or a HalGpioError code on failure.</returns>
    virtual HalGpioError Initialize() = 0;

    /// <summary>
    /// Checks if the GPIO pin has been successfully initialized.
    /// </summary>
    /// <returns>true if Initialize() succeeded, false otherwise.</returns>
    virtual bool IsInitialized() const = 0;

    /// <summary>
    /// Configures both the direction and internal resistance of the GPIO pin.
    /// Requires Initialize() to have succeeded.
    /// </summary>
    virtual HalGpioError Configure(Direction direction, Resistance resistance) = 0;

    /// <summary>
    /// Sets the direction (Input/Output) of the GPIO pin.
    /// Requires Initialize() to have succeeded.
    /// </summary>
    virtual HalGpioError SetDirection(Direction direction) = 0;

    /// <summary>
    /// Sets the internal pull resistor configuration.
    /// Requires Initialize() to have succeeded.
    /// </summary>
    virtual HalGpioError SetResistance(Resistance resistance) = 0;

    /// <summary>
    /// Sets the output level of the GPIO pin.
    /// Requires Initialize() to have succeeded and pin to be output capable.
    /// </summary>
    virtual HalGpioError Write(bool value) = 0;

    /// <summary>
    /// Reads the current input level of the GPIO pin.
    /// Requires Initialize() to have succeeded and pin to be input capable.
    /// </summary>
    /// <returns>1 if high, 0 if low. May return a negative value on error.</returns>
    virtual int Read() = 0;
};

#endif // I_GPIO_HAL_H