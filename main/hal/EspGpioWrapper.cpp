#include "hal/EspGpioWrapper.h"

const char *EspGpioWrapper::TAG = "EspGpioWrapper";

/// <summary>
/// Constructs a GPIO wrapper with default input configuration
/// </summary>
/// <param name="gpioNum">The ESP32 GPIO pin number</param>
EspGpioWrapper::EspGpioWrapper(gpio_num_t gpioNum)
    : gpioNum(gpioNum),
      initialDirection(IGpioHal::Direction::Input),
      initialResistance(IGpioHal::Resistance::PullUp),
      currentDirection(IGpioHal::Direction::Disconnected),
      currentResistance(IGpioHal::Resistance::None),
      isValidPin(GPIO_IS_VALID_GPIO(gpioNum)),
      initialized(false)
{
    ESP_LOGD(TAG, "Constructor called for GPIO %d. Call Initialize() next.", (int)gpioNum);
    if (!isValidPin)
    {
        ESP_LOGE(TAG, "Invalid GPIO number provided: %d", (int)gpioNum);
    }
}

/// <summary>
/// Constructs a GPIO wrapper with specified initial configuration
/// </summary>
/// <param name="gpioNum">The ESP32 GPIO pin number</param>
/// <param name="initialDirection">The initial pin direction intent</param>
/// <param name="initialResistance">The initial pin pull resistance intent</param>
EspGpioWrapper::EspGpioWrapper(gpio_num_t gpioNum, IGpioHal::Direction initialDirection, IGpioHal::Resistance initialResistance)
    : gpioNum(gpioNum),
      initialDirection(initialDirection),
      initialResistance(initialResistance),
      currentDirection(IGpioHal::Direction::Disconnected),
      currentResistance(IGpioHal::Resistance::None),
      isValidPin(GPIO_IS_VALID_GPIO(gpioNum)),
      initialized(false)
{
    ESP_LOGD(TAG, "Constructor called for GPIO %d with initial config intent. Call Initialize() next.", (int)gpioNum);
    if (!isValidPin)
    {
        ESP_LOGE(TAG, "Invalid GPIO number provided: %d", (int)gpioNum);
    }
}

/// <summary>
/// Destructor
/// </summary>
EspGpioWrapper::~EspGpioWrapper()
{
    ESP_LOGD(TAG, "Destructor called for GPIO %d. Pin state unchanged.", (int)gpioNum);

    // Hardware state is left as-is. Resetting here might be unexpected.
    initialized = false;
}

/// <summary>
/// Initializes the GPIO pin with the configuration specified in constructor
/// </summary>
/// <returns>HAL_GPIO_OK on success, or an error code on failure</returns>
HalGpioError EspGpioWrapper::Initialize()
{
    if (initialized)
    {
        ESP_LOGW(TAG, "Already initialized (GPIO %d).", (int)gpioNum);
        return HAL_GPIO_OK;
    }

    if (!isValidPin)
    {
        ESP_LOGE(TAG, "Cannot initialize invalid GPIO %d", (int)gpioNum);
        return MapError(ESP_ERR_INVALID_ARG);
    }

    ESP_LOGI(TAG, "Initializing GPIO %d...", (int)gpioNum);
    // Reset pin first to ensure known state before applying config
    esp_err_t err = gpio_reset_pin(gpioNum);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to reset GPIO %d: %s", (int)gpioNum, esp_err_to_name(err));
        return MapError(err);
    }

    // Apply the stored initial configuration
    err = ApplyConfig(initialDirection, initialResistance);
    if (err == ESP_OK)
    {
        initialized = true;
        ESP_LOGI(TAG, "GPIO %d initialized successfully.", (int)gpioNum);
        return HAL_GPIO_OK;
    }
    else
    {
        ESP_LOGE(TAG, "Initial configuration failed for GPIO %d.", (int)gpioNum);
        initialized = false;
        return MapError(err);
    }
}

/// <summary>
/// Checks if the GPIO pin has been initialized
/// </summary>
/// <returns>True if the pin has been successfully initialized</returns>
bool EspGpioWrapper::IsInitialized() const
{
    return initialized;
}

