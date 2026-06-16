#include "EspBoardUtils.h"
#include "esp_log.h"

// Define the static const char* TAG member
const char* EspBoardUtils::TAG = "EspBoardUtils";

EspBoardUtils::EspBoardUtils(gpio_num_t led_gpio_pin) :
    _led_pin(led_gpio_pin),
    _initialized(false)
{
    ESP_LOGI(TAG, "Initializing board utils, configuring LED on GPIO %d", (int)_led_pin);

    // Simple configuration for a standard output LED
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;      
    io_conf.pin_bit_mask = (1ULL << _led_pin);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;

    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
         ESP_LOGE(TAG, "Failed to configure LED GPIO %d (0x%X)", (int)_led_pin, err);
         _initialized = false;
    } else {
         ESP_LOGI(TAG, "LED GPIO %d configured successfully.", (int)_led_pin);
         // Set initial state (e.g., off)
         gpio_set_level(_led_pin, 0);
         _initialized = true;
    }
}

EspBoardUtils::~EspBoardUtils() {
    if (_initialized) {
         ESP_LOGI(TAG, "Resetting LED GPIO %d", (int)_led_pin);
         
         // Optional: Reset the GPIO pin to a default state if desired
         gpio_reset_pin(_led_pin);
         _initialized = false;
    }
}

void EspBoardUtils::SetDebugLed(bool on) {
     if (!_initialized) {
         ESP_LOGE(TAG, "Cannot set LED state: Board utils not initialized.");
         return;
     }
    esp_err_t err = gpio_set_level(_led_pin, on ? 1 : 0);
    if (err != ESP_OK) {
         ESP_LOGE(TAG, "Failed to set LED level on GPIO %d (0x%X)", (int)_led_pin, err);
    }
}