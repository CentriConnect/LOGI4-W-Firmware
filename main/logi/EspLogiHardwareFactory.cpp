// ----- File: main/hal/factories/EspLogiHardwareFactory.cpp ----- (Static Allocation - Corrected Init Order) -----
#include "logi/EspLogiHardwareFactory.h"
#include "hal/EspAdcWrapper.h"
#include "hal/EspI2cMasterWrapper.h"
#include "hal/EspGpioWrapper.h"
#include "hal/EspAdcWrapperConfig.h"
#include "hal/EspUartWrapper.h"
#include "drivers/TemperatureSensorSht4x.h"
#include "drivers/RgbLedPartNumberIs31fl3193.h"
#include "drivers/AnalogLevelSensor.h"
#include "drivers/AdcAds1015.h"
#include "drivers/GPSModuleYIC51009EBGGB.h"
#include "logi/LogiHardwareDriver.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

static const char *TAG_FACTORY = "LogiHwFactory";

// --- Definitions for Static Const Members ---
const adc_unit_t EspLogiHardwareFactory::ADC_UNIT = ADC_UNIT_1;
const adc_channel_t EspLogiHardwareFactory::FUEL_ADC_CHANNEL = ADC_CHANNEL_0;
const adc_channel_t EspLogiHardwareFactory::SUPPLY_ADC_CHANNEL = ADC_CHANNEL_1;
const adc_channel_t EspLogiHardwareFactory::BATTERY_ADC_CHANNEL = ADC_CHANNEL_2;
const adc_channel_t EspLogiHardwareFactory::SOLAR_ADC_CHANNEL = ADC_CHANNEL_3;
const adc_channel_t EspLogiHardwareFactory::BATT_TEMP_ADC_CHANNEL = ADC_CHANNEL_4;
const adc_atten_t EspLogiHardwareFactory::DEFAULT_ADC_ATTEN = ADC_ATTEN_DB_11;
const adc_bitwidth_t EspLogiHardwareFactory::DEFAULT_ADC_BITWIDTH = ADC_BITWIDTH_DEFAULT;
const i2c_port_t EspLogiHardwareFactory::I2C_PORT = I2C_NUM_0;
const gpio_num_t EspLogiHardwareFactory::I2C_SDA_PIN = GPIO_NUM_6;
const gpio_num_t EspLogiHardwareFactory::I2C_SCL_PIN = GPIO_NUM_7;
const gpio_num_t EspLogiHardwareFactory::UART_RX_PIN = GPIO_NUM_4;  // Per schematic: IO4 = RXD
const gpio_num_t EspLogiHardwareFactory::UART_TX_PIN = GPIO_NUM_5;  // Per schematic: IO5 = TXD
// REQ-I2C-01: 100 kHz, matching the hardware-checkout firmware which proved
// reliable on this PCB (hardware_test.cpp:331 "Use 100kHz for reliability").
// Nick's 2026-06-01 #3 confirmed his failing bench unit was on battery — the
// case where 400 kHz on internal pull-ups was marginal. Drop to 100 kHz removes
// the marginal-edge-timing failure mode. (PDEC-001 still open re: external
// pull-ups; if present, we could revisit 400 kHz later.)
const uint32_t EspLogiHardwareFactory::I2C_FREQ_HZ = 100000;
const uint16_t EspLogiHardwareFactory::SHT4X_I2C_ADDR = 0x44;
const uint16_t EspLogiHardwareFactory::RGBLED_I2C_ADDR = 0x68;
const gpio_num_t EspLogiHardwareFactory::POWER_ERROR_GPIO = GPIO_NUM_18;
const gpio_num_t EspLogiHardwareFactory::LED_STANDBY_GPIO = GPIO_NUM_21;
const gpio_num_t EspLogiHardwareFactory::SENSOR_POWER_GPIO = GPIO_NUM_19;
const gpio_num_t EspLogiHardwareFactory::CHARGING_ERROR_GPIO = GPIO_NUM_23;
// v1.3.0 (ISS-FW-015/ISS-002): MON_MEAS_DO is IO20 on schematic rev 4.0 AND
// 4.1.1 - the old GPIO_NUM_22 actually drove DEBUG_LED, so the measurement
// dividers were never enabled (root cause of un-scaled supply/level readings).
const gpio_num_t EspLogiHardwareFactory::MEASURE_GPIO = GPIO_NUM_20;
const gpio_num_t EspLogiHardwareFactory::GNSS_RESET_GPIO = GPIO_NUM_10;  // Active LOW - drive HIGH for normal operation
const gpio_num_t EspLogiHardwareFactory::GNSS_ENABLE_GPIO = GPIO_NUM_11;
const int EspLogiHardwareFactory::GPS_UART_BAUD_RATE = 115200;  // YIC51009EBGGB datasheet baud rate
const uint8_t EspLogiHardwareFactory::BATTERY_ADC_SCALAR = 2;
const uint8_t EspLogiHardwareFactory::SOLAR_ADC_SCALAR = 2;
// --- End Static Member Definitions ---

