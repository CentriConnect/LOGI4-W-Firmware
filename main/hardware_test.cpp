//==============================================================================
// LOGI4W PCBA Hardware Checkout Test - Implementation
//
// Purpose: Test firmware for validating each hardware function on the LOGI4W
// PCBA. Outputs results via serial console (115200 baud).
//
// GPIO Corrections from schematic:
// - Debug LED is IO21 (not IO22)
// - LED_STANDBY_DO (RGB LED driver SDB control) is IO19
// - IO22 = CHG_STATUS_DI (charging status input from SPV1050)
//==============================================================================

#include "hardware_test.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "hal/EspI2cMasterWrapper.h"
#include "drivers/LevelSensorI2c9705.h"

static const char* TAG = "HW_TEST";

//==============================================================================
// Static handles for hardware resources
//==============================================================================
static i2c_master_bus_handle_t s_i2c_bus_handle = NULL;
static adc_oneshot_unit_handle_t s_adc_handle = NULL;
static adc_cali_handle_t s_adc_cali_handle = NULL;
static bool s_hw_initialized = false;

//==============================================================================
// Helper: Delay in milliseconds
//==============================================================================
static void delay_ms(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms));
}

//==============================================================================
// Helper: Initialize GPIO as output
//==============================================================================
static esp_err_t init_gpio_output(gpio_num_t pin)
{
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << pin);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    return gpio_config(&io_conf);
}

//==============================================================================
// Helper: Initialize GPIO as input
//==============================================================================
static esp_err_t init_gpio_input(gpio_num_t pin, bool pullup, bool pulldown)
{
    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << pin);
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pull_up_en = pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = pulldown ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    return gpio_config(&io_conf);
}

//==============================================================================
// Initialize shared hardware resources
//==============================================================================
static bool init_shared_hardware(void)
{
    if (s_hw_initialized) {
        return true;
    }

    ESP_LOGI(TAG, "Initializing shared hardware resources...");

    // FIRST: Enable sensor power supply (IO12) - required for SHT4x to work
    ESP_LOGI(TAG, "  Enabling sensor power (IO%d)...", PIN_SPS_ENABLE);
    ESP_LOGI(TAG, "    Configuring GPIO...");
    esp_err_t gpio_err = init_gpio_output(PIN_SPS_ENABLE);
    if (gpio_err != ESP_OK) {
        ESP_LOGW(TAG, "    WARNING: GPIO%d config failed: %s (continuing)", PIN_SPS_ENABLE, esp_err_to_name(gpio_err));
    } else {
        ESP_LOGI(TAG, "    Setting level HIGH...");
        gpio_set_level(PIN_SPS_ENABLE, 1);
        ESP_LOGI(TAG, "    Done.");
    }
    delay_ms(50);  // Allow power to stabilize

    // Also enable measurement dividers (IO20)
    ESP_LOGI(TAG, "  Enabling measurement dividers (IO%d)...", PIN_MON_MEAS);
    gpio_err = init_gpio_output(PIN_MON_MEAS);
    if (gpio_err != ESP_OK) {
        ESP_LOGW(TAG, "    WARNING: GPIO%d config failed: %s (continuing)", PIN_MON_MEAS, esp_err_to_name(gpio_err));
    } else {
        gpio_set_level(PIN_MON_MEAS, 1);
    }

    // Initialize I2C bus
    ESP_LOGI(TAG, "  Initializing I2C bus (SDA=%d, SCL=%d)...", PIN_I2C_SDA, PIN_I2C_SCL);
    i2c_master_bus_config_t i2c_bus_conf = {};
    i2c_bus_conf.i2c_port = I2C_NUM_0;
    i2c_bus_conf.sda_io_num = PIN_I2C_SDA;
    i2c_bus_conf.scl_io_num = PIN_I2C_SCL;
    i2c_bus_conf.clk_source = I2C_CLK_SRC_DEFAULT;
    i2c_bus_conf.flags.enable_internal_pullup = true;
    i2c_bus_conf.glitch_ignore_cnt = 7;

    esp_err_t err = i2c_new_master_bus(&i2c_bus_conf, &s_i2c_bus_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "  Failed to create I2C bus: %s", esp_err_to_name(err));
        return false;
    }
    ESP_LOGI(TAG, "  I2C bus initialized.");

    // Initialize ADC unit
    ESP_LOGI(TAG, "  Initializing ADC unit...");
    adc_oneshot_unit_init_cfg_t adc_cfg = {};
    adc_cfg.unit_id = ADC_UNIT_1;
    adc_cfg.ulp_mode = ADC_ULP_MODE_DISABLE;

    err = adc_oneshot_new_unit(&adc_cfg, &s_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "  Failed to create ADC unit: %s", esp_err_to_name(err));
        return false;
    }

    // Configure all 4 ADC channels
    adc_oneshot_chan_cfg_t chan_cfg = {};
    chan_cfg.atten = ADC_ATTEN_DB_12;
    chan_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;

    adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_0, &chan_cfg); // Fuel
    adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_1, &chan_cfg); // SPS
    adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_2, &chan_cfg); // Battery
    adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL_3, &chan_cfg); // Solar

    // Initialize ADC calibration
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id = ADC_UNIT_1;
    cali_cfg.atten = ADC_ATTEN_DB_12;
    cali_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    err = adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_adc_cali_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_cfg = {};
    cali_cfg.unit_id = ADC_UNIT_1;
    cali_cfg.atten = ADC_ATTEN_DB_12;
    cali_cfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    err = adc_cali_create_scheme_line_fitting(&cali_cfg, &s_adc_cali_handle);
#endif
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "  ADC calibration initialized.");
    } else {
        ESP_LOGW(TAG, "  ADC calibration not available: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "  ADC unit initialized.");

    s_hw_initialized = true;
    return true;
}

//==============================================================================
// Test 1: GPIO Output Test (IO21 - LED Standby/SDB pin)
//==============================================================================
bool TestDebugLed(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "TEST 1: GPIO Output Test (IO%d)", PIN_DEBUG_LED);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  Note: This pin controls the RGB LED driver SDB");
    ESP_LOGI(TAG, "  HIGH = LED driver active, LOW = LED driver standby");

    esp_err_t err = init_gpio_output(PIN_DEBUG_LED);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "  FAIL: Could not configure GPIO%d: %s", PIN_DEBUG_LED, esp_err_to_name(err));
        return false;
    }

    ESP_LOGI(TAG, "  Toggling GPIO %d times...", DEBUG_LED_BLINK_COUNT);

    for (int i = 0; i < DEBUG_LED_BLINK_COUNT; i++) {
        gpio_set_level(PIN_DEBUG_LED, 1);
        ESP_LOGI(TAG, "    GPIO HIGH (%d/%d) - LED driver active", i + 1, DEBUG_LED_BLINK_COUNT);
        delay_ms(500);
        gpio_set_level(PIN_DEBUG_LED, 0);
        ESP_LOGI(TAG, "    GPIO LOW  (%d/%d) - LED driver standby", i + 1, DEBUG_LED_BLINK_COUNT);
        delay_ms(500);
    }

    // Leave HIGH so LED driver is active for subsequent tests
    gpio_set_level(PIN_DEBUG_LED, 1);

    ESP_LOGI(TAG, "  PASS: GPIO output test complete");
    return true;
}

