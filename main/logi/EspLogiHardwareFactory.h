#ifndef ESP_LOGI_HARDWARE_FACTORY_H
#define ESP_LOGI_HARDWARE_FACTORY_H

#include "logi/ILogiHardwareDriver.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_adc/adc_oneshot.h"

class EspAdcWrapper;
class EspI2cMasterWrapper;
class EspGpioWrapper;
class TemperatureSensorSht4x;
class RgbLedPartNumberIs31fl3193;
class AnalogLevelSensor;
class LogiHardwareDriver;
class ILogiHardwareDriver; 

class EspLogiHardwareFactory
{
public:
    // Returns a raw pointer to the statically managed driver
    static ILogiHardwareDriver *CreateLogiHardware();

    /// v1.2.1: deletes and re-creates the I2C master bus, then re-adds all
    /// device wrappers. Last-resort recovery for a wedged i2c.master driver
    /// (persistent ESP_ERR_INVALID_STATE). Returns true on full success.
    static bool RecoverI2cBus();

private:
    EspLogiHardwareFactory() = default; // Private constructor 

    static const adc_unit_t ADC_UNIT;
    static const adc_channel_t FUEL_ADC_CHANNEL;
    static const adc_channel_t SUPPLY_ADC_CHANNEL;
    static const adc_channel_t BATTERY_ADC_CHANNEL;
    static const adc_channel_t SOLAR_ADC_CHANNEL;
    static const adc_channel_t BATT_TEMP_ADC_CHANNEL;
    static const adc_atten_t DEFAULT_ADC_ATTEN;
    static const adc_bitwidth_t DEFAULT_ADC_BITWIDTH;
    static const i2c_port_t I2C_PORT;
    static const gpio_num_t I2C_SDA_PIN;
    static const gpio_num_t I2C_SCL_PIN;
    static const gpio_num_t UART_RX_PIN;
    static const gpio_num_t UART_TX_PIN;
    static const uint32_t I2C_FREQ_HZ;
    static const uint16_t SHT4X_I2C_ADDR;
    static const uint16_t RGBLED_I2C_ADDR;
    static const gpio_num_t POWER_ERROR_GPIO;
    static const gpio_num_t LED_STANDBY_GPIO;
    static const gpio_num_t SENSOR_POWER_GPIO;
    static const gpio_num_t CHARGING_ERROR_GPIO;
    static const gpio_num_t MEASURE_GPIO;
    static const gpio_num_t GNSS_RESET_GPIO;
    static const gpio_num_t GNSS_ENABLE_GPIO;
    static const int GPS_UART_BAUD_RATE;
    static const uint8_t BATTERY_ADC_SCALAR;
    static const uint8_t SOLAR_ADC_SCALAR;
};

#endif // ESP_LOGI_HARDWARE_FACTORY_H