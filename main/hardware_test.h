//==============================================================================
// LOGI4W PCBA Hardware Checkout Test - Header
//
// Purpose: Test firmware for validating each hardware function on the LOGI4W
// PCBA. Outputs results via serial console (115200 baud).
//==============================================================================

#ifndef HARDWARE_TEST_H
#define HARDWARE_TEST_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

//==============================================================================
// GPIO Pin Definitions (from working factory code EspLogiHardwareFactory.cpp)
//==============================================================================

// ADC Inputs (ADC1)
#define PIN_FUEL_VOLT       GPIO_NUM_0   // ADC1_CH0 - Fuel sensor signal
#define PIN_SPS_VOLT        GPIO_NUM_1   // ADC1_CH1 - Sensor power supply voltage
#define PIN_BATT_VOLT       GPIO_NUM_2   // ADC1_CH2 - Battery voltage
#define PIN_SOLAR_VOLT      GPIO_NUM_3   // ADC1_CH3 - Solar panel voltage

// GNSS UART
#define PIN_GNSS_RXD        GPIO_NUM_4   // ESP receives from GPS
#define PIN_GNSS_TXD        GPIO_NUM_5   // ESP sends to GPS

// I2C Bus
#define PIN_I2C_SDA         GPIO_NUM_6
#define PIN_I2C_SCL         GPIO_NUM_7

// GNSS Control
#define PIN_GNSS_RESET      GPIO_NUM_10  // Active LOW reset
#define PIN_GNSS_ENABLE     GPIO_NUM_11  // HIGH = LDO enabled, powers +3.3G rail

// Power/Sensor Control (from factory code - these are the WORKING assignments!)
#define PIN_SPS_ERROR       GPIO_NUM_18  // Power error input (POWER_ERROR_GPIO)
#define PIN_SPS_ENABLE      GPIO_NUM_19  // Sensor power enable (SENSOR_POWER_GPIO)
#define PIN_LED_STANDBY     GPIO_NUM_21  // RGB LED driver SDB (LED_STANDBY_GPIO)
#define PIN_MON_MEAS        GPIO_NUM_22  // Measurement divider enable (MEASURE_GPIO)
#define PIN_CHG_STATUS      GPIO_NUM_23  // Charging error/status (CHARGING_ERROR_GPIO)

// Note: There may not be a separate debug LED - the LED_STANDBY pin (GPIO21)
// controls the RGB LED driver, so we'll test that instead.
// If a debug LED exists on a different GPIO, update this.
#define PIN_DEBUG_LED       GPIO_NUM_21  // Using same as LED_STANDBY for now

//==============================================================================
// I2C Device Addresses
//==============================================================================
#define SHT4X_I2C_ADDR      0x44         // Temperature/Humidity sensor
#define IS31FL3193_I2C_ADDR 0x68         // RGB LED driver
#define ROCHESTER_9705_I2C_ADDR 0x0C     // Rochester 9705 I2C fuel level sensor

//==============================================================================
// Test Configuration
//==============================================================================
#define FLASH_WINDOW_SECONDS    20       // Time to allow reflashing at startup
#define GNSS_FIX_TIMEOUT_SEC    180      // Time to wait for GPS fix (3 minutes for cold start)
#define GNSS_BAUD_RATE          115200   // YIC51009EBGGB baud rate
#define DEBUG_LED_BLINK_COUNT   5        // Number of LED blinks in test
#define ADC_SAMPLE_COUNT        10       // Number of ADC samples to average