//==============================================================================
// Test 2: LED Standby Control (same as Test 1 - IO21)
//==============================================================================
bool TestLedStandbyControl(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "TEST 2: LED Standby Control (IO%d)", PIN_LED_STANDBY);
    ESP_LOGI(TAG, "========================================");

    // IS31FL3193 SDB pin: LOW = standby/shutdown, HIGH = active
    // This is the same pin as PIN_DEBUG_LED, already tested in Test 1

    ESP_LOGI(TAG, "  (Already configured in Test 1)");
    ESP_LOGI(TAG, "  Verifying LED driver is enabled (GPIO HIGH)...");

    esp_err_t err = init_gpio_output(PIN_LED_STANDBY);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "  FAIL: Could not configure GPIO%d: %s", PIN_LED_STANDBY, esp_err_to_name(err));
        return false;
    }

    gpio_set_level(PIN_LED_STANDBY, 1);  // Enable LED driver
    delay_ms(10);

    ESP_LOGI(TAG, "  PASS: LED standby control configured (driver enabled)");
    return true;
}

//==============================================================================
// I2C Scan for Devices
//==============================================================================
void ScanI2cDevices(bool* found_sht4x, bool* found_rgb, bool* found_9705)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "TEST 3: I2C Bus Scan");
    ESP_LOGI(TAG, "========================================");

    *found_sht4x = false;
    *found_rgb = false;
    *found_9705 = false;

    if (!s_i2c_bus_handle) {
        ESP_LOGE(TAG, "  FAIL: I2C bus not initialized");
        return;
    }

    ESP_LOGI(TAG, "  Scanning I2C bus for devices (using i2c_master_probe)...");

    int devices_found = 0;

    // Only scan for our expected devices to avoid interference
    uint8_t target_addrs[] = {SHT4X_I2C_ADDR, IS31FL3193_I2C_ADDR, ROCHESTER_9705_I2C_ADDR};

    for (int i = 0; i < 3; i++) {
        uint8_t addr = target_addrs[i];

        esp_err_t err = i2c_master_probe(s_i2c_bus_handle, addr, 50);

        if (err == ESP_OK) {
            devices_found++;
            if (addr == SHT4X_I2C_ADDR) {
                *found_sht4x = true;
                ESP_LOGI(TAG, "    Found SHT4x at 0x%02X", addr);
            } else if (addr == IS31FL3193_I2C_ADDR) {
                *found_rgb = true;
                ESP_LOGI(TAG, "    Found IS31FL3193 (RGB LED) at 0x%02X", addr);
            } else if (addr == ROCHESTER_9705_I2C_ADDR) {
                *found_9705 = true;
                ESP_LOGI(TAG, "    Found Rochester 9705 (Fuel Sensor) at 0x%02X", addr);
            }
        } else {
            ESP_LOGI(TAG, "    Probe 0x%02X: %s", addr, esp_err_to_name(err));
        }

        delay_ms(10);  // Small delay between device probes
    }

    ESP_LOGI(TAG, "  Targeted scan complete. Devices found: %d", devices_found);
    ESP_LOGI(TAG, "    SHT4x (0x%02X): %s", SHT4X_I2C_ADDR, *found_sht4x ? "FOUND" : "NOT FOUND");
    ESP_LOGI(TAG, "    IS31FL3193 (0x%02X): %s", IS31FL3193_I2C_ADDR, *found_rgb ? "FOUND" : "NOT FOUND");
    ESP_LOGI(TAG, "    Rochester 9705 (0x%02X): %s", ROCHESTER_9705_I2C_ADDR, *found_9705 ? "FOUND" : "NOT FOUND");

    // Full bus scan using i2c_master_probe (address-only, no data byte)
    ESP_LOGI(TAG, "  Full I2C bus scan (0x03-0x77) using i2c_master_probe...");
    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        esp_err_t err = i2c_master_probe(s_i2c_bus_handle, addr, 50);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "    >> Device ACK at 0x%02X <<", addr);
        }
        delay_ms(2);
    }
    ESP_LOGI(TAG, "  Full scan complete.");
}