// --- Static Pointers for Persistent Objects ---
// We need pointers to hold the static instances created inside the function
static EspI2cMasterWrapper *p_sht4x_i2c_wrapper = nullptr;
static EspI2cMasterWrapper *p_rgbled_i2c_wrapper = nullptr;
static EspGpioWrapper *p_power_error_gpio = nullptr;
static EspGpioWrapper *p_led_standby_gpio = nullptr;
static EspGpioWrapper *p_sensor_power_gpio = nullptr;
static EspGpioWrapper *p_charging_error_gpio = nullptr;
static EspGpioWrapper *p_measure_gpio = nullptr;
static EspGpioWrapper *p_gnss_reset_gpio = nullptr;
static EspGpioWrapper *p_gnss_enable_gpio = nullptr;
// v1.3.0: IAdcHal so each input can be an internal ESP32 channel (board rev
// <=4.0) or an ADS1015 channel (rev 4.1+) without touching the consumers.
static IAdcHal *p_fuel_adc = nullptr;
static IAdcHal *p_supply_adc = nullptr;
static IAdcHal *p_battery_adc = nullptr;
static IAdcHal *p_solar_adc = nullptr;
static IAdcHal *p_batt_temp_adc = nullptr;
// v1.3.0: rev-4.1+ analog parts (constructed always, initialized only when
// the board-rev probe finds the ADS1015).
static EspI2cMasterWrapper *p_ads1015_i2c_wrapper = nullptr;
static bool s_board_rev41 = false;
static EspUartWrapper *p_gps_uart_obj = nullptr;
static TemperatureSensorSht4x *p_temp_sensor_driver = nullptr;
static RgbLedPartNumberIs31fl3193 *p_rgb_led_driver = nullptr;
static AnalogLevelSensor *p_analog_level_driver = nullptr;
static LogiHardwareDriver *p_logi_hardware_driver = nullptr;
static GPSModuleYIC51009EBGGB *p_gps_sensor_driver_obj = nullptr;

// --- v1.2.1: I2C bus handle at file scope so RecoverI2cBus() can rebuild it ---
static i2c_master_bus_handle_t s_i2c_bus_handle = NULL;

/// Creates the single I2C master bus with the project's standard config.
static esp_err_t CreateI2cMasterBus(i2c_port_t port, gpio_num_t sda, gpio_num_t scl)
{
    i2c_master_bus_config_t i2c_bus_conf = {};
    i2c_bus_conf.i2c_port = port;
    i2c_bus_conf.sda_io_num = sda;
    i2c_bus_conf.scl_io_num = scl;
    i2c_bus_conf.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_bus_conf.flags.enable_internal_pullup = true;
    i2c_bus_conf.glitch_ignore_cnt = 7;
    return i2c_new_master_bus(&i2c_bus_conf, &s_i2c_bus_handle);
}

