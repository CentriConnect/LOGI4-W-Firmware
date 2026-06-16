#ifndef ESP_GPIO_WRAPPER_H
#define ESP_GPIO_WRAPPER_H

#include "driver/gpio.h"
#include "esp_log.h"
#include "interfaces/IGpioHal.h" // Include the generic interface

/// <summary>
/// Implementation of IGpioHal for ESP32 GPIO control
/// </summary>
class EspGpioWrapper : public IGpioHal
{
public:
    /// <summary>
    /// Constructs a GPIO wrapper with default input configuration
    /// </summary>
    /// <param name="gpioNum">The ESP32 GPIO pin number</param>
    EspGpioWrapper(gpio_num_t gpioNum);

    /// <summary>
    /// Constructs a GPIO wrapper with specified initial configuration
    /// </summary>
    /// <param name="gpioNum">The ESP32 GPIO pin number</param>
    /// <param name="initialDirection">The initial pin direction intent</param>
    /// <param name="initialResistance">The initial pin pull resistance intent</param>
    EspGpioWrapper(gpio_num_t gpioNum, IGpioHal::Direction initialDirection, IGpioHal::Resistance initialResistance = IGpioHal::Resistance::None);

    /// <summary>
    /// Destructor
    /// </summary>
    virtual ~EspGpioWrapper();

    // --- IGpioHal Interface Implementation ---

    /// <summary>
    /// Initializes the GPIO pin with the configuration specified in constructor
    /// </summary>
    /// <returns>HAL_GPIO_OK on success, or an error code on failure</returns>
    HalGpioError Initialize() override;

    /// <summary>
    /// Checks if the GPIO pin has been initialized
    /// </summary>
    /// <returns>True if the pin has been successfully initialized</returns>
    bool IsInitialized() const override;

    /// <summary>
    /// Configures both direction and resistance of the GPIO pin
    /// </summary>
    /// <param name="direction">The pin direction to set</param>
    /// <param name="resistance">The pin resistance to set</param>
    /// <returns>HAL_GPIO_OK on success, or an error code on failure</returns>
    HalGpioError Configure(Direction direction, Resistance resistance) override;

    /// <summary>
    /// Sets the direction of the GPIO pin
    /// </summary>
    /// <param name="direction">The pin direction to set</param>
    /// <returns>HAL_GPIO_OK on success, or an error code on failure</returns>
    HalGpioError SetDirection(Direction direction) override;

    /// <summary>
    /// Sets the pull resistance of the GPIO pin
    /// </summary>
    /// <param name="resistance">The pin resistance to set</param>
    /// <returns>HAL_GPIO_OK on success, or an error code on failure</returns>
    HalGpioError SetResistance(Resistance resistance) override;

    /// <summary>
    /// Writes a digital value to the GPIO pin
    /// </summary>
    /// <param name="value">The value to write (true=high, false=low)</param>
    /// <returns>HAL_GPIO_OK on success, or an error code on failure</returns>
    HalGpioError Write(bool value) override;

    /// <summary>
    /// Reads the current digital value of the GPIO pin
    /// </summary>
    /// <returns>1=high, 0=low, or -1 on error</returns>
    int Read() override;

    /// <summary>
    /// Gets the GPIO pin number
    /// </summary>
    /// <returns>The ESP32 GPIO pin number</returns>
    gpio_num_t GetGpioNum() const;

    /// <summary>
    /// Gets the current direction configuration of the GPIO pin
    /// </summary>
    /// <returns>The current direction setting</returns>
    Direction GetDirection() const;

    /// <summary>
    /// Gets the current resistance configuration of the GPIO pin
    /// </summary>
    /// <returns>The current pull resistance setting</returns>
    Resistance GetResistance() const;

    /// <summary>
    /// Checks if the pin is valid
    /// </summary>
    /// <returns>True if the pin number is valid for this hardware</returns>
    bool IsValidPin() const;

private:
    gpio_num_t gpioNum;
    IGpioHal::Direction initialDirection;   
    IGpioHal::Resistance initialResistance; 
    IGpioHal::Direction currentDirection;   
    IGpioHal::Resistance currentResistance; 
    bool isValidPin;
    bool initialized; 

    static const char *TAG;

    /// <summary>
    /// Helper method to apply GPIO configuration
    /// </summary>
    /// <param name="direction">The pin direction to apply</param>
    /// <param name="resistance">The pin resistance to apply</param>
    /// <returns>ESP_OK on success, or an ESP error code on failure</returns>
    esp_err_t ApplyConfig(IGpioHal::Direction direction, IGpioHal::Resistance resistance);

    /// <summary>
    /// Maps ESP-IDF error codes to HAL error codes
    /// </summary>
    /// <param name="err">The ESP-IDF error code to map</param>
    /// <returns>The corresponding HAL error code</returns>
    static HalGpioError MapError(esp_err_t err);
};

#endif // ESP_GPIO_WRAPPER_H