//==============================================================================
// Test SHT4x Temperature/Humidity Sensor
//==============================================================================
bool TestSht4xSensor(float* temp_c, float* humidity_pct)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "TEST 4: SHT4x Temperature/Humidity Sensor");
    ESP_LOGI(TAG, "========================================");

    *temp_c = 0.0f;
    *humidity_pct = 0.0f;

    if (!s_i2c_bus_handle) {
        ESP_LOGE(TAG, "  FAIL: I2C bus not initialized");
        return false;
    }

    // Make sure sensor power is enabled
    gpio_set_level(PIN_SPS_ENABLE, 1);
    delay_ms(50);

    // Add SHT4x device with slower clock for reliability
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = SHT4X_I2C_ADDR;
    dev_cfg.scl_speed_hz = 100000;  // Use 100kHz for reliability

    i2c_master_dev_handle_t sht4x_handle = NULL;
    esp_err_t err = i2c_master_bus_add_device(s_i2c_bus_handle, &dev_cfg, &sht4x_handle);
    if (err != ESP_OK || sht4x_handle == NULL) {
        ESP_LOGE(TAG, "  FAIL: Could not add SHT4x device: %s", esp_err_to_name(err));
        return false;
    }

    delay_ms(10);  // Allow device to settle after adding to bus

    // First, send soft reset to ensure clean state
    ESP_LOGI(TAG, "  Sending soft reset...");
    uint8_t reset_cmd = 0x94;
    err = i2c_master_transmit(sht4x_handle, &reset_cmd, 1, 100);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "  WARNING: Soft reset failed: %s (continuing anyway)", esp_err_to_name(err));
    }
    delay_ms(10);  // Wait for reset to complete

    // Send high precision measurement command (0xFD)
    ESP_LOGI(TAG, "  Sending measurement command (0xFD)...");
    uint8_t cmd = 0xFD;
    err = i2c_master_transmit(sht4x_handle, &cmd, 1, 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "  FAIL: Could not send command: %s", esp_err_to_name(err));
        i2c_master_bus_rm_device(sht4x_handle);
        return false;
    }

    // Wait for measurement (SHT4x high precision needs ~8.2ms, use 20ms to be safe)
    ESP_LOGI(TAG, "  Waiting for measurement...");
    delay_ms(20);

    // Read 6 bytes: temp_msb, temp_lsb, temp_crc, hum_msb, hum_lsb, hum_crc
    ESP_LOGI(TAG, "  Reading data...");
    uint8_t data[6] = {0};
    err = i2c_master_receive(sht4x_handle, data, 6, 100);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "  FAIL: Could not read data: %s", esp_err_to_name(err));
        ESP_LOGI(TAG, "  (Sensor may need power - check IO12 enables sensor supply)");
        i2c_master_bus_rm_device(sht4x_handle);
        return false;
    }

    ESP_LOGI(TAG, "  Raw bytes: %02X %02X %02X %02X %02X %02X",
             data[0], data[1], data[2], data[3], data[4], data[5]);

    // Calculate CRC for verification (SHT4x uses CRC-8 with polynomial 0x31, init 0xFF)
    auto calc_crc = [](uint8_t* data) -> uint8_t {
        uint8_t crc = 0xFF;
        for (int i = 0; i < 2; i++) {
            crc ^= data[i];
            for (int b = 0; b < 8; b++) {
                if (crc & 0x80) {
                    crc = (crc << 1) ^ 0x31;
                } else {
                    crc <<= 1;
                }
            }
        }
        return crc;
    };

    uint8_t temp_crc_calc = calc_crc(&data[0]);
    uint8_t hum_crc_calc = calc_crc(&data[3]);

    if (temp_crc_calc != data[2] || hum_crc_calc != data[5]) {
        ESP_LOGE(TAG, "  FAIL: CRC mismatch! Temp CRC: 0x%02X (calc 0x%02X), Hum CRC: 0x%02X (calc 0x%02X)",
                 data[2], temp_crc_calc, data[5], hum_crc_calc);
        i2c_master_bus_rm_device(sht4x_handle);
        return false;
    }

    // Convert raw values to physical units
    uint16_t raw_temp = ((uint16_t)data[0] << 8) | data[1];
    uint16_t raw_hum = ((uint16_t)data[3] << 8) | data[4];

    *temp_c = -45.0f + 175.0f * ((float)raw_temp / 65535.0f);
    *humidity_pct = -6.0f + 125.0f * ((float)raw_hum / 65535.0f);

    // Clamp humidity
    if (*humidity_pct > 100.0f) *humidity_pct = 100.0f;
    if (*humidity_pct < 0.0f) *humidity_pct = 0.0f;

    ESP_LOGI(TAG, "  Raw data: Temp=0x%04X, Hum=0x%04X", raw_temp, raw_hum);
    ESP_LOGI(TAG, "  PASS: Temperature = %.2f C, Humidity = %.1f %%", *temp_c, *humidity_pct);

    // Sanity check
    if (*temp_c < -40.0f || *temp_c > 125.0f) {
        ESP_LOGW(TAG, "  WARNING: Temperature out of expected range!");
    }

    i2c_master_bus_rm_device(sht4x_handle);
    return true;
}

//==============================================================================
// Test I2C Fuel Level Sensor (Rochester 9705)
//==============================================================================
bool TestI2cFuelSensor(HardwareTestResults_t* results)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "TEST 5: I2C Fuel Sensor (Rochester 9705)");
    ESP_LOGI(TAG, "========================================");

    results->fuel_i2c_init_pass = false;
    results->fuel_i2c_read_pass = false;
    results->fuel_i2c_temp_c = 0.0f;
    results->fuel_i2c_level_pct = 0;

    if (!s_i2c_bus_handle) {
        ESP_LOGE(TAG, "  FAIL: I2C bus not initialized");
        return false;
    }

    // Create HAL wrapper for the 9705 sensor on the existing I2C bus
    ESP_LOGI(TAG, "  Creating I2C wrapper for device 0x%02X...", ROCHESTER_9705_I2C_ADDR);
    EspI2cMasterWrapper fuel_i2c(s_i2c_bus_handle, ROCHESTER_9705_I2C_ADDR, 100000);

    HalI2cError hal_err = fuel_i2c.Initialize();
    if (hal_err != HAL_I2C_OK) {
        ESP_LOGE(TAG, "  FAIL: Could not initialize I2C wrapper (err: %d)", hal_err);
        ESP_LOGI(TAG, "  (Sensor may not be connected to the 4-pin connector)");
        return false;
    }

    // Create the sensor driver using the HAL wrapper
    ESP_LOGI(TAG, "  Initializing Rochester 9705 sensor driver...");
    LevelSensorI2c9705 fuel_sensor(fuel_i2c);

    bool init_ok = fuel_sensor.Sensor_Initialize();
    results->fuel_i2c_init_pass = init_ok;

    if (!init_ok) {
        ESP_LOGE(TAG, "  FAIL: Sensor_Initialize() failed");
        ESP_LOGI(TAG, "  (Sensor may not be connected or not responding)");
        return false;
    }
    ESP_LOGI(TAG, "  PASS: Sensor initialized successfully");

    // Perform multiple reads to allow the sensor to warm up
    float temp = 0.0f;
    uint16_t level = 0;
    bool read_ok = false;

    ESP_LOGI(TAG, "  Performing warm-up reads...");
    for (int i = 0; i < 3; i++) {
        read_ok = fuel_sensor.Read(&temp, &level);
        if (read_ok) {
            ESP_LOGI(TAG, "    Read %d: Temp=%.2f C, Level=%u%%", i + 1, temp, level);
        } else {
            ESP_LOGW(TAG, "    Read %d: FAILED", i + 1);
        }
        delay_ms(100);
    }

    results->fuel_i2c_read_pass = read_ok;
    results->fuel_i2c_temp_c = temp;
    results->fuel_i2c_level_pct = level;

    if (read_ok) {
        ESP_LOGI(TAG, "  PASS: Temperature = %.2f C, Level = %u%%", temp, level);

        // Sanity check temperature
        if (temp < -40.0f || temp > 125.0f) {
            ESP_LOGW(TAG, "  WARNING: Temperature out of expected range!");
        }
    } else {
        ESP_LOGE(TAG, "  FAIL: Could not read sensor data after retries");
    }

    return read_ok;
}