// --- Static Factory Method ---
ILogiHardwareDriver *EspLogiHardwareFactory::CreateLogiHardware()
{
    ESP_LOGI(TAG_FACTORY, "Creating Logi Hardware components (Static Allocation)...");

    // --- Use static handles, initialize only once ---
    static adc_oneshot_unit_handle_t adc_unit_handle = NULL;
    static bool factory_initialized = false; // Tracks overall factory first run

    if (!factory_initialized)
    {
        ESP_LOGI(TAG_FACTORY, "Performing one-time initialization of shared handles and static objects...");

        // --- SENSOR_POWER (SPS) LOW before the I2C bus is created ---
        // Bench evidence 2026-06-12 (two boots, same board, same day):
        //   * +3.3S up BEFORE i2c_new_master_bus  -> bus clamped from the first
        //     transaction ("clear bus failed" / ESP_ERR_INVALID_STATE forever).
        //   * Bus created with SPS LOW (pull-ups idle-high), SPS raised later
        //     -> completely clean boot.
        // Nick's boards (2026-06-11) show the OPPOSITE tolerance: they clamp at
        // the mid-session raise. PCA9512AD/U2 (DNP in schematic rev 4.0, but
        // population varies by build) latches the bus depending on the state of
        // its A/B sides when +3.3S ramps - so NO ordering is safe on every
        // board. Policy: create the bus in the electrically-quiet state (SPS
        // LOW), raise SPS once at the end of driver init (v1.2.0-proven), and
        // treat any LED/I2C casualty as NON-FATAL (latch + recovery, never
        // halt). See ISS-FW-012.
        static EspGpioWrapper sensor_power_gpio_obj(SENSOR_POWER_GPIO, IGpioHal::Direction::Output, IGpioHal::Resistance::None);
        p_sensor_power_gpio = &sensor_power_gpio_obj;
        if (p_sensor_power_gpio->Initialize() != HAL_GPIO_OK)
        {
            ESP_LOGE(TAG_FACTORY, "Failed to initialize SENSOR_POWER GPIO %d!", (int)SENSOR_POWER_GPIO);
            return nullptr;
        }
        p_sensor_power_gpio->Write(false);
        ESP_LOGI(TAG_FACTORY, "Sensor power (SPS, GPIO %d) held LOW for I2C bus init.", (int)SENSOR_POWER_GPIO);

        // --- Initialize SINGLE I2C Bus ---
        ESP_LOGI(TAG_FACTORY, "Initializing I2C Bus Port %d (SDA:%d, SCL:%d)", (int)I2C_PORT, (int)I2C_SDA_PIN, (int)I2C_SCL_PIN);
        esp_err_t bus_err = CreateI2cMasterBus(I2C_PORT, I2C_SDA_PIN, I2C_SCL_PIN);
        if (bus_err != ESP_OK || !s_i2c_bus_handle)
        {
            ESP_LOGE(TAG_FACTORY, "Failed to create I2C master bus: %s", esp_err_to_name(bus_err));
            return nullptr;
        }
        ESP_LOGI(TAG_FACTORY, "I2C Bus Port %d initialized.", (int)I2C_PORT);

        // --- Board revision detection (v1.3.0) ---
        // Rev 4.1+ boards carry an ADS1015 at 0x48 (solar/SPS/batt-temp/battery
        // moved off the internal ADC so IO0/IO1 could host the 32.768 kHz
        // crystal). Probe it: ACK = rev-4.1 analog map, no ACK = legacy map.
        s_board_rev41 = (i2c_master_probe(s_i2c_bus_handle, 0x48, 100) == ESP_OK);
        ESP_LOGI(TAG_FACTORY, "Board rev detect: ADS1015 @0x48 %s -> using %s analog map.",
                 s_board_rev41 ? "FOUND" : "not found",
                 s_board_rev41 ? "rev-4.1 (external ADC)" : "legacy rev-4.0 (internal ADC)");

        // --- Initialize SINGLE ADC Unit ---
        ESP_LOGI(TAG_FACTORY, "Initializing ADC Unit %d...", (int)ADC_UNIT);
        adc_oneshot_unit_init_cfg_t adc_unit_conf = {};
        adc_unit_conf.unit_id = ADC_UNIT;
        adc_unit_conf.ulp_mode = ADC_ULP_MODE_DISABLE;
        esp_err_t adc_unit_err = adc_oneshot_new_unit(&adc_unit_conf, &adc_unit_handle);
        if (adc_unit_err != ESP_OK || !adc_unit_handle)
        {
            ESP_LOGE(TAG_FACTORY, "Failed to create ADC unit %d: %s", (int)ADC_UNIT, esp_err_to_name(adc_unit_err));
            // Consider cleaning up I2C bus handle here if ADC fails
            return nullptr;
        }
        ESP_LOGI(TAG_FACTORY, "ADC Unit %d initialized.", (int)ADC_UNIT);

        // --- Construct and Initialize HAL Wrappers ---
        bool hal_init_success = true;

        // GPIOs
        static EspGpioWrapper power_error_gpio_obj(POWER_ERROR_GPIO, IGpioHal::Direction::Input, IGpioHal::Resistance::None);
        p_power_error_gpio = &power_error_gpio_obj; // Store static address
        hal_init_success &= (p_power_error_gpio->Initialize() == HAL_GPIO_OK);

        static EspGpioWrapper led_standby_gpio_obj(LED_STANDBY_GPIO, IGpioHal::Direction::Output, IGpioHal::Resistance::PullDown);
        p_led_standby_gpio = &led_standby_gpio_obj;
        hal_init_success &= (p_led_standby_gpio->Initialize() == HAL_GPIO_OK);

        // SENSOR_POWER (SPS, GPIO 19) was initialized and driven HIGH before the
        // I2C bus was created (see top of this function) - do NOT touch it here.

        static EspGpioWrapper charging_error_gpio_obj(CHARGING_ERROR_GPIO, IGpioHal::Direction::Input, IGpioHal::Resistance::None);
        p_charging_error_gpio = &charging_error_gpio_obj;
        hal_init_success &= (p_charging_error_gpio->Initialize() == HAL_GPIO_OK);

        static EspGpioWrapper measure_gpio_obj(MEASURE_GPIO, IGpioHal::Direction::Output, IGpioHal::Resistance::None);
        p_measure_gpio = &measure_gpio_obj;
        hal_init_success &= (p_measure_gpio->Initialize() == HAL_GPIO_OK);

        static EspGpioWrapper gnss_reset_gpio_obj(GNSS_RESET_GPIO, IGpioHal::Direction::Output, IGpioHal::Resistance::None);
        p_gnss_reset_gpio = &gnss_reset_gpio_obj;
        hal_init_success &= (p_gnss_reset_gpio->Initialize() == HAL_GPIO_OK);

        ESP_LOGI(TAG_FACTORY, "Initializing GNSS Enable GPIO (GPIO %d)...", (int)GNSS_ENABLE_GPIO);
        static EspGpioWrapper gnss_enable_gpio_obj(GNSS_ENABLE_GPIO, IGpioHal::Direction::Output, IGpioHal::Resistance::None);
        p_gnss_enable_gpio = &gnss_enable_gpio_obj;
        if (p_gnss_enable_gpio->Initialize() != HAL_GPIO_OK)
        {
            ESP_LOGE(TAG_FACTORY, "FAILED to initialize GNSS Enable GPIO %d!", (int)GNSS_ENABLE_GPIO);
            hal_init_success = false;
        }
        else
        {
            ESP_LOGI(TAG_FACTORY, "GNSS Enable GPIO %d initialized successfully.", (int)GNSS_ENABLE_GPIO);
        }

        if (!hal_init_success)
        {
            ESP_LOGE(TAG_FACTORY, "Failed to initialize one or more GPIO wrappers.");
            return nullptr;
        }

        // v1.2.1 fix: CONFIG_PM_SLP_DISABLE_GPIO=y isolates all GPIOs on every
        // auto light-sleep entry (48h provisioning window, REQ-PROV-02). That
        // dropped SENSOR_POWER (SPS) and LED_STANDBY (IS31FL3193 SDB) each sleep
        // interval - SPS re-ramping on every wake re-triggers the I2C bus clamp
        // (see SPS note above) and SDB dropping hardware-shuts-down the LED
        // mid-pattern. Opt these driven outputs out of sleep isolation so they
        // hold their level through light sleep.
        gpio_sleep_sel_dis(SENSOR_POWER_GPIO);
        gpio_sleep_sel_dis(LED_STANDBY_GPIO);
        gpio_sleep_sel_dis(MEASURE_GPIO);
        gpio_sleep_sel_dis(GNSS_RESET_GPIO);
        gpio_sleep_sel_dis(GNSS_ENABLE_GPIO);
        ESP_LOGI(TAG_FACTORY, "Sleep isolation disabled for held outputs (SPS/LED_SDB/MEAS/GNSS).");

        ESP_LOGI(TAG_FACTORY, "Static GPIO Wrappers initialized.");

        // --- ADCs (v1.3.0: dual analog map, selected by board-rev probe) ---
        if (s_board_rev41)
        {
            // Rev 4.1+: fuel on internal ADC1_CH2 (IO2, FUEL_VOLT_AI per the
            // v4.1.1 schematic print); everything else on the ADS1015 with the
            // checkout-proven channel map: AIN0=solar(2x), AIN1=SPS/supply,
            // AIN2=batt-temp(NTC), AIN3=battery(2x). IO0/IO1 stay untouched -
            // they belong to the 32.768 kHz crystal (Y1).
            static EspI2cMasterWrapper ads1015_i2c_wrapper_obj(s_i2c_bus_handle, 0x48, I2C_FREQ_HZ);
            p_ads1015_i2c_wrapper = &ads1015_i2c_wrapper_obj;
            hal_init_success &= (p_ads1015_i2c_wrapper->Initialize() == HAL_I2C_OK);

            static Ads1015Device ads1015_device_obj(ads1015_i2c_wrapper_obj);
            static Ads1015Channel ads_solar_ch_obj(ads1015_device_obj, 0, SOLAR_ADC_SCALAR, 1);
            static Ads1015Channel ads_supply_ch_obj(ads1015_device_obj, 1, 1, 1);
            static Ads1015Channel ads_batt_temp_ch_obj(ads1015_device_obj, 2, 1, 1);
            static Ads1015Channel ads_battery_ch_obj(ads1015_device_obj, 3, BATTERY_ADC_SCALAR, 1);

            static EspAdcWrapperConfig fuel41_adc_cfg_obj = {ADC_UNIT, ADC_CHANNEL_2, DEFAULT_ADC_ATTEN, DEFAULT_ADC_BITWIDTH, 1};
            static EspAdcWrapper fuel41_adc_obj(adc_unit_handle, fuel41_adc_cfg_obj);

            p_fuel_adc = &fuel41_adc_obj;
            p_supply_adc = &ads_supply_ch_obj;
            p_battery_adc = &ads_battery_ch_obj;
            p_solar_adc = &ads_solar_ch_obj;
            p_batt_temp_adc = &ads_batt_temp_ch_obj;

            hal_init_success &= (p_fuel_adc->Initialize() == HAL_ADC_OK);
            hal_init_success &= (p_supply_adc->Initialize() == HAL_ADC_OK);
            hal_init_success &= (p_battery_adc->Initialize() == HAL_ADC_OK);
            hal_init_success &= (p_solar_adc->Initialize() == HAL_ADC_OK);
            hal_init_success &= (p_batt_temp_adc->Initialize() == HAL_ADC_OK);

            // One-time empirical check of the fuel-channel discrepancy: the
            // v4.1.1 schematic print says FUEL_VOLT_AI = ADC1_CH2 (IO2), but
            // the checkout firmware's netlist note said CH3 (IO3 = ADC_RDY per
            // print). Log both once so a bench run with a head attached
            // settles it. Remove after confirmation.
            {
                static EspAdcWrapperConfig fuel_cand3_cfg_obj = {ADC_UNIT, ADC_CHANNEL_3, DEFAULT_ADC_ATTEN, DEFAULT_ADC_BITWIDTH, 1};
                static EspAdcWrapper fuel_cand3_obj(adc_unit_handle, fuel_cand3_cfg_obj);
                int mv2 = -1, mv3 = -1;
                p_fuel_adc->GetMillivolts(&mv2);
                if (fuel_cand3_obj.Initialize() == HAL_ADC_OK)
                {
                    fuel_cand3_obj.GetMillivolts(&mv3);
                }
                ESP_LOGI(TAG_FACTORY, "Fuel-channel check: ADC1_CH2(IO2)=%d mV, ADC1_CH3(IO3)=%d mV (head attached -> the ratiometric one is fuel).", mv2, mv3);
            }
        }
        else
        {
            // Legacy (rev <= 4.0): all five on the internal ADC, CH0-CH4.
            static EspAdcWrapperConfig fuel_adc_cfg_obj = {ADC_UNIT, FUEL_ADC_CHANNEL, DEFAULT_ADC_ATTEN, DEFAULT_ADC_BITWIDTH, 1};
            static EspAdcWrapper fuel_adc_obj(adc_unit_handle, fuel_adc_cfg_obj);
            p_fuel_adc = &fuel_adc_obj;
            hal_init_success &= (p_fuel_adc->Initialize() == HAL_ADC_OK);

            static EspAdcWrapperConfig supply_adc_cfg_obj = {ADC_UNIT, SUPPLY_ADC_CHANNEL, DEFAULT_ADC_ATTEN, DEFAULT_ADC_BITWIDTH, 1};
            static EspAdcWrapper supply_adc_obj(adc_unit_handle, supply_adc_cfg_obj);
            p_supply_adc = &supply_adc_obj;
            hal_init_success &= (p_supply_adc->Initialize() == HAL_ADC_OK);

            static EspAdcWrapperConfig battery_adc_cfg_obj = {ADC_UNIT, BATTERY_ADC_CHANNEL, DEFAULT_ADC_ATTEN, DEFAULT_ADC_BITWIDTH, BATTERY_ADC_SCALAR};
            static EspAdcWrapper battery_adc_obj(adc_unit_handle, battery_adc_cfg_obj);
            p_battery_adc = &battery_adc_obj;
            hal_init_success &= (p_battery_adc->Initialize() == HAL_ADC_OK);

            static EspAdcWrapperConfig solar_adc_cfg_obj = {ADC_UNIT, SOLAR_ADC_CHANNEL, DEFAULT_ADC_ATTEN, DEFAULT_ADC_BITWIDTH, SOLAR_ADC_SCALAR};
            static EspAdcWrapper solar_adc_obj(adc_unit_handle, solar_adc_cfg_obj);
            p_solar_adc = &solar_adc_obj;
            hal_init_success &= (p_solar_adc->Initialize() == HAL_ADC_OK);

            static EspAdcWrapperConfig batt_temp_adc_cfg_obj = {ADC_UNIT, BATT_TEMP_ADC_CHANNEL, DEFAULT_ADC_ATTEN, DEFAULT_ADC_BITWIDTH, 1};
            static EspAdcWrapper batt_temp_adc_obj(adc_unit_handle, batt_temp_adc_cfg_obj);
            p_batt_temp_adc = &batt_temp_adc_obj;
            hal_init_success &= (p_batt_temp_adc->Initialize() == HAL_ADC_OK);
        }

        if (!hal_init_success)
        {
            ESP_LOGE(TAG_FACTORY, "Failed to initialize one or more ADC channels (%s map).",
                     s_board_rev41 ? "rev-4.1" : "legacy");
            return nullptr;
        }
        ESP_LOGI(TAG_FACTORY, "ADC channels initialized (%s analog map).",
                 s_board_rev41 ? "rev-4.1 ADS1015+CH2" : "legacy internal CH0-4");

        // UARTs - GPS module (YIC51009EBGGB) uses 9600 baud by default for NMEA output
        static EspUartWrapper gps_uart_obj(UART_NUM_1, UART_RX_PIN, UART_TX_PIN, nullptr);
        p_gps_uart_obj = &gps_uart_obj;
        hal_init_success &= (p_gps_uart_obj->init(GPS_UART_BAUD_RATE) == ESP_OK);

        if (!hal_init_success)
        {
            ESP_LOGE(TAG_FACTORY, "Failed to initialize UART Device.");
            return nullptr;
        }
        ESP_LOGI(TAG_FACTORY, "Static UART Wrappers initialized.");

        // I2C Devices
        static EspI2cMasterWrapper sht4x_i2c_wrapper_obj(s_i2c_bus_handle, SHT4X_I2C_ADDR, I2C_FREQ_HZ);
        p_sht4x_i2c_wrapper = &sht4x_i2c_wrapper_obj;
        hal_init_success &= (p_sht4x_i2c_wrapper->Initialize() == HAL_I2C_OK);

        static EspI2cMasterWrapper rgbled_i2c_wrapper_obj(s_i2c_bus_handle, RGBLED_I2C_ADDR, I2C_FREQ_HZ);
        p_rgbled_i2c_wrapper = &rgbled_i2c_wrapper_obj;
        hal_init_success &= (p_rgbled_i2c_wrapper->Initialize() == HAL_I2C_OK);

        if (!hal_init_success)
        {
            ESP_LOGE(TAG_FACTORY, "Failed to initialize one or more I2C device wrappers.");
            return nullptr;
        }
        ESP_LOGI(TAG_FACTORY, "Static I2C Device Wrappers initialized.");

        // --- Construct Component Drivers (Now that HAL is initialized) ---
        // These are constructed only once due to being static locals
        static TemperatureSensorSht4x temp_sensor_driver_obj(*p_sht4x_i2c_wrapper);
        p_temp_sensor_driver = &temp_sensor_driver_obj;

        static RgbLedPartNumberIs31fl3193 rgb_led_driver_obj(*p_rgbled_i2c_wrapper, *p_led_standby_gpio);
        p_rgb_led_driver = &rgb_led_driver_obj;

        static AnalogLevelSensor analog_level_driver_obj(*p_fuel_adc, *p_supply_adc);
        p_analog_level_driver = &analog_level_driver_obj;

        static GPSModuleYIC51009EBGGB gps_sensor_driver_obj(gps_uart_obj);
        p_gps_sensor_driver_obj = &gps_sensor_driver_obj;
        
        // Initialize the GPS driver to register event handlers
        if (!p_gps_sensor_driver_obj->Initialize()) {
            ESP_LOGE(TAG_FACTORY, "Failed to initialize GPS driver!");
            // Continue anyway - GPS is not critical for basic operation
        } else {
            ESP_LOGI(TAG_FACTORY, "GPS driver initialized successfully.");
        }

        ESP_LOGI(TAG_FACTORY, "Static Component Drivers constructed.");        

        // --- Construct Main Hardware Driver ---
        static LogiHardwareDriver logi_hardware_driver_obj(
            *p_temp_sensor_driver,
            *p_rgb_led_driver,
            *p_analog_level_driver,
            *p_gps_sensor_driver_obj,
            *p_battery_adc,
            *p_solar_adc,
            *p_batt_temp_adc,
            *p_power_error_gpio,
            *p_sensor_power_gpio,
            *p_charging_error_gpio,
            *p_measure_gpio,
            *p_gnss_reset_gpio,
            *p_gnss_enable_gpio);
        p_logi_hardware_driver = &logi_hardware_driver_obj;
        // v1.2.1: let the driver request a full bus rebuild when transactions
        // are stuck in ESP_ERR_INVALID_STATE (the driver-internal clear-bus
        // recovery provably cannot recover from a wedged bus).
        p_logi_hardware_driver->SetI2cBusRecoveryHook(&EspLogiHardwareFactory::RecoverI2cBus);
        ESP_LOGI(TAG_FACTORY, "Static LogiHardwareDriver constructed.");

        factory_initialized = true; // Mark factory as fully initialized
        ESP_LOGI(TAG_FACTORY, "One-time factory initialization complete.");
    }
    else
    {
        ESP_LOGI(TAG_FACTORY, "Factory already initialized, returning existing pointer.");
    }

    // --- Return pointer to the static LogiHardwareDriver instance ---
    // Cast to the interface pointer type
    return static_cast<ILogiHardwareDriver *>(p_logi_hardware_driver);
}