/// <summary>
/// Helper method to apply GPIO configuration
/// </summary>
/// <param name="direction">The pin direction to apply</param>
/// <param name="resistance">The pin resistance to apply</param>
/// <returns>ESP_OK on success, or an ESP error code on failure</returns>
esp_err_t EspGpioWrapper::ApplyConfig(IGpioHal::Direction direction, IGpioHal::Resistance resistance)
{
    if (!isValidPin)
    {
        return ESP_ERR_INVALID_ARG;
    }

    // Handle Disconnected state by resetting the pin
    if (direction == IGpioHal::Direction::Disconnected)
    {
        ESP_LOGI(TAG, "Resetting GPIO %d due to Disconnected state.", (int)gpioNum);
        currentDirection = direction;
        currentResistance = IGpioHal::Resistance::None;
        return gpio_reset_pin(gpioNum);
    }

    gpio_config_t ioConf = {};
    ioConf.pin_bit_mask = (1ULL << gpioNum);
    ioConf.intr_type = GPIO_INTR_DISABLE;

    // Map Interface Enums to ESP-IDF Enums
    if (direction == IGpioHal::Direction::Input)
    {
        ioConf.mode = GPIO_MODE_INPUT;
    }
    else if (direction == IGpioHal::Direction::Output)
    {
        ioConf.mode = GPIO_MODE_OUTPUT;
    }
    else
    {
        // InputOutput
        ioConf.mode = GPIO_MODE_INPUT_OUTPUT;
    }

    if (resistance == IGpioHal::Resistance::PullUp)
    {
        ioConf.pull_up_en = GPIO_PULLUP_ENABLE;
        ioConf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    }
    else if (resistance == IGpioHal::Resistance::PullDown)
    {
        ioConf.pull_up_en = GPIO_PULLUP_DISABLE;
        ioConf.pull_down_en = GPIO_PULLDOWN_ENABLE;
    }
    else
    {
        // None
        ioConf.pull_up_en = GPIO_PULLUP_DISABLE;
        ioConf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    }

    esp_err_t err = gpio_config(&ioConf);
    if (err == ESP_OK)
    {
        // Update internal state tracking ONLY on success
        currentDirection = direction;
        currentResistance = resistance;
        ESP_LOGD(TAG, "Applied config GPIO %d: Mode=%d, PU=%d, PD=%d",
                 (int)gpioNum, ioConf.mode, ioConf.pull_up_en, ioConf.pull_down_en);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to apply config GPIO %d: %s", (int)gpioNum, esp_err_to_name(err));
    }
    return err;
}

/// <summary>
/// Configures both direction and resistance of the GPIO pin
/// </summary>
/// <param name="direction">The pin direction to set</param>
/// <param name="resistance">The pin resistance to set</param>
/// <returns>HAL_GPIO_OK on success, or an error code on failure</returns>
HalGpioError EspGpioWrapper::Configure(IGpioHal::Direction direction, IGpioHal::Resistance resistance)
{
    if (!IsInitialized())
    {
        return MapError(ESP_ERR_INVALID_STATE);
    }

    // Only apply if configuration actually changes
    if (currentDirection == direction && currentResistance == resistance && direction != Direction::Disconnected)
    {
        return HAL_GPIO_OK;
    }

    esp_err_t err = ApplyConfig(direction, resistance);
    return MapError(err);
}

/// <summary>
/// Sets the direction of the GPIO pin
/// </summary>
/// <param name="direction">The pin direction to set</param>
/// <returns>HAL_GPIO_OK on success, or an error code on failure</returns>
HalGpioError EspGpioWrapper::SetDirection(IGpioHal::Direction direction)
{
    if (!IsInitialized())
    {
        return MapError(ESP_ERR_INVALID_STATE);
    }

    // Only apply if direction changes
    if (currentDirection == direction && direction != Direction::Disconnected)
    {
        return HAL_GPIO_OK;
    }

    // Apply full config with new direction and current resistance
    esp_err_t err = ApplyConfig(direction, currentResistance);
    return MapError(err);
}