//==============================================================================
// Test ADC Channels
//==============================================================================
bool TestAdcChannels(int* batt_mv, int* solar_mv, int* sps_mv, int* fuel_mv)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "TEST 6: ADC Channels");
    ESP_LOGI(TAG, "========================================");

    *batt_mv = 0;
    *solar_mv = 0;
    *sps_mv = 0;
    *fuel_mv = 0;

    if (!s_adc_handle) {
        ESP_LOGE(TAG, "  FAIL: ADC not initialized");
        return false;
    }

    // Enable sensor power and measurement dividers
    ESP_LOGI(TAG, "  Enabling sensor power (IO%d) and measurement (IO%d)...", PIN_SPS_ENABLE, PIN_MON_MEAS);
    init_gpio_output(PIN_SPS_ENABLE);
    init_gpio_output(PIN_MON_MEAS);
    gpio_set_level(PIN_SPS_ENABLE, 1);
    gpio_set_level(PIN_MON_MEAS, 1);
    delay_ms(100); // Allow power to stabilize

    ESP_LOGI(TAG, "  Reading ADC channels (averaging %d samples)...", ADC_SAMPLE_COUNT);

    auto read_adc_averaged = [](adc_channel_t ch, int* out_mv, int scalar) -> bool {
        int raw_sum = 0;
        int mv_sum = 0;

        for (int i = 0; i < ADC_SAMPLE_COUNT; i++) {
            int raw = 0;
            esp_err_t err = adc_oneshot_read(s_adc_handle, ch, &raw);
            if (err != ESP_OK) {
                return false;
            }
            raw_sum += raw;

            if (s_adc_cali_handle) {
                int mv = 0;
                adc_cali_raw_to_voltage(s_adc_cali_handle, raw, &mv);
                mv_sum += mv;
            }
            delay_ms(5);
        }

        int raw_avg = raw_sum / ADC_SAMPLE_COUNT;

        if (s_adc_cali_handle) {
            *out_mv = (mv_sum / ADC_SAMPLE_COUNT) * scalar;
        } else {
            // Fallback: estimate millivolts from raw (assuming 12-bit, 3.3V reference)
            *out_mv = ((raw_avg * 3300) / 4095) * scalar;
        }

        ESP_LOGI(TAG, "    Channel %d: Raw=%d, Voltage=%d mV (scalar=%d)",
                 ch, raw_avg, *out_mv, scalar);
        return true;
    };

    bool all_pass = true;

    // Battery voltage (IO2, ADC1_CH2) - 2x voltage divider
    ESP_LOGI(TAG, "  Battery Voltage (ADC1_CH2, 2x divider):");
    all_pass &= read_adc_averaged(ADC_CHANNEL_2, batt_mv, 2);

    // Solar voltage (IO3, ADC1_CH3) - 2x voltage divider
    ESP_LOGI(TAG, "  Solar Voltage (ADC1_CH3, 2x divider):");
    all_pass &= read_adc_averaged(ADC_CHANNEL_3, solar_mv, 2);

    // Sensor power supply voltage (IO1, ADC1_CH1) - direct
    ESP_LOGI(TAG, "  Sensor Power Supply (ADC1_CH1):");
    all_pass &= read_adc_averaged(ADC_CHANNEL_1, sps_mv, 1);

    // Fuel sensor voltage (IO0, ADC1_CH0) - direct
    ESP_LOGI(TAG, "  Fuel Sensor (ADC1_CH0):");
    all_pass &= read_adc_averaged(ADC_CHANNEL_0, fuel_mv, 1);

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "  ADC Summary:");
    ESP_LOGI(TAG, "    Battery:  %d mV %s", *batt_mv,
             (*batt_mv > 2500 && *batt_mv < 4500) ? "(OK - battery detected)" :
             (*batt_mv < 100) ? "(No battery connected)" : "(unexpected value)");
    ESP_LOGI(TAG, "    Solar:    %d mV %s", *solar_mv,
             (*solar_mv < 100) ? "(No solar connected - OK)" : "(Solar detected)");
    ESP_LOGI(TAG, "    SPS:      %d mV %s", *sps_mv,
             (*sps_mv > 2800 && *sps_mv < 3600) ? "(OK - ~3.3V)" : "(unexpected value)");
    ESP_LOGI(TAG, "    Fuel:     %d mV %s", *fuel_mv,
             (*fuel_mv < 100) ? "(No sensor connected - OK)" : "(Signal detected)");

    if (all_pass) {
        ESP_LOGI(TAG, "  PASS: All ADC channels read successfully");
        ESP_LOGI(TAG, "  Note: Low/zero values for battery, solar, fuel are OK if not connected");
    } else {
        ESP_LOGE(TAG, "  FAIL: One or more ADC channels failed to read");
    }

    return all_pass;
}

//==============================================================================
// Test GNSS Module
//==============================================================================
static bool s_gnss_nmea_received = false;
static char s_gnss_buffer[512];
static int s_gnss_buffer_pos = 0;

// Temporary storage for GSA parsing during GNSS test
static uint8_t s_gnss_fix_type = 1;
static float s_gnss_pdop = 99.9f;
static float s_gnss_hdop = 99.9f;
static float s_gnss_vdop = 99.9f;
static uint8_t s_gnss_sats_used = 0;
static uint8_t s_gnss_sats_in_view = 0;
static int s_gnss_max_snr = 0;
static uint32_t s_gnss_ttff_ms = 0;
static uint32_t s_gnss_start_time = 0;
static bool s_gnss_ttff_measured = false;