//==============================================================================
// Test Result Structure
//==============================================================================
typedef struct {
    bool debug_led_pass;
    bool led_standby_pass;
    bool i2c_scan_sht4x;
    bool i2c_scan_rgb;
    bool sht4x_temp_pass;
    bool sht4x_humidity_pass;
    float sht4x_temp_c;
    float sht4x_humidity_pct;
    bool adc_batt_pass;
    bool adc_solar_pass;
    bool adc_sps_pass;
    bool adc_fuel_pass;
    int adc_batt_mv;
    int adc_solar_mv;
    int adc_sps_mv;
    int adc_fuel_mv;
    bool gnss_power_pass;
    bool gnss_nmea_pass;
    bool gnss_fix_pass;
    double gnss_latitude;
    double gnss_longitude;
    float gnss_altitude;
    uint8_t gnss_satellites_used;
    uint8_t gnss_satellites_in_view;
    uint8_t gnss_fix_type;
    float gnss_hdop;
    float gnss_vdop;
    float gnss_pdop;
    uint32_t gnss_ttff_ms;
    int gnss_max_snr;
    bool chg_status_pass;
    bool chg_status_charging;
    bool power_error_pass;
    bool power_error_fault;
    bool i2c_scan_9705;
    bool fuel_i2c_init_pass;
    bool fuel_i2c_read_pass;
    float fuel_i2c_temp_c;
    uint16_t fuel_i2c_level_pct;
} HardwareTestResults_t;

//==============================================================================
// Test Function Prototypes
//==============================================================================

/**
 * @brief Run the complete hardware test sequence
 * @param results Pointer to structure that will be filled with test results
 */
void RunHardwareTests(HardwareTestResults_t* results);

/**
 * @brief Print a summary of all test results
 * @param results Pointer to structure containing test results
 */
void PrintTestSummary(const HardwareTestResults_t* results);

/**
 * @brief Test the debug LED (D3) on IO21
 * @return true if LED toggled successfully
 */
bool TestDebugLed(void);

/**
 * @brief Test the LED standby control (IO19) for RGB LED driver
 * @return true if standby control works correctly
 */
bool TestLedStandbyControl(void);

/**
 * @brief Scan I2C bus for expected devices
 * @param found_sht4x Set to true if SHT4x found at 0x44
 * @param found_rgb Set to true if IS31FL3193 found at 0x68
 * @param found_9705 Set to true if Rochester 9705 found at 0x0C
 */
void ScanI2cDevices(bool* found_sht4x, bool* found_rgb, bool* found_9705);

/**
 * @brief Test the SHT4x temperature/humidity sensor
 * @param temp_c Output: temperature in Celsius
 * @param humidity_pct Output: relative humidity percentage
 * @return true if valid readings obtained
 */
bool TestSht4xSensor(float* temp_c, float* humidity_pct);

/**
 * @brief Test I2C fuel level sensor (Rochester 9705)
 * @param results Pointer to structure that will be filled with test results
 * @return true if sensor initialized and read successfully
 */
bool TestI2cFuelSensor(HardwareTestResults_t* results);

/**
 * @brief Test all ADC channels
 * @param batt_mv Output: battery voltage in millivolts
 * @param solar_mv Output: solar voltage in millivolts
 * @param sps_mv Output: sensor power supply voltage in millivolts
 * @param fuel_mv Output: fuel sensor voltage in millivolts
 * @return true if all ADC readings successful
 */
bool TestAdcChannels(int* batt_mv, int* solar_mv, int* sps_mv, int* fuel_mv);

/**
 * @brief Test GNSS module power and communication
 * @param results Pointer to HardwareTestResults_t to store all GNSS results
 */
void TestGnssModule(HardwareTestResults_t* results);

/**
 * @brief Test charging status input (IO22)
 * @param is_charging Output: true if currently charging
 * @return true if status read successfully
 */
bool TestChargingStatus(bool* is_charging);

/**
 * @brief Test power error detection (IO18)
 * @param has_fault Output: true if fault condition detected
 * @return true if status read successfully
 */
bool TestPowerError(bool* has_fault);

/**
 * @brief Test RGB LED with various colors
 * @return true if RGB LED responds to I2C commands
 */
bool TestRgbLed(void);

#ifdef __cplusplus
}
#endif

#endif // HARDWARE_TEST_H
