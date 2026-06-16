#ifndef ESP_BOARD_UTILS_H
#define ESP_BOARD_UTILS_H

#include "interfaces/IBoardUtils.h"
#include "driver/gpio.h" // For gpio_num_t

/// <summary>
/// Concrete implementation of IBoardUtils for ESP32-C6 using ESP-IDF GPIO driver.
/// </summary>
class EspBoardUtils : public IBoardUtils {
    gpio_num_t _led_pin; // GPIO pin number for the status LED
    bool _initialized;   // Flag indicating successful initialization

    // Private tag for logging
    static const char* TAG;

public:
    /// <summary>
    /// Constructor for EspBoardUtils. Configures the specified GPIO pin as an output for the LED.
    /// </summary>
    /// <param name="led_gpio_pin">The GPIO pin number connected to the status LED.</param>
    EspBoardUtils(gpio_num_t led_gpio_pin);

    /// <summary>
    /// Destructor. Can optionally reset the GPIO pin configuration.
    /// </summary>
    virtual ~EspBoardUtils();

    // --- Delete copy constructor and assignment operator ---
    EspBoardUtils(const EspBoardUtils&) = delete;
    EspBoardUtils& operator=(const EspBoardUtils&) = delete;

    /// <summary>
    /// Sets the state of the configured status LED.
    /// </summary>
    /// <param name="on">true to turn the LED on, false to turn it off.</param>
    void SetDebugLed(bool on) override;

    /// <summary>
    /// Checks if the board utils were initialized correctly.
    /// </summary>
    /// <returns>true if GPIO was configured, false otherwise.</returns>
    bool IsInitialized() const { return _initialized; }
};

#endif // ESP_BOARD_UTILS_H