void TestGnssModule(HardwareTestResults_t* results)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "TEST 7: GNSS Module (YIC51009EBGGB)");
    ESP_LOGI(TAG, "========================================");

    results->gnss_latitude = 0.0;
    results->gnss_longitude = 0.0;
    results->gnss_altitude = 0.0f;
    results->gnss_power_pass = false;
    results->gnss_nmea_pass = false;
    results->gnss_fix_pass = false;
    results->gnss_satellites_used = 0;
    results->gnss_satellites_in_view = 0;
    results->gnss_fix_type = 1;
    results->gnss_hdop = 99.9f;
    results->gnss_vdop = 99.9f;
    results->gnss_pdop = 99.9f;
    results->gnss_ttff_ms = 0;
    results->gnss_max_snr = 0;

    // Reset static variables
    s_gnss_fix_type = 1;
    s_gnss_pdop = 99.9f;
    s_gnss_hdop = 99.9f;
    s_gnss_vdop = 99.9f;
    s_gnss_sats_used = 0;
    s_gnss_sats_in_view = 0;
    s_gnss_max_snr = 0;
    s_gnss_ttff_ms = 0;
    s_gnss_ttff_measured = false;

    // Configure GNSS control GPIOs
    ESP_LOGI(TAG, "  Configuring GNSS control pins...");
    init_gpio_output(PIN_GNSS_ENABLE);
    init_gpio_output(PIN_GNSS_RESET);

    // Power sequence:
    // 1. Hold reset LOW
    // 2. Enable power (IO11 HIGH)
    // 3. Wait for power stable
    // 4. Release reset (IO10 HIGH due to external pullup, or drive HIGH)

    ESP_LOGI(TAG, "  Step 1: Hold GNSS in reset (IO%d LOW)...", PIN_GNSS_RESET);
    gpio_set_level(PIN_GNSS_RESET, 0);

    ESP_LOGI(TAG, "  Step 2: Enable GNSS power (IO%d HIGH)...", PIN_GNSS_ENABLE);
    gpio_set_level(PIN_GNSS_ENABLE, 1);
    delay_ms(50);

    ESP_LOGI(TAG, "  >> VERIFY: D9 LED (GNSS power indicator) should illuminate <<");
    results->gnss_power_pass = true; // Assume OK if we get here

    ESP_LOGI(TAG, "  Step 3: Release reset (IO%d HIGH)...", PIN_GNSS_RESET);
    gpio_set_level(PIN_GNSS_RESET, 1);
    delay_ms(100);

    // Record start time for TTFF measurement
    s_gnss_start_time = xTaskGetTickCount();

    // Initialize UART for GNSS
    ESP_LOGI(TAG, "  Initializing UART for GNSS (RX=%d, TX=%d, %d baud)...",
             PIN_GNSS_RXD, PIN_GNSS_TXD, GNSS_BAUD_RATE);

    uart_config_t uart_config = {
        .baud_rate = GNSS_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(UART_NUM_1, 2048, 0, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "  FAIL: Could not install UART driver: %s", esp_err_to_name(err));
        return;
    }

    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, PIN_GNSS_TXD, PIN_GNSS_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // Enable pull-up on RX pin
    gpio_set_pull_mode(PIN_GNSS_RXD, GPIO_PULLUP_ONLY);

    ESP_LOGI(TAG, "  Waiting for NMEA data...");

    // Read NMEA data for up to GNSS_FIX_TIMEOUT_SEC
    uint32_t start_time = xTaskGetTickCount();
    uint32_t timeout_ticks = pdMS_TO_TICKS(GNSS_FIX_TIMEOUT_SEC * 1000);
    bool first_nmea = true;
    int nmea_sentence_count = 0;

    while ((xTaskGetTickCount() - start_time) < timeout_ticks) {
        uint8_t data[256];
        int len = uart_read_bytes(UART_NUM_1, data, sizeof(data) - 1, pdMS_TO_TICKS(1000));

        if (len > 0) {
            data[len] = '\0';

            if (first_nmea) {
                ESP_LOGI(TAG, "  NMEA data received!");
                results->gnss_nmea_pass = true;
                first_nmea = false;
            }

            // Parse NMEA sentences looking for position fix
            char* line = (char*)data;
            while (*line) {
                char* eol = strchr(line, '\n');
                if (eol) {
                    *eol = '\0';

                    // Remove CR if present
                    char* cr = strchr(line, '\r');
                    if (cr) *cr = '\0';

                    if (line[0] == '$') {
                        nmea_sentence_count++;

                        // Print first few sentences for debugging
                        if (nmea_sentence_count <= 5) {
                            ESP_LOGI(TAG, "    NMEA: %s", line);
                        } else if (nmea_sentence_count == 6) {
                            ESP_LOGI(TAG, "    (suppressing further NMEA output...)");
                        }

                        // Parse GGA sentence for position
                        if (strncmp(line, "$GNGGA", 6) == 0 || strncmp(line, "$GPGGA", 6) == 0) {
                            char buf[256];
                            strncpy(buf, line, sizeof(buf) - 1);
                            buf[sizeof(buf) - 1] = '\0';

                            char* tokens[20] = {NULL};
                            int tc = 0;
                            char* tok = strtok(buf, ",");
                            while (tok && tc < 20) {
                                tokens[tc++] = tok;
                                tok = strtok(NULL, ",");
                            }

                            // Parse satellites used (field 7)
                            if (tc > 7 && tokens[7] && strlen(tokens[7]) > 0) {
                                s_gnss_sats_used = (uint8_t)atoi(tokens[7]);
                            }

                            // Parse HDOP from GGA (field 8)
                            if (tc > 8 && tokens[8] && strlen(tokens[8]) > 0) {
                                s_gnss_hdop = (float)atof(tokens[8]);
                            }

                            // Check fix quality (field 6, index 6)
                            if (tc > 6 && tokens[6] && atoi(tokens[6]) > 0) {
                                // We have a fix! Calculate TTFF if not already done
                                if (!s_gnss_ttff_measured) {
                                    uint32_t current_time = xTaskGetTickCount();
                                    s_gnss_ttff_ms = (current_time - s_gnss_start_time) * portTICK_PERIOD_MS;
                                    s_gnss_ttff_measured = true;
                                    ESP_LOGI(TAG, "  TTFF: %lu ms", s_gnss_ttff_ms);
                                }

                                results->gnss_fix_pass = true;

                                // Parse latitude (field 2, direction field 3)
                                if (tc > 4 && tokens[2] && tokens[3] && strlen(tokens[2]) > 0) {
                                    char deg_buf[3] = {tokens[2][0], tokens[2][1], '\0'};
                                    double deg = atof(deg_buf);
                                    double min = atof(tokens[2] + 2);
                                    results->gnss_latitude = deg + (min / 60.0);
                                    if (tokens[3][0] == 'S') results->gnss_latitude = -results->gnss_latitude;
                                }

                                // Parse longitude (field 4, direction field 5)
                                if (tc > 5 && tokens[4] && tokens[5] && strlen(tokens[4]) > 0) {
                                    char deg_buf[4] = {tokens[4][0], tokens[4][1], tokens[4][2], '\0'};
                                    double deg = atof(deg_buf);
                                    double min = atof(tokens[4] + 3);
                                    results->gnss_longitude = deg + (min / 60.0);
                                    if (tokens[5][0] == 'W') results->gnss_longitude = -results->gnss_longitude;
                                }

                                // Parse altitude (field 9)
                                if (tc > 9 && tokens[9] && strlen(tokens[9]) > 0) {
                                    results->gnss_altitude = (float)atof(tokens[9]);
                                }

                                ESP_LOGI(TAG, "  GPS FIX OBTAINED!");
                                ESP_LOGI(TAG, "    Latitude:  %.6f", results->gnss_latitude);
                                ESP_LOGI(TAG, "    Longitude: %.6f", results->gnss_longitude);
                                ESP_LOGI(TAG, "    Altitude:  %.1f m", results->gnss_altitude);
                                goto gnss_test_done;
                            }
                        }
                        // Parse GSA sentence for DOP values and fix type
                        else if (strncmp(line, "$GNGSA", 6) == 0 || strncmp(line, "$GPGSA", 6) == 0) {
                            char buf[256];
                            strncpy(buf, line, sizeof(buf) - 1);
                            buf[sizeof(buf) - 1] = '\0';

                            char* tokens[20] = {NULL};
                            int tc = 0;
                            char* tok = strtok(buf, ",");
                            while (tok && tc < 20) {
                                tokens[tc++] = tok;
                                tok = strtok(NULL, ",");
                            }

                            // Fix type (field 2): 1=no fix, 2=2D, 3=3D
                            if (tc > 2 && tokens[2] && strlen(tokens[2]) > 0) {
                                s_gnss_fix_type = (uint8_t)atoi(tokens[2]);
                            }

                            // PDOP (field 15)
                            if (tc > 15 && tokens[15] && strlen(tokens[15]) > 0) {
                                s_gnss_pdop = (float)atof(tokens[15]);
                            }

                            // HDOP (field 16)
                            if (tc > 16 && tokens[16] && strlen(tokens[16]) > 0) {
                                s_gnss_hdop = (float)atof(tokens[16]);
                            }

                            // VDOP (field 17) - may have checksum attached
                            if (tc > 17 && tokens[17] && strlen(tokens[17]) > 0) {
                                char vdopStr[16];
                                strncpy(vdopStr, tokens[17], sizeof(vdopStr) - 1);
                                vdopStr[sizeof(vdopStr) - 1] = '\0';
                                char* checksum = strchr(vdopStr, '*');
                                if (checksum) *checksum = '\0';
                                s_gnss_vdop = (float)atof(vdopStr);
                            }
                        }
                        // Parse GSV sentence for satellites in view and SNR
                        else if (strncmp(line, "$GNGSV", 6) == 0 || strncmp(line, "$GPGSV", 6) == 0) {
                            char buf[256];
                            strncpy(buf, line, sizeof(buf) - 1);
                            buf[sizeof(buf) - 1] = '\0';

                            char* tokens[20] = {NULL};
                            int tc = 0;
                            char* tok = strtok(buf, ",");
                            while (tok && tc < 20) {
                                tokens[tc++] = tok;
                                tok = strtok(NULL, ",");
                            }

                            // Get message number (field 2)
                            int msgNum = (tc > 2 && tokens[2]) ? atoi(tokens[2]) : 0;

                            // Parse satellites in view from first message only (field 3)
                            if (msgNum == 1 && tc > 3 && tokens[3] && strlen(tokens[3]) > 0) {
                                s_gnss_sats_in_view = (uint8_t)atoi(tokens[3]);
                            }

                            // Parse SNR values (4th field of each satellite group)
                            for (int i = 4; i + 3 < tc; i += 4) {
                                if (tokens[i + 3] && strlen(tokens[i + 3]) > 0) {
                                    // Remove checksum if present
                                    char snrStr[16];
                                    strncpy(snrStr, tokens[i + 3], sizeof(snrStr) - 1);
                                    snrStr[sizeof(snrStr) - 1] = '\0';
                                    char* checksum = strchr(snrStr, '*');
                                    if (checksum) *checksum = '\0';
                                    int snr = atoi(snrStr);
                                    if (snr > s_gnss_max_snr) {
                                        s_gnss_max_snr = snr;
                                    }
                                }
                            }
                        }
                    }

                    line = eol + 1;
                } else {
                    break;
                }
            }
        }

        // Print status every 10 seconds
        uint32_t elapsed_sec = (xTaskGetTickCount() - start_time) * portTICK_PERIOD_MS / 1000;
        if (elapsed_sec > 0 && (elapsed_sec % 10) == 0) {
            static uint32_t last_print = 0;
            if (elapsed_sec != last_print) {
                ESP_LOGI(TAG, "  Waiting for GPS fix... (%lu/%d sec, %d sentences, sats: %d/%d)",
                         elapsed_sec, GNSS_FIX_TIMEOUT_SEC, nmea_sentence_count,
                         s_gnss_sats_used, s_gnss_sats_in_view);
                last_print = elapsed_sec;
            }
        }
    }

gnss_test_done:
    // Copy collected data to results
    results->gnss_satellites_used = s_gnss_sats_used;
    results->gnss_satellites_in_view = s_gnss_sats_in_view;
    results->gnss_fix_type = s_gnss_fix_type;
    results->gnss_hdop = s_gnss_hdop;
    results->gnss_vdop = s_gnss_vdop;
    results->gnss_pdop = s_gnss_pdop;
    results->gnss_ttff_ms = s_gnss_ttff_ms;
    results->gnss_max_snr = s_gnss_max_snr;

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "  GNSS Test Summary:");
    ESP_LOGI(TAG, "    Power:      %s", results->gnss_power_pass ? "PASS" : "FAIL");
    ESP_LOGI(TAG, "    NMEA data:  %s (%d sentences)", results->gnss_nmea_pass ? "PASS" : "FAIL", nmea_sentence_count);
    ESP_LOGI(TAG, "    GPS fix:    %s", results->gnss_fix_pass ? "PASS" : "FAIL (timeout or no satellites)");
    if (results->gnss_fix_pass) {
        ESP_LOGI(TAG, "    Position:   %.6f, %.6f, %.1f m",
                 results->gnss_latitude, results->gnss_longitude, results->gnss_altitude);
    }
    ESP_LOGI(TAG, "    Fix Type:   %d (%s)", results->gnss_fix_type,
             results->gnss_fix_type == 3 ? "3D" : results->gnss_fix_type == 2 ? "2D" : "None");
    ESP_LOGI(TAG, "    Sats Used:  %d", results->gnss_satellites_used);
    ESP_LOGI(TAG, "    Sats View:  %d", results->gnss_satellites_in_view);
    ESP_LOGI(TAG, "    DOP (H/V/P): %.1f / %.1f / %.1f",
             results->gnss_hdop, results->gnss_vdop, results->gnss_pdop);
    ESP_LOGI(TAG, "    TTFF:       %lu ms", results->gnss_ttff_ms);
    ESP_LOGI(TAG, "    Max SNR:    %d dB", results->gnss_max_snr);

    // Don't uninstall UART driver - just leave it
}

//==============================================================================
// Test Charging Status (IO22)
//==============================================================================
bool TestChargingStatus(bool* is_charging)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "TEST 8: Charging Status (IO%d)", PIN_CHG_STATUS);
    ESP_LOGI(TAG, "========================================");

    *is_charging = false;

    // Configure as input (SPV1050 drives this pin)
    esp_err_t err = init_gpio_input(PIN_CHG_STATUS, false, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "  FAIL: Could not configure GPIO%d: %s", PIN_CHG_STATUS, esp_err_to_name(err));
        return false;
    }

    // Read the charging status
    int level = gpio_get_level(PIN_CHG_STATUS);

    // SPV1050 CHARGING output: typically LOW when charging, HIGH when not charging or complete
    *is_charging = (level == 0);

    ESP_LOGI(TAG, "  Charging status pin level: %d", level);
    ESP_LOGI(TAG, "  PASS: Charging status = %s", *is_charging ? "CHARGING" : "NOT CHARGING/COMPLETE");

    return true;
}

//==============================================================================
// Test Power Error Detection (IO18)
//==============================================================================
bool TestPowerError(bool* has_fault)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "TEST 9: Power Error Detection (IO%d)", PIN_SPS_ERROR);
    ESP_LOGI(TAG, "========================================");

    *has_fault = false;

    // Configure as input (U1 FLG pin drives this)
    esp_err_t err = init_gpio_input(PIN_SPS_ERROR, true, false); // External pullup may exist
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "  FAIL: Could not configure GPIO%d: %s", PIN_SPS_ERROR, esp_err_to_name(err));
        return false;
    }

    // Make sure sensor power is enabled for this test
    gpio_set_level(PIN_SPS_ENABLE, 1);
    delay_ms(50);

    // Read the error status
    int level = gpio_get_level(PIN_SPS_ERROR);

    // U1 (TPS22919) FLG pin: typically active LOW for fault, HIGH for normal
    *has_fault = (level == 0);

    ESP_LOGI(TAG, "  Power error pin level: %d", level);
    ESP_LOGI(TAG, "  PASS: Power status = %s", *has_fault ? "FAULT DETECTED" : "NORMAL");

    return true;
}

//==============================================================================
// Test RGB LED with various colors
//==============================================================================
bool TestRgbLed(void)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "TEST 10: RGB LED (IS31FL3193)");
    ESP_LOGI(TAG, "========================================");

    if (!s_i2c_bus_handle) {
        ESP_LOGE(TAG, "  FAIL: I2C bus not initialized");
        return false;
    }

    // Make sure LED driver is enabled (SDB = HIGH)
    gpio_set_level(PIN_LED_STANDBY, 1);
    delay_ms(10);

    // Add IS31FL3193 device
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = IS31FL3193_I2C_ADDR;
    dev_cfg.scl_speed_hz = 400000;

    i2c_master_dev_handle_t led_handle = NULL;
    esp_err_t err = i2c_master_bus_add_device(s_i2c_bus_handle, &dev_cfg, &led_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "  FAIL: Could not add IS31FL3193 device: %s", esp_err_to_name(err));
        return false;
    }

    auto write_reg = [&led_handle](uint8_t reg, uint8_t val) -> bool {
        uint8_t data[2] = {reg, val};
        return i2c_master_transmit(led_handle, data, 2, 100) == ESP_OK;
    };

    ESP_LOGI(TAG, "  Initializing IS31FL3193...");

    // Exit shutdown mode
    if (!write_reg(0x00, 0x20)) { // Shutdown register: normal operation
        ESP_LOGE(TAG, "  FAIL: Could not exit shutdown mode");
        i2c_master_bus_rm_device(led_handle);
        return false;
    }
    delay_ms(5);

    // Set output current (register 0x03)
    write_reg(0x03, 0x50); // ~10mA

    // Set to PWM mode (not breathing)
    write_reg(0x02, 0x00); // LED mode: PWM

    ESP_LOGI(TAG, "  Testing colors...");
    ESP_LOGI(TAG, "  >> VERIFY: RGB LED should cycle through colors <<");

    // Color test sequence
    struct {
        const char* name;
        uint8_t r, g, b;
    } colors[] = {
        {"RED",     0xFF, 0x00, 0x00},
        {"GREEN",   0x00, 0xFF, 0x00},
        {"BLUE",    0x00, 0x00, 0xFF},
        {"YELLOW",  0xFF, 0xFF, 0x00},
        {"CYAN",    0x00, 0xFF, 0xFF},
        {"MAGENTA", 0xFF, 0x00, 0xFF},
        {"WHITE",   0xFF, 0xFF, 0xFF},
        {"OFF",     0x00, 0x00, 0x00},
    };

    for (const auto& color : colors) {
        ESP_LOGI(TAG, "    %s (R=%02X, G=%02X, B=%02X)...", color.name, color.r, color.g, color.b);

        // Write PWM values (registers 0x04, 0x05, 0x06 for channels 0, 1, 2)
        write_reg(0x04, color.r); // PWM CH0 (typically Red)
        write_reg(0x05, color.g); // PWM CH1 (typically Green)
        write_reg(0x06, color.b); // PWM CH2 (typically Blue)

        // Enable channels that have non-zero PWM (register 0x01)
        uint8_t enable = 0;
        if (color.r > 0) enable |= 0x01;
        if (color.g > 0) enable |= 0x02;
        if (color.b > 0) enable |= 0x04;
        write_reg(0x01, enable);

        // Trigger data update (register 0x07)
        write_reg(0x07, 0x00);

        delay_ms(1000);
    }

    ESP_LOGI(TAG, "  PASS: RGB LED test complete (visual verification needed)");

    i2c_master_bus_rm_device(led_handle);
    return true;
}

//==============================================================================
// Run Complete Hardware Test Sequence
//==============================================================================
void RunHardwareTests(HardwareTestResults_t* results)
{
    memset(results, 0, sizeof(HardwareTestResults_t));

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "################################################################");
    ESP_LOGI(TAG, "#                                                              #");
    ESP_LOGI(TAG, "#           LOGI4W PCBA HARDWARE CHECKOUT TEST                 #");
    ESP_LOGI(TAG, "#                                                              #");
    ESP_LOGI(TAG, "################################################################");
    ESP_LOGI(TAG, "");

    // Initialize shared hardware (I2C bus only)
    if (!init_shared_hardware()) {
        ESP_LOGE(TAG, "FATAL: Could not initialize shared hardware!");
        return;
    }

    // === CONTINUOUS I2C PROBE MODE (SCOPE-FRIENDLY) ===
    // Scans ALL 112 valid I2C addresses back-to-back with no delay,
    // creating a long burst of SCL/SDA activity visible on a scope.
    // Each full scan takes ~112 * ~100us = ~11ms of continuous I2C traffic.
    // Then 10ms pause, then repeat.
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  CONTINUOUS I2C PROBE MODE (SCOPE)");
    ESP_LOGI(TAG, "  Full bus scan 0x03-0x77 in tight loop");
    ESP_LOGI(TAG, "  ~11ms burst every 20ms");
    ESP_LOGI(TAG, "  Press reset to stop");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");

    int iteration = 0;

    while (1) {
        iteration++;

        // Scan all 112 valid I2C addresses back-to-back (no delay between probes)
        // This creates a long continuous burst of I2C clock/data activity
        for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
            esp_err_t err = i2c_master_probe(s_i2c_bus_handle, addr, 50);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, ">>> FOUND DEVICE at 0x%02X! (iteration %d) <<<", addr, iteration);
            }
        }

        // Short pause between full scans
        delay_ms(10);

        // Log status every 100 iterations
        if ((iteration % 100) == 0) {
            ESP_LOGI(TAG, "  Scanning... (iteration %d)", iteration);
        }
    }
}