// --- v1.2.1: full I2C bus recovery ---
// ESP-IDF's i2c.master driver wedges permanently once a transaction hits
// ESP_ERR_INVALID_STATE with the bus clamped ("clear bus failed" /
// "reset hardware failed" on every subsequent transaction). The only reliable
// recovery is deleting and re-creating the master bus, then re-adding devices.
bool EspLogiHardwareFactory::RecoverI2cBus()
{
    ESP_LOGW(TAG_FACTORY, "I2C bus recovery: deleting and re-creating master bus...");

    // Devices must be removed before the bus can be deleted.
    if (p_sht4x_i2c_wrapper) p_sht4x_i2c_wrapper->Detach();
    if (p_rgbled_i2c_wrapper) p_rgbled_i2c_wrapper->Detach();
    if (p_ads1015_i2c_wrapper) p_ads1015_i2c_wrapper->Detach();

    if (s_i2c_bus_handle != NULL)
    {
        esp_err_t del_err = i2c_del_master_bus(s_i2c_bus_handle);
        s_i2c_bus_handle = NULL;
        if (del_err != ESP_OK)
        {
            ESP_LOGE(TAG_FACTORY, "I2C bus recovery: del_master_bus failed: %s (continuing)", esp_err_to_name(del_err));
        }
    }

    esp_err_t bus_err = CreateI2cMasterBus(I2C_PORT, I2C_SDA_PIN, I2C_SCL_PIN);
    if (bus_err != ESP_OK || s_i2c_bus_handle == NULL)
    {
        ESP_LOGE(TAG_FACTORY, "I2C bus recovery: re-create failed: %s", esp_err_to_name(bus_err));
        return false;
    }

    bool ok = true;
    if (p_sht4x_i2c_wrapper) ok &= (p_sht4x_i2c_wrapper->Reattach(s_i2c_bus_handle) == HAL_I2C_OK);
    if (p_rgbled_i2c_wrapper) ok &= (p_rgbled_i2c_wrapper->Reattach(s_i2c_bus_handle) == HAL_I2C_OK);
    if (p_ads1015_i2c_wrapper) ok &= (p_ads1015_i2c_wrapper->Reattach(s_i2c_bus_handle) == HAL_I2C_OK);

    if (ok)
    {
        ESP_LOGW(TAG_FACTORY, "I2C bus recovery complete.");
    }
    else
    {
        ESP_LOGE(TAG_FACTORY, "I2C bus recovery: device re-add failed.");
    }
    return ok;
}