/// <summary>
/// Sets the pull resistance of the GPIO pin
/// </summary>
/// <param name="resistance">The pin resistance to set</param>
/// <returns>HAL_GPIO_OK on success, or an error code on failure</returns>
HalGpioError EspGpioWrapper::SetResistance(IGpioHal::Resistance resistance)
{
    if (!IsInitialized())
    {
        return MapError(ESP_ERR_INVALID_STATE);
    }

    // Only apply if resistance changes
    if (currentResistance == resistance)
    {
        return HAL_GPIO_OK;
    }

    // Apply full config with current direction and new resistance
    esp_err_t err = ApplyConfig(currentDirection, resistance);
    return MapError(err);
}

/// <summary>
/// Writes a digital value to the GPIO pin
/// </summary>
/// <param name="value">The value to write (true=high, false=low)</param>
/// <returns>HAL_GPIO_OK on success, or an error code on failure</returns>
HalGpioError EspGpioWrapper::Write(bool value)
{
    if (!IsInitialized())
    {
        return MapError(ESP_ERR_INVALID_STATE);
    }

    if (currentDirection != IGpioHal::Direction::Output && currentDirection != IGpioHal::Direction::InputOutput)
    {
        ESP_LOGW(TAG, "Write attempt on GPIO %d not configured as Output/InputOutput (Mode: %d)", (int)gpioNum, (int)currentDirection);
        return MapError(ESP_ERR_INVALID_STATE);
    }

    esp_err_t err = gpio_set_level(gpioNum, value ? 1 : 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set level for GPIO %d: %s", (int)gpioNum, esp_err_to_name(err));
    }
    return MapError(err);
}

/// <summary>
/// Reads the current digital value of the GPIO pin
/// </summary>
/// <returns>1=high, 0=low, or -1 on error</returns>
int EspGpioWrapper::Read()
{
    if (!IsInitialized())
    {
        ESP_LOGE(TAG, "Read attempt on uninitialized GPIO %d", (int)gpioNum);
        return -1; // Indicate error
    }

    if (currentDirection != IGpioHal::Direction::Input && currentDirection != IGpioHal::Direction::InputOutput)
    {
        ESP_LOGW(TAG, "Read attempt on GPIO %d not configured as Input/InputOutput (Mode: %d)", (int)gpioNum, (int)currentDirection);
        // Allow read attempt anyway, but log warning
    }

    // gpio_get_level returns 0 or 1, or potentially undefined if not input-capable
    return gpio_get_level(gpioNum);
}

/// <summary>
/// Maps ESP-IDF error codes to HAL error codes
/// </summary>
/// <param name="err">The ESP-IDF error code to map</param>
/// <returns>The corresponding HAL error code</returns>
HalGpioError EspGpioWrapper::MapError(esp_err_t err)
{
    switch (err)
    {
    case ESP_OK:
        return HAL_GPIO_OK;
    case ESP_ERR_INVALID_ARG:
        return HAL_GPIO_ERR_INVALID_ARG;
    case ESP_ERR_INVALID_STATE:
        return HAL_GPIO_ERR_INVALID_STATE;
    default:
        return HAL_GPIO_ERR_INIT_FAILED; // Map generic/other errors to init fail for now
    }
}

/// <summary>
/// Gets the GPIO pin number
/// </summary>
/// <returns>The ESP32 GPIO pin number</returns>
gpio_num_t EspGpioWrapper::GetGpioNum() const
{
    return gpioNum;
}

/// <summary>
/// Gets the current direction configuration of the GPIO pin
/// </summary>
/// <returns>The current direction setting</returns>
IGpioHal::Direction EspGpioWrapper::GetDirection() const
{
    return currentDirection;
}

/// <summary>
/// Gets the current resistance configuration of the GPIO pin
/// </summary>
/// <returns>The current pull resistance setting</returns>
IGpioHal::Resistance EspGpioWrapper::GetResistance() const
{
    return currentResistance;
}

/// <summary>
/// Checks if the pin is valid
/// </summary>
/// <returns>True if the pin number is valid for this hardware</returns>
bool EspGpioWrapper::IsValidPin() const
{
    return isValidPin;
}