//==============================================================================
// Print Test Summary
//==============================================================================
void PrintTestSummary(const HardwareTestResults_t* results)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "################################################################");
    ESP_LOGI(TAG, "#                    TEST RESULTS SUMMARY                      #");
    ESP_LOGI(TAG, "################################################################");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "+----------------------------------+--------+-------------------+");
    ESP_LOGI(TAG, "| Test                             | Status | Value             |");
    ESP_LOGI(TAG, "+----------------------------------+--------+-------------------+");

    ESP_LOGI(TAG, "| Debug LED (IO21)                 | %s |                   |",
             results->debug_led_pass ? "PASS  " : "FAIL  ");

    ESP_LOGI(TAG, "| LED Standby Control (IO19)       | %s |                   |",
             results->led_standby_pass ? "PASS  " : "FAIL  ");

    ESP_LOGI(TAG, "| I2C Scan - SHT4x (0x44)          | %s |                   |",
             results->i2c_scan_sht4x ? "PASS  " : "FAIL  ");

    ESP_LOGI(TAG, "| I2C Scan - IS31FL3193 (0x68)     | %s |                   |",
             results->i2c_scan_rgb ? "PASS  " : "FAIL  ");

    ESP_LOGI(TAG, "| SHT4x Temperature                | %s | %.2f C           |",
             results->sht4x_temp_pass ? "PASS  " : "FAIL  ", results->sht4x_temp_c);

    ESP_LOGI(TAG, "| SHT4x Humidity                   | %s | %.1f %%           |",
             results->sht4x_humidity_pass ? "PASS  " : "FAIL  ", results->sht4x_humidity_pct);

    ESP_LOGI(TAG, "| I2C Scan - 9705 (0x0C)           | %s |                   |",
             results->i2c_scan_9705 ? "PASS  " : "  --  ");

    ESP_LOGI(TAG, "| Fuel Sensor Init (9705)          | %s |                   |",
             results->fuel_i2c_init_pass ? "PASS  " : "  --  ");

    ESP_LOGI(TAG, "| Fuel Sensor Read (9705)          | %s | %.1f C / %u%%    |",
             results->fuel_i2c_read_pass ? "PASS  " : "  --  ",
             results->fuel_i2c_temp_c, results->fuel_i2c_level_pct);

    ESP_LOGI(TAG, "| ADC Battery Voltage              | %s | %d mV            |",
             results->adc_batt_pass ? "PASS  " : "FAIL  ", results->adc_batt_mv);

    ESP_LOGI(TAG, "| ADC Solar Voltage                | %s | %d mV            |",
             results->adc_solar_pass ? "PASS  " : "FAIL  ", results->adc_solar_mv);

    ESP_LOGI(TAG, "| ADC SPS Voltage                  | %s | %d mV            |",
             results->adc_sps_pass ? "PASS  " : "FAIL  ", results->adc_sps_mv);

    ESP_LOGI(TAG, "| ADC Fuel Voltage                 | %s | %d mV            |",
             results->adc_fuel_pass ? "PASS  " : "FAIL  ", results->adc_fuel_mv);

    ESP_LOGI(TAG, "| GNSS Power (D9 LED)              | %s |                   |",
             results->gnss_power_pass ? "PASS  " : "FAIL  ");

    ESP_LOGI(TAG, "| GNSS NMEA Output                 | %s |                   |",
             results->gnss_nmea_pass ? "PASS  " : "FAIL  ");

    ESP_LOGI(TAG, "| GNSS Fix                         | %s |                   |",
             results->gnss_fix_pass ? "PASS  " : "FAIL  ");

    if (results->gnss_fix_pass) {
        ESP_LOGI(TAG, "|   Latitude                       |       | %.6f          |", results->gnss_latitude);
        ESP_LOGI(TAG, "|   Longitude                      |       | %.6f         |", results->gnss_longitude);
        ESP_LOGI(TAG, "|   Altitude                       |       | %.1f m          |", results->gnss_altitude);
    }
    ESP_LOGI(TAG, "|   Fix Type                       |       | %d (%s)          |",
             results->gnss_fix_type,
             results->gnss_fix_type == 3 ? "3D" : results->gnss_fix_type == 2 ? "2D" : "None");
    ESP_LOGI(TAG, "|   Sats Used                      |       | %d               |", results->gnss_satellites_used);
    ESP_LOGI(TAG, "|   Sats In View                   |       | %d               |", results->gnss_satellites_in_view);
    ESP_LOGI(TAG, "|   HDOP                           |       | %.1f             |", results->gnss_hdop);
    ESP_LOGI(TAG, "|   VDOP                           |       | %.1f             |", results->gnss_vdop);
    ESP_LOGI(TAG, "|   PDOP                           |       | %.1f             |", results->gnss_pdop);
    ESP_LOGI(TAG, "|   TTFF                           |       | %lu ms          |", results->gnss_ttff_ms);
    ESP_LOGI(TAG, "|   Max SNR                        |       | %d dB           |", results->gnss_max_snr);

    ESP_LOGI(TAG, "| Charging Status (IO22)           | %s | %s     |",
             results->chg_status_pass ? "PASS  " : "FAIL  ",
             results->chg_status_charging ? "CHARGING    " : "NOT CHARGING");

    ESP_LOGI(TAG, "| Power Error (IO18)               | %s | %s         |",
             results->power_error_pass ? "PASS  " : "FAIL  ",
             results->power_error_fault ? "FAULT  " : "NORMAL ");

    ESP_LOGI(TAG, "+----------------------------------+--------+-------------------+");

    // Count passes and fails
    int pass_count = 0;
    int fail_count = 0;
    int optional_pass = 0;

    if (results->debug_led_pass) pass_count++; else fail_count++;
    if (results->led_standby_pass) pass_count++; else fail_count++;
    if (results->i2c_scan_sht4x) pass_count++; else fail_count++;
    if (results->i2c_scan_rgb) pass_count++; else fail_count++;
    if (results->sht4x_temp_pass) pass_count++; else fail_count++;
    if (results->sht4x_humidity_pass) pass_count++; else fail_count++;
    if (results->adc_batt_pass) pass_count++; else fail_count++;
    if (results->adc_solar_pass) pass_count++; else fail_count++;
    if (results->adc_sps_pass) pass_count++; else fail_count++;
    if (results->adc_fuel_pass) pass_count++; else fail_count++;
    if (results->gnss_power_pass) pass_count++; else fail_count++;
    if (results->gnss_nmea_pass) pass_count++; else fail_count++;
    // GPS fix is optional (may not have clear sky view)
    if (results->chg_status_pass) pass_count++; else fail_count++;
    if (results->power_error_pass) pass_count++; else fail_count++;
    // Fuel sensor tests are optional (sensor may not be connected)
    if (results->i2c_scan_9705) optional_pass++;
    if (results->fuel_i2c_init_pass) optional_pass++;
    if (results->fuel_i2c_read_pass) optional_pass++;

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "TOTAL: %d PASS, %d FAIL (+ %d/3 optional fuel sensor)",
             pass_count, fail_count, optional_pass);

    if (fail_count == 0) {
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "*** ALL TESTS PASSED ***");
    } else {
        ESP_LOGW(TAG, "");
        ESP_LOGW(TAG, "*** %d TEST(S) FAILED - REVIEW ABOVE ***", fail_count);
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "################################################################");
    ESP_LOGI(TAG, "#                    TEST COMPLETE                             #");
    ESP_LOGI(TAG, "################################################################");
}
