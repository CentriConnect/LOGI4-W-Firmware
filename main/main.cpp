//==============================================================================
// LOGI4W PCBA HARDWARE CHECKOUT TEST FIRMWARE
//
// Purpose: Comprehensive hardware validation firmware for LOGI4W PCBA.
// Tests all peripherals and outputs results via serial console (115200 baud).
//
// IMPORTANT: 20 second flash window at startup allows re-flashing if needed.
// This is CRITICAL since the board has NO BUTTONS.
//
// To select test mode, change the TEST_MODE define below:
//   0 = HARDWARE_TEST - Full hardware checkout (default)
//   1 = BLE_TEST - BLE current measurement
//   2 = ORIGINAL - Normal application firmware
//   3 = GNSS_TEST - Repeated GNSS acquisition test with CSV output
//   4 = FOTA_TEST - AWS IoT Jobs FOTA update test
//==============================================================================

#define TEST_MODE  2  // 0=Hardware Test, 1=BLE Test, 2=Original App, 3=GNSS Test, 4=FOTA Test

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include <stdio.h>

//==============================================================================
// MODE 0: HARDWARE CHECKOUT TEST
//==============================================================================
#if TEST_MODE == 0

#include "hardware_test.h"

static const char *TAG = "HW_CHECKOUT";

extern "C" void app_main(void)
{
    //==========================================================================
    // CRITICAL: Flash window BEFORE any hardware initialization
    // This gives time to re-flash if the firmware has issues.
    // The board has NO BUTTONS, so this is essential!
    //==========================================================================
    printf("\n\n");
    printf("################################################################\n");
    printf("#                                                              #\n");
    printf("#           LOGI4W PCBA HARDWARE CHECKOUT TEST                 #\n");
    printf("#                                                              #\n");
    printf("################################################################\n");
    printf("\n");
    printf(">>> FLASH WINDOW: 20 seconds <<<\n");
    printf(">>> Hold boot pin or reflash NOW if needed <<<\n");
    printf("\n");

    for (int i = 20; i > 0; i--) {
        printf("%d... ", i);
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    printf("\n\n");
    printf("Flash window closed. Starting hardware tests...\n\n");

    // Initialize NVS (required by some ESP-IDF components)
    ESP_LOGI(TAG, "Initializing NVS...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized.");

    // Run hardware tests
    HardwareTestResults_t results;
    RunHardwareTests(&results);

    // Keep running - blink debug LED to show we're alive
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Test complete. Debug LED will blink slowly to indicate ready state.");
    ESP_LOGI(TAG, "Press reset or reflash to run tests again.");

    gpio_config_t io_conf = {};
    io_conf.pin_bit_mask = (1ULL << PIN_DEBUG_LED);
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&io_conf);

    while (1) {
        gpio_set_level(PIN_DEBUG_LED, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(PIN_DEBUG_LED, 0);
        vTaskDelay(pdMS_TO_TICKS(2900));
    }
}

//==============================================================================
// MODE 1: BLE CURRENT MEASUREMENT TEST
//==============================================================================
#elif TEST_MODE == 1

#include "esp_pm.h"

// NimBLE includes
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"

static const char *TAG = "BLE_TEST";

// BLE advertising interval in units of 0.625ms
#define BLE_ADV_INTERVAL_UNITS  160  // 100ms

// GPIO definitions for power control
#define SENSOR_POWER_GPIO   GPIO_NUM_12
#define MEASURE_GPIO        GPIO_NUM_20
#define GNSS_ENABLE_GPIO    GPIO_NUM_11
#define LED_STANDBY_GPIO    GPIO_NUM_19
#define GNSS_RESET_GPIO     GPIO_NUM_10
#define DEBUG_LED_GPIO      GPIO_NUM_21

static uint8_t s_own_addr_type;
static const char *DEVICE_NAME = "LOGI4W-BLE-TEST";

static void ble_advertise(void);

static int ble_gap_event_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status != 0) {
                ble_advertise();
            }
            break;
        case BLE_GAP_EVENT_DISCONNECT:
            ble_advertise();
            break;
        case BLE_GAP_EVENT_ADV_COMPLETE:
            ble_advertise();
            break;
        default:
            break;
    }
    return 0;
}

static void ble_advertise(void)
{
    struct ble_hs_adv_fields fields;
    memset(&fields, 0, sizeof(fields));
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;
    fields.tx_pwr_lvl_is_present = 1;
    fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;

    ble_gap_adv_set_fields(&fields);

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BLE_ADV_INTERVAL_UNITS;
    adv_params.itvl_max = BLE_ADV_INTERVAL_UNITS;

    ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event_cb, NULL);
}

static void ble_on_sync(void)
{
    ble_hs_id_infer_auto(0, &s_own_addr_type);
    ble_svc_gap_device_name_set(DEVICE_NAME);
    ble_advertise();
}

static void ble_on_reset(int reason) {}

static void ble_host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void power_down_peripherals(void)
{
    gpio_config_t io_conf = {};
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;

    // Power down all peripherals
    gpio_num_t outputs[] = {SENSOR_POWER_GPIO, MEASURE_GPIO, GNSS_ENABLE_GPIO,
                            LED_STANDBY_GPIO, GNSS_RESET_GPIO, DEBUG_LED_GPIO};
    for (auto pin : outputs) {
        io_conf.pin_bit_mask = (1ULL << pin);
        gpio_config(&io_conf);
        gpio_set_level(pin, 0);
    }
}

extern "C" void app_main(void)
{
    printf("\n\n=== LOGI4W BLE CURRENT MEASUREMENT TEST ===\n");
    printf("Flash window: 20 seconds...\n");

    for (int i = 20; i > 0; i--) {
        printf("%d... ", i);
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    printf("\nStarting BLE test...\n\n");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    power_down_peripherals();

    // Configure power management
    esp_pm_config_t pm_config = {
        .max_freq_mhz = 160,
        .min_freq_mhz = 10,
        .light_sleep_enable = true
    };
    esp_pm_configure(&pm_config);

    // Initialize BLE
    nimble_port_init();
    ble_hs_cfg.reset_cb = ble_on_reset;
    ble_hs_cfg.sync_cb = ble_on_sync;
    ble_svc_gap_init();
    nimble_port_freertos_init(ble_host_task);

    ESP_LOGI(TAG, "BLE advertising at %.2f ms interval", BLE_ADV_INTERVAL_UNITS * 0.625f);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        ESP_LOGI(TAG, "Still running...");
    }
}

//==============================================================================
// MODE 3: GNSS REPEATED ACQUISITION TEST WITH CSV OUTPUT
//==============================================================================
#elif TEST_MODE == 3

#include "hardware_test.h"
#include "driver/uart.h"
#include <string.h>
#include <math.h>

static const char *TAG = "GNSS_TEST";

//==============================================================================
// GNSS Test Configuration
//==============================================================================
#define GNSS_COLD_RUNS          10      // Number of cold-start runs (PMTK103)
#define GNSS_WARM_RUNS          10      // Number of warm-start runs (power cycle only)
#define GNSS_NUM_RUNS           (GNSS_COLD_RUNS + GNSS_WARM_RUNS)
#define GNSS_FIX_TIMEOUT_SEC    180     // Max wait per run
#define GNSS_SAMPLE_DURATION    60      // Seconds to keep logging after first fix
#define GNSS_POWER_OFF_SEC      5       // Off time between runs

// PMTK cold start: erases almanac, ephemeris, position, time
#define PMTK_COLD_START         "$PMTK103*30\r\n"

//==============================================================================
// NMEA Parsed Data Structures
//==============================================================================
struct GgaData {
    char utc_time[16];
    int fix_quality;
    double latitude;
    double longitude;
    float altitude_m;
    uint8_t sats_used;
    float hdop;
};

struct GsaData {
    uint8_t fix_type;       // 1=no fix, 2=2D, 3=3D
    float pdop;
    float hdop;
    float vdop;
};

struct GsvData {
    uint16_t sats_in_view;  // accumulated across all constellations
    int max_snr_db;
    int snr_sum;
    int snr_count;
    bool epoch_reset;       // reset flag, set by GGA processing
};

struct GstData {
    float rms_err_m;
    float lat_err_m;
    float lon_err_m;
    float alt_err_m;
};

struct RmcData {
    float speed_kts;
    float course_deg;
    char date[8];
};

// Current state for ongoing run
static GgaData s_gga = {};
static GsaData s_gsa = {};
static GsvData s_gsv = {};
static GstData s_gst = {};
static RmcData s_rmc = {};
static uint32_t s_ttff_ms = 0;
static bool s_has_fix = false;

// Per-run summary
struct RunSummary {
    uint32_t ttff_ms;
    bool got_fix;
    bool cold_start;
    double final_lat;
    double final_lon;
    float final_alt_m;
    float avg_hdop;
    float avg_vdop;
    float avg_pdop;
    int max_snr_db;
    uint8_t max_sats_used;
    uint16_t max_sats_in_view;
    int gga_count;
};

static RunSummary s_runs[GNSS_NUM_RUNS];

//==============================================================================
// NMEA Tokenizer — preserves empty fields (unlike strtok)
//==============================================================================

// Splits buf in-place by commas. Empty fields become "" (pointer to '\0').
// Returns number of tokens found.
static int nmea_tokenize(char *buf, char *tokens[], int max_tokens)
{
    int count = 0;
    tokens[count++] = buf;
    while (*buf && count < max_tokens) {
        if (*buf == ',') {
            *buf = '\0';
            tokens[count++] = buf + 1;
        }
        buf++;
    }
    return count;
}

// Strip NMEA checksum from a token (e.g., "1.2*6A" -> "1.2")
static void strip_checksum(char *s)
{
    char *star = strchr(s, '*');
    if (star) *star = '\0';
}

// Parse NMEA latitude/longitude: DDMM.MMMM or DDDMM.MMMM
static double nmea_parse_coord(const char *field, const char *dir, int deg_digits)
{
    if (!field || strlen(field) == 0 || !dir || strlen(dir) == 0) return 0.0;
    char deg_buf[4] = {};
    memcpy(deg_buf, field, deg_digits);
    double deg = atof(deg_buf);
    double min = atof(field + deg_digits);
    double result = deg + (min / 60.0);
    if (dir[0] == 'S' || dir[0] == 'W') result = -result;
    return result;
}

//==============================================================================
// Parse $GxGGA
//==============================================================================
static void parse_gga(char *sentence)
{
    char buf[256];
    strncpy(buf, sentence, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *tokens[20] = {};
    int tc = nmea_tokenize(buf, tokens, 20);

    if (tc > 1 && strlen(tokens[1]) > 0)
        strncpy(s_gga.utc_time, tokens[1], sizeof(s_gga.utc_time) - 1);

    if (tc > 6 && strlen(tokens[6]) > 0)
        s_gga.fix_quality = atoi(tokens[6]);

    if (tc > 3 && strlen(tokens[2]) > 0 && strlen(tokens[3]) > 0)
        s_gga.latitude = nmea_parse_coord(tokens[2], tokens[3], 2);

    if (tc > 5 && strlen(tokens[4]) > 0 && strlen(tokens[5]) > 0)
        s_gga.longitude = nmea_parse_coord(tokens[4], tokens[5], 3);

    if (tc > 7 && strlen(tokens[7]) > 0)
        s_gga.sats_used = (uint8_t)atoi(tokens[7]);

    if (tc > 8 && strlen(tokens[8]) > 0)
        s_gga.hdop = (float)atof(tokens[8]);

    if (tc > 9 && strlen(tokens[9]) > 0)
        s_gga.altitude_m = (float)atof(tokens[9]);

    // Signal GSV to reset sats_in_view accumulator for next epoch
    s_gsv.epoch_reset = true;
}

//==============================================================================
// Parse $GxGSA — fields are at fixed positions even with empty PRN slots
//==============================================================================
static void parse_gsa(char *sentence)
{
    char buf[256];
    strncpy(buf, sentence, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *tokens[20] = {};
    int tc = nmea_tokenize(buf, tokens, 20);

    // Fix type (field 2): 1=no fix, 2=2D, 3=3D
    if (tc > 2 && strlen(tokens[2]) > 0)
        s_gsa.fix_type = (uint8_t)atoi(tokens[2]);

    // PDOP (field 15), HDOP (field 16), VDOP (field 17)
    // With NMEA 4.10+ system ID, these are still at 15/16/17
    if (tc > 15 && strlen(tokens[15]) > 0)
        s_gsa.pdop = (float)atof(tokens[15]);

    if (tc > 16 && strlen(tokens[16]) > 0)
        s_gsa.hdop = (float)atof(tokens[16]);

    if (tc > 17 && strlen(tokens[17]) > 0) {
        char tmp[16];
        strncpy(tmp, tokens[17], sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        strip_checksum(tmp);
        if (strlen(tmp) > 0)
            s_gsa.vdop = (float)atof(tmp);
    }
}

//==============================================================================
// Parse $GxGSV — accumulate sats_in_view across all constellations
//==============================================================================
static void parse_gsv(char *sentence)
{
    char buf[256];
    strncpy(buf, sentence, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *tokens[24] = {};
    int tc = nmea_tokenize(buf, tokens, 24);

    // Message number (field 2)
    int msg_num = (tc > 2) ? atoi(tokens[2]) : 0;

    // Sats in view (field 3) — on msg 1, accumulate across constellation groups
    if (msg_num == 1 && tc > 3 && strlen(tokens[3]) > 0) {
        int count = atoi(tokens[3]);
        if (s_gsv.epoch_reset) {
            // First constellation group in this epoch — reset and set
            s_gsv.sats_in_view = (uint16_t)count;
            s_gsv.epoch_reset = false;
        } else {
            // Additional constellation — add
            s_gsv.sats_in_view += (uint16_t)count;
        }
    }

    // Parse SNR values (field index 7, 11, 15, 19 in standard GSV)
    // Each sat block is 4 fields: PRN, elev, azim, SNR
    // Starting at field 4: sat groups at 4,8,12,16
    for (int base = 4; base + 3 < tc; base += 4) {
        int snr_idx = base + 3;
        if (snr_idx < tc && strlen(tokens[snr_idx]) > 0) {
            char tmp[16];
            strncpy(tmp, tokens[snr_idx], sizeof(tmp) - 1);
            tmp[sizeof(tmp) - 1] = '\0';
            strip_checksum(tmp);
            if (strlen(tmp) > 0) {
                int snr = atoi(tmp);
                if (snr > 0) {
                    if (snr > s_gsv.max_snr_db) s_gsv.max_snr_db = snr;
                    s_gsv.snr_sum += snr;
                    s_gsv.snr_count++;
                }
            }
        }
    }
}

//==============================================================================
// Parse $GxGST
//==============================================================================
static void parse_gst(char *sentence)
{
    char buf[256];
    strncpy(buf, sentence, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *tokens[12] = {};
    int tc = nmea_tokenize(buf, tokens, 12);

    if (tc > 2 && strlen(tokens[2]) > 0)
        s_gst.rms_err_m = (float)atof(tokens[2]);

    if (tc > 6 && strlen(tokens[6]) > 0)
        s_gst.lat_err_m = (float)atof(tokens[6]);

    if (tc > 7 && strlen(tokens[7]) > 0)
        s_gst.lon_err_m = (float)atof(tokens[7]);

    if (tc > 8 && strlen(tokens[8]) > 0) {
        char tmp[16];
        strncpy(tmp, tokens[8], sizeof(tmp) - 1);
        tmp[sizeof(tmp) - 1] = '\0';
        strip_checksum(tmp);
        if (strlen(tmp) > 0)
            s_gst.alt_err_m = (float)atof(tmp);
    }
}

//==============================================================================
// Parse $GxRMC
//==============================================================================
static void parse_rmc(char *sentence)
{
    char buf[256];
    strncpy(buf, sentence, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *tokens[16] = {};
    int tc = nmea_tokenize(buf, tokens, 16);

    if (tc > 7 && strlen(tokens[7]) > 0)
        s_rmc.speed_kts = (float)atof(tokens[7]);

    if (tc > 8 && strlen(tokens[8]) > 0)
        s_rmc.course_deg = (float)atof(tokens[8]);

    if (tc > 9 && strlen(tokens[9]) > 0)
        strncpy(s_rmc.date, tokens[9], sizeof(s_rmc.date) - 1);
}

//==============================================================================
// Process a single NMEA line
//==============================================================================
static void process_nmea_line(char *line, int run, bool cold_start,
                              uint32_t run_start_tick,
                              int *gga_count, float *hdop_sum, float *vdop_sum,
                              float *pdop_sum, int *dop_count)
{
    if (line[0] != '$') return;

    // Identify sentence type (skip talker ID: $GN, $GP, $GL, $GA, $GB)
    const char *type = line + 3;

    if (strncmp(type, "GGA", 3) == 0) {
        parse_gga(line);
        (*gga_count)++;

        // Check for first fix
        if (s_gga.fix_quality > 0 && !s_has_fix) {
            uint32_t now = xTaskGetTickCount();
            s_ttff_ms = (now - run_start_tick) * portTICK_PERIOD_MS;
            s_has_fix = true;
            ESP_LOGI(TAG, "  >> FIX ACQUIRED! TTFF = %lu ms <<", s_ttff_ms);
        }

        // Accumulate DOP for averaging
        if (s_gsa.pdop < 90.0f) {
            *hdop_sum += s_gsa.hdop;
            *vdop_sum += s_gsa.vdop;
            *pdop_sum += s_gsa.pdop;
            (*dop_count)++;
        }

        float avg_snr = (s_gsv.snr_count > 0)
            ? ((float)s_gsv.snr_sum / s_gsv.snr_count) : 0.0f;

        // Output CSV row (with start_type column)
        uint32_t elapsed_ms = (xTaskGetTickCount() - run_start_tick) * portTICK_PERIOD_MS;
        printf("%d,%s,%lu,%s,%d,%d,%.6f,%.6f,%.1f,%d,%d,%.1f,%.1f,%.1f,%d,%.1f,%lu,%.2f,%.2f,%.2f,%.2f,%.1f\n",
            run + 1,
            cold_start ? "cold" : "warm",
            elapsed_ms,
            s_gga.utc_time,
            s_gga.fix_quality,
            s_gsa.fix_type,
            s_gga.latitude,
            s_gga.longitude,
            s_gga.altitude_m,
            s_gga.sats_used,
            s_gsv.sats_in_view,
            s_gga.hdop,
            s_gsa.vdop,
            s_gsa.pdop,
            s_gsv.max_snr_db,
            avg_snr,
            s_has_fix ? s_ttff_ms : 0UL,
            s_gst.rms_err_m,
            s_gst.lat_err_m,
            s_gst.lon_err_m,
            s_gst.alt_err_m,
            s_rmc.speed_kts);
        fflush(stdout);

    } else if (strncmp(type, "GSA", 3) == 0) {
        parse_gsa(line);
    } else if (strncmp(type, "GSV", 3) == 0) {
        parse_gsv(line);
    } else if (strncmp(type, "GST", 3) == 0) {
        parse_gst(line);
    } else if (strncmp(type, "RMC", 3) == 0) {
        parse_rmc(line);
    }
}

//==============================================================================
// GNSS Power Control
//==============================================================================
static void gnss_power_off(void)
{
    gpio_set_level(PIN_GNSS_ENABLE, 0);
    gpio_set_level(PIN_GNSS_RESET, 0);
}

static void gnss_power_on(void)
{
    gpio_set_level(PIN_GNSS_RESET, 0);
    gpio_set_level(PIN_GNSS_ENABLE, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_GNSS_RESET, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
}

// Send PMTK cold start command to erase almanac/ephemeris/position/time
static void gnss_send_cold_start(void)
{
    const char *cmd = PMTK_COLD_START;
    uart_write_bytes(UART_NUM_1, cmd, strlen(cmd));
    ESP_LOGI(TAG, "  Sent PMTK103 (full cold start)");
    vTaskDelay(pdMS_TO_TICKS(100));
}

//==============================================================================
// Reset parsed data for a new run
//==============================================================================
static void reset_nmea_state(void)
{
    memset(&s_gga, 0, sizeof(s_gga));
    memset(&s_gsa, 0, sizeof(s_gsa));
    memset(&s_gsv, 0, sizeof(s_gsv));
    memset(&s_gst, 0, sizeof(s_gst));
    memset(&s_rmc, 0, sizeof(s_rmc));
    s_gsa.fix_type = 1;
    s_gsa.pdop = 99.9f;
    s_gsa.hdop = 99.9f;
    s_gsa.vdop = 99.9f;
    s_gsv.epoch_reset = true;
    s_ttff_ms = 0;
    s_has_fix = false;
}

//==============================================================================
// UART Line Accumulator
//==============================================================================
static char s_line_buf[512];
static int s_line_pos = 0;

static void uart_feed(const uint8_t *data, int len,
                      int run, bool cold_start, uint32_t run_start_tick,
                      int *gga_count, float *hdop_sum, float *vdop_sum,
                      float *pdop_sum, int *dop_count)
{
    for (int i = 0; i < len; i++) {
        char c = (char)data[i];
        if (c == '\n' || c == '\r') {
            if (s_line_pos > 0) {
                s_line_buf[s_line_pos] = '\0';
                process_nmea_line(s_line_buf, run, cold_start, run_start_tick,
                                  gga_count, hdop_sum, vdop_sum, pdop_sum, dop_count);
                s_line_pos = 0;
            }
        } else {
            if (s_line_pos < (int)(sizeof(s_line_buf) - 1)) {
                s_line_buf[s_line_pos++] = c;
            }
        }
    }
}

//==============================================================================
// Run a single GNSS acquisition test
//==============================================================================
static void run_gnss_test(int run, bool cold_start)
{
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "===== RUN %d/%d (%s START) =====",
             run + 1, GNSS_NUM_RUNS, cold_start ? "COLD" : "WARM");

    reset_nmea_state();
    s_line_pos = 0;

    // Flush UART RX buffer
    uart_flush_input(UART_NUM_1);

    // Power on GNSS
    ESP_LOGI(TAG, "  Powering on GNSS...");
    gnss_power_on();

    // For cold start: send PMTK103 to erase all stored data
    if (cold_start) {
        gnss_send_cold_start();
    }

    uint32_t run_start_tick = xTaskGetTickCount();
    uint32_t timeout_ticks = pdMS_TO_TICKS(GNSS_FIX_TIMEOUT_SEC * 1000);
    uint32_t sample_ticks = pdMS_TO_TICKS(GNSS_SAMPLE_DURATION * 1000);
    uint32_t fix_tick = 0;

    int gga_count = 0;
    float hdop_sum = 0.0f, vdop_sum = 0.0f, pdop_sum = 0.0f;
    int dop_count = 0;
    uint32_t last_status_sec = 0;

    while (1) {
        uint32_t now = xTaskGetTickCount();
        uint32_t elapsed_ticks = now - run_start_tick;

        if (s_has_fix) {
            if (fix_tick == 0) fix_tick = now;
            if ((now - fix_tick) >= sample_ticks) {
                ESP_LOGI(TAG, "  Sample duration complete (%d sec after fix).", GNSS_SAMPLE_DURATION);
                break;
            }
        } else {
            if (elapsed_ticks >= timeout_ticks) {
                ESP_LOGW(TAG, "  TIMEOUT: No fix after %d seconds.", GNSS_FIX_TIMEOUT_SEC);
                break;
            }
        }

        uint8_t rx_data[256];
        int len = uart_read_bytes(UART_NUM_1, rx_data, sizeof(rx_data), pdMS_TO_TICKS(200));
        if (len > 0) {
            uart_feed(rx_data, len, run, cold_start, run_start_tick,
                      &gga_count, &hdop_sum, &vdop_sum, &pdop_sum, &dop_count);
        }

        if (!s_has_fix) {
            uint32_t elapsed_sec = (elapsed_ticks * portTICK_PERIOD_MS) / 1000;
            if (elapsed_sec >= 15 && elapsed_sec != last_status_sec && (elapsed_sec % 15) == 0) {
                ESP_LOGI(TAG, "  [%lu/%d sec] sats_used=%d sats_view=%d max_snr=%d fix_type=%d",
                         elapsed_sec, GNSS_FIX_TIMEOUT_SEC,
                         s_gga.sats_used, s_gsv.sats_in_view,
                         s_gsv.max_snr_db, s_gsa.fix_type);
                last_status_sec = elapsed_sec;
            }
        }
    }

    // Record run summary
    RunSummary *rs = &s_runs[run];
    rs->got_fix = s_has_fix;
    rs->cold_start = cold_start;
    rs->ttff_ms = s_has_fix ? s_ttff_ms : 0;
    rs->final_lat = s_gga.latitude;
    rs->final_lon = s_gga.longitude;
    rs->final_alt_m = s_gga.altitude_m;
    rs->avg_hdop = (dop_count > 0) ? (hdop_sum / dop_count) : 99.9f;
    rs->avg_vdop = (dop_count > 0) ? (vdop_sum / dop_count) : 99.9f;
    rs->avg_pdop = (dop_count > 0) ? (pdop_sum / dop_count) : 99.9f;
    rs->max_snr_db = s_gsv.max_snr_db;
    rs->max_sats_used = s_gga.sats_used;
    rs->max_sats_in_view = s_gsv.sats_in_view;
    rs->gga_count = gga_count;

    ESP_LOGI(TAG, "  --- Run %d Summary (%s) ---", run + 1, cold_start ? "COLD" : "WARM");
    ESP_LOGI(TAG, "  Fix: %s | TTFF: %lu ms | GGA sentences: %d",
             rs->got_fix ? "YES" : "NO", rs->ttff_ms, gga_count);
    if (rs->got_fix) {
        ESP_LOGI(TAG, "  Position: %.6f, %.6f, %.1f m",
                 rs->final_lat, rs->final_lon, rs->final_alt_m);
    }
    ESP_LOGI(TAG, "  Avg DOP (H/V/P): %.1f / %.1f / %.1f | Max SNR: %d dB",
             rs->avg_hdop, rs->avg_vdop, rs->avg_pdop, rs->max_snr_db);
    ESP_LOGI(TAG, "  Sats used: %d | Sats in view: %d",
             rs->max_sats_used, rs->max_sats_in_view);

    ESP_LOGI(TAG, "  Powering off GNSS for %d seconds...", GNSS_POWER_OFF_SEC);
    gnss_power_off();
    vTaskDelay(pdMS_TO_TICKS(GNSS_POWER_OFF_SEC * 1000));
}

//==============================================================================
// Print Final Summary Table
//==============================================================================
static void print_final_summary(void)
{
    printf("\n");
    ESP_LOGI(TAG, "################################################################");
    ESP_LOGI(TAG, "#              GNSS TEST FINAL SUMMARY                         #");
    ESP_LOGI(TAG, "################################################################");
    ESP_LOGI(TAG, "");

    printf("# run,start_type,fix,ttff_ms,lat,lon,alt_m,avg_hdop,avg_vdop,avg_pdop,max_snr,sats_used,sats_view,gga_count\n");

    int cold_fix = 0, warm_fix = 0;
    uint32_t cold_ttff_sum = 0, warm_ttff_sum = 0;
    uint32_t cold_ttff_min = UINT32_MAX, warm_ttff_min = UINT32_MAX;
    uint32_t cold_ttff_max = 0, warm_ttff_max = 0;

    for (int i = 0; i < GNSS_NUM_RUNS; i++) {
        RunSummary *rs = &s_runs[i];
        printf("# %d,%s,%s,%lu,%.6f,%.6f,%.1f,%.1f,%.1f,%.1f,%d,%d,%d,%d\n",
               i + 1,
               rs->cold_start ? "cold" : "warm",
               rs->got_fix ? "YES" : "NO",
               rs->ttff_ms,
               rs->final_lat, rs->final_lon, rs->final_alt_m,
               rs->avg_hdop, rs->avg_vdop, rs->avg_pdop,
               rs->max_snr_db, rs->max_sats_used, rs->max_sats_in_view,
               rs->gga_count);

        if (rs->got_fix) {
            if (rs->cold_start) {
                cold_fix++;
                cold_ttff_sum += rs->ttff_ms;
                if (rs->ttff_ms < cold_ttff_min) cold_ttff_min = rs->ttff_ms;
                if (rs->ttff_ms > cold_ttff_max) cold_ttff_max = rs->ttff_ms;
            } else {
                warm_fix++;
                warm_ttff_sum += rs->ttff_ms;
                if (rs->ttff_ms < warm_ttff_min) warm_ttff_min = rs->ttff_ms;
                if (rs->ttff_ms > warm_ttff_max) warm_ttff_max = rs->ttff_ms;
            }
        }
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "COLD START: Fix rate %d/%d (%.0f%%)", cold_fix, GNSS_COLD_RUNS,
             (GNSS_COLD_RUNS > 0) ? (100.0f * cold_fix / GNSS_COLD_RUNS) : 0.0f);
    if (cold_fix > 0)
        ESP_LOGI(TAG, "  TTFF (ms): min=%lu  avg=%lu  max=%lu",
                 cold_ttff_min, cold_ttff_sum / cold_fix, cold_ttff_max);

    ESP_LOGI(TAG, "WARM START: Fix rate %d/%d (%.0f%%)", warm_fix, GNSS_WARM_RUNS,
             (GNSS_WARM_RUNS > 0) ? (100.0f * warm_fix / GNSS_WARM_RUNS) : 0.0f);
    if (warm_fix > 0)
        ESP_LOGI(TAG, "  TTFF (ms): min=%lu  avg=%lu  max=%lu",
                 warm_ttff_min, warm_ttff_sum / warm_fix, warm_ttff_max);

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "################################################################");
    ESP_LOGI(TAG, "#              TEST COMPLETE                                   #");
    ESP_LOGI(TAG, "################################################################");
}

//==============================================================================
// app_main for GNSS Test
//==============================================================================
extern "C" void app_main(void)
{
    //==========================================================================
    // Flash window
    //==========================================================================
    printf("\n\n");
    printf("################################################################\n");
    printf("#                                                              #\n");
    printf("#           LOGI4W GNSS ACQUISITION TEST v2                   #\n");
    printf("#                                                              #\n");
    printf("################################################################\n");
    printf("\n");
    printf(">>> FLASH WINDOW: 20 seconds <<<\n");
    printf(">>> Hold boot pin or reflash NOW if needed <<<\n");
    printf("\n");

    for (int i = 20; i > 0; i--) {
        printf("%d... ", i);
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    printf("\n\n");

    //==========================================================================
    // Configuration summary
    //==========================================================================
    ESP_LOGI(TAG, "GNSS Test Configuration:");
    ESP_LOGI(TAG, "  Cold-start runs:  %d (PMTK103 erase)", GNSS_COLD_RUNS);
    ESP_LOGI(TAG, "  Warm-start runs:  %d (power cycle only)", GNSS_WARM_RUNS);
    ESP_LOGI(TAG, "  Total runs:       %d", GNSS_NUM_RUNS);
    ESP_LOGI(TAG, "  Fix timeout:      %d sec", GNSS_FIX_TIMEOUT_SEC);
    ESP_LOGI(TAG, "  Sample duration:  %d sec (after fix)", GNSS_SAMPLE_DURATION);
    ESP_LOGI(TAG, "  Power off time:   %d sec (between runs)", GNSS_POWER_OFF_SEC);
    ESP_LOGI(TAG, "  GNSS UART:        RX=IO%d TX=IO%d @ %d baud",
             PIN_GNSS_RXD, PIN_GNSS_TXD, GNSS_BAUD_RATE);
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "Phase 1: %d cold-start runs (PMTK103 erases almanac/ephemeris)", GNSS_COLD_RUNS);
    ESP_LOGI(TAG, "Phase 2: %d warm-start runs (power cycle, retained almanac)", GNSS_WARM_RUNS);
    ESP_LOGI(TAG, "");

    //==========================================================================
    // Initialize NVS
    //==========================================================================
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    //==========================================================================
    // Initialize GPIO
    //==========================================================================
    gpio_config_t io_conf = {};
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.intr_type = GPIO_INTR_DISABLE;

    io_conf.pin_bit_mask = (1ULL << PIN_GNSS_ENABLE);
    gpio_config(&io_conf);

    io_conf.pin_bit_mask = (1ULL << PIN_GNSS_RESET);
    gpio_config(&io_conf);

    gnss_power_off();

    //==========================================================================
    // Initialize UART for GNSS
    //==========================================================================
    uart_config_t uart_config = {
        .baud_rate = GNSS_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(UART_NUM_1, 2048, 256, 0, NULL, 0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "FATAL: Could not install UART driver: %s", esp_err_to_name(err));
        while (1) { vTaskDelay(pdMS_TO_TICKS(10000)); }
    }
    uart_param_config(UART_NUM_1, &uart_config);
    uart_set_pin(UART_NUM_1, PIN_GNSS_TXD, PIN_GNSS_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    gpio_set_pull_mode(PIN_GNSS_RXD, GPIO_PULLUP_ONLY);

    //==========================================================================
    // Print CSV header (now includes start_type column)
    //==========================================================================
    printf("run,start_type,timestamp_ms,utc_time,fix_quality,fix_type,lat,lon,alt_m,sats_used,sats_in_view,hdop,vdop,pdop,max_snr_db,avg_snr_db,ttff_ms,rms_err_m,lat_err_m,lon_err_m,alt_err_m,speed_kts\n");
    fflush(stdout);

    //==========================================================================
    // Phase 1: Cold-start runs
    //==========================================================================
    memset(s_runs, 0, sizeof(s_runs));

    ESP_LOGI(TAG, "========== PHASE 1: COLD START RUNS ==========");
    for (int i = 0; i < GNSS_COLD_RUNS; i++) {
        run_gnss_test(i, true);
    }

    //==========================================================================
    // Phase 2: Warm-start runs
    //==========================================================================
    ESP_LOGI(TAG, "========== PHASE 2: WARM START RUNS ==========");
    for (int i = 0; i < GNSS_WARM_RUNS; i++) {
        run_gnss_test(GNSS_COLD_RUNS + i, false);
    }

    //==========================================================================
    // Final summary
    //==========================================================================
    print_final_summary();

    // Idle — blink LED
    gpio_config_t led_conf = {};
    led_conf.pin_bit_mask = (1ULL << PIN_DEBUG_LED);
    led_conf.mode = GPIO_MODE_OUTPUT;
    led_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    led_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    led_conf.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&led_conf);

    while (1) {
        gpio_set_level(PIN_DEBUG_LED, 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        gpio_set_level(PIN_DEBUG_LED, 0);
        vTaskDelay(pdMS_TO_TICKS(2900));
    }
}

//==============================================================================
// MODE 4: FOTA (Firmware Over-The-Air) TEST
//==============================================================================
#elif TEST_MODE == 4

#include "wifiCredentials.h"
#include "hal/EspNetworkManager.h"
#include "hal/EspTimeKeeper.h"
#include "MQTT/AwsIotManager.h"
#include "MQTT/AwsIotConfig.h"
#include "MQTT/DeviceShadowState.h"
#include "esp_ota_ops.h"
#include "esp_wifi.h"

static const char *TAG = "FOTA_TEST";

// How often to poll for jobs (seconds)
#define FOTA_POLL_INTERVAL_SEC  15

// How long to wait after requesting jobs before checking the queue (ms)
#define FOTA_JOB_RESPONSE_WAIT_MS  5000

//--------------------------------------------------------------------------
// Connection-kill test: set to 0 to disable, or 1-99 to disconnect WiFi
// when the download reaches that percentage. Tests error handling / retry.
//--------------------------------------------------------------------------
#define FOTA_KILL_CONNECTION_AT_PCT  50

extern "C" void app_main(void)
{
    //==========================================================================
    // Flash window
    //==========================================================================
    printf("\n\n");
    printf("################################################################\n");
    printf("#                                                              #\n");
    printf("#           LOGI4W FOTA UPDATE TEST                            #\n");
    printf("#                                                              #\n");
    printf("################################################################\n");
    printf("\n");
    printf(">>> FLASH WINDOW: 20 seconds <<<\n");
    printf(">>> Hold boot pin or reflash NOW if needed <<<\n");
    printf("\n");

    for (int i = 20; i > 0; i--) {
        printf("%d... ", i);
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    printf("\n\n");

    //==========================================================================
    // Print partition info
    //==========================================================================
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_app_desc_t *app_desc = esp_app_get_description();
    ESP_LOGI(TAG, "Running partition: %s (offset 0x%lx)", running->label, running->address);
    ESP_LOGI(TAG, "Firmware version: %s", app_desc->version);
    ESP_LOGI(TAG, "Compile date: %s %s", app_desc->date, app_desc->time);

    // Check if we're running newly OTA'd firmware pending verification
    bool ota_pending_verify = false;
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ota_pending_verify = true;
            ESP_LOGW(TAG, ">>> OTA PENDING VERIFICATION - validating new firmware <<<");
        }
    }

    const esp_partition_t *next_update = esp_ota_get_next_update_partition(NULL);
    if (next_update) {
        ESP_LOGI(TAG, "Next OTA partition: %s (offset 0x%lx)", next_update->label, next_update->address);
    }
    ESP_LOGI(TAG, "Thing name: %s", AWS_IOT_THING_NAME);
    ESP_LOGI(TAG, "Endpoint:   %s", AWS_IOT_ENDPOINT);
    ESP_LOGI(TAG, "WiFi SSID:  %s", MY_WIFI_SSID);
    printf("\n");

    //==========================================================================
    // Initialize NVS
    //==========================================================================
    ESP_LOGI(TAG, "Initializing NVS...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erase, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    //==========================================================================
    // Connect WiFi
    //==========================================================================
    ESP_LOGI(TAG, "Initializing WiFi...");
    EspNetworkManager networkMgr;
    if (!networkMgr.Initialize()) {
        ESP_LOGE(TAG, "FATAL: WiFi stack init failed!");
        if (ota_pending_verify) {
            ESP_LOGE(TAG, "OTA validation FAILED — rolling back to previous firmware!");
            esp_ota_mark_app_invalid_rollback_and_reboot(); // never returns
        }
        while (1) { vTaskDelay(pdMS_TO_TICKS(10000)); }
    }

    ESP_LOGI(TAG, "Connecting to WiFi '%s'...", MY_WIFI_SSID);
    if (!networkMgr.Connect(MY_WIFI_SSID, MY_WIFI_PASS)) {
        ESP_LOGE(TAG, "FATAL: WiFi connection failed!");
        if (ota_pending_verify) {
            ESP_LOGE(TAG, "OTA validation FAILED — rolling back to previous firmware!");
            esp_ota_mark_app_invalid_rollback_and_reboot(); // never returns
        }
        while (1) { vTaskDelay(pdMS_TO_TICKS(10000)); }
    }
    ESP_LOGI(TAG, "WiFi connected!");

    //==========================================================================
    // Sync time via SNTP (needed for TLS and job timestamps)
    //==========================================================================
    ESP_LOGI(TAG, "Syncing time via SNTP...");
    EspTimeKeeper timeKeeper;
    if (timeKeeper.SyncTime()) {
        time_t now;
        time(&now);
        struct tm timeinfo;
        localtime_r(&now, &timeinfo);
        char time_str[64];
        strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &timeinfo);
        ESP_LOGI(TAG, "Time synced: %s", time_str);
    } else {
        ESP_LOGW(TAG, "SNTP sync failed — continuing anyway (TLS may fail if clock is off)");
    }

    //==========================================================================
    // Connect to AWS IoT
    //==========================================================================
    ESP_LOGI(TAG, "Initializing AWS IoT...");
    AwsIotManager awsIotMgr;
    if (!awsIotMgr.Initialize()) {
        ESP_LOGE(TAG, "FATAL: AWS IoT init failed!");
        if (ota_pending_verify) {
            ESP_LOGE(TAG, "OTA validation FAILED — rolling back to previous firmware!");
            esp_ota_mark_app_invalid_rollback_and_reboot(); // never returns
        }
        while (1) { vTaskDelay(pdMS_TO_TICKS(10000)); }
    }

    ESP_LOGI(TAG, "Connecting to AWS IoT...");
    if (!awsIotMgr.Connect()) {
        ESP_LOGE(TAG, "FATAL: AWS IoT connection failed!");
        if (ota_pending_verify) {
            ESP_LOGE(TAG, "OTA validation FAILED — rolling back to previous firmware!");
            esp_ota_mark_app_invalid_rollback_and_reboot(); // never returns
        }
        while (1) { vTaskDelay(pdMS_TO_TICKS(10000)); }
    }
    ESP_LOGI(TAG, "AWS IoT connected!");

    // OTA validation passed — firmware can connect to cloud
    if (ota_pending_verify) {
        ESP_LOGI(TAG, ">>> OTA VALIDATION PASSED — marking firmware as valid <<<");
        esp_ota_mark_app_valid_cancel_rollback();
    }

    //==========================================================================
    // Initialize Jobs handler
    //==========================================================================
    ESP_LOGI(TAG, "Initializing Jobs handler...");
    if (!awsIotMgr.InitializeJobsHandler(AWS_IOT_THING_NAME)) {
        ESP_LOGE(TAG, "FATAL: Jobs handler init failed!");
        while (1) { vTaskDelay(pdMS_TO_TICKS(10000)); }
    }
    ESP_LOGI(TAG, "Jobs handler ready!");

    AwsIotJobsHandler *jobsHandler = awsIotMgr.GetJobsHandler();

    //==========================================================================
    // Set FOTA progress callback (with optional connection-kill test)
    //==========================================================================
    static bool wifi_kill_fired = false;
    wifi_kill_fired = false;

    jobsHandler->SetFotaProgressCallback(
        [](size_t downloaded, size_t total) {
            int pct = (total > 0) ? (int)((downloaded * 100) / total) : 0;
            ESP_LOGI("FOTA_TEST", "Download progress: %d%% (%u / %u bytes)",
                     pct, (unsigned)downloaded, (unsigned)total);

#if FOTA_KILL_CONNECTION_AT_PCT > 0
            if (!wifi_kill_fired && pct >= FOTA_KILL_CONNECTION_AT_PCT) {
                wifi_kill_fired = true;
                ESP_LOGW("FOTA_TEST", "");
                ESP_LOGW("FOTA_TEST", "*** SIMULATING CONNECTION LOSS at %d%% ***", pct);
                ESP_LOGW("FOTA_TEST", "");
                esp_wifi_disconnect();
            }
#endif
        }
    );

    //==========================================================================
    // Main loop: poll for FOTA jobs
    //==========================================================================
    printf("\n");
    ESP_LOGI(TAG, "================================================================");
    ESP_LOGI(TAG, "  FOTA test ready. Polling for jobs every %d seconds.", FOTA_POLL_INTERVAL_SEC);
    ESP_LOGI(TAG, "  Create an AWS IoT Job targeting thing: %s", AWS_IOT_THING_NAME);
#if FOTA_KILL_CONNECTION_AT_PCT > 0
    ESP_LOGW(TAG, "  CONNECTION KILL TEST ACTIVE: WiFi will disconnect at %d%%", FOTA_KILL_CONNECTION_AT_PCT);
#endif
    ESP_LOGI(TAG, "================================================================");
    printf("\n");

    int poll_count = 0;

    while (1) {
        poll_count++;
        ESP_LOGI(TAG, "--- Poll #%d: Checking for FOTA jobs ---", poll_count);

        // Publish start-next to request the next pending job with full document
        AwsIotJob job;
        jobsHandler->GetNextJob(job);  // publishes request; may not have response yet

        // Wait for async MQTT response to arrive and populate pending_jobs
        ESP_LOGI(TAG, "Waiting %d ms for job response...", FOTA_JOB_RESPONSE_WAIT_MS);
        vTaskDelay(pdMS_TO_TICKS(FOTA_JOB_RESPONSE_WAIT_MS));

        // Now check if a job arrived
        if (!jobsHandler->IsFotaUpdateAvailable() || !jobsHandler->GetNextJob(job)) {
            ESP_LOGI(TAG, "No pending FOTA jobs. Next poll in %d seconds.", FOTA_POLL_INTERVAL_SEC);
            vTaskDelay(pdMS_TO_TICKS(FOTA_POLL_INTERVAL_SEC * 1000));
            continue;
        }

        // We have a job!
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "############################################################");
        ESP_LOGI(TAG, "  FOTA JOB FOUND!");
        ESP_LOGI(TAG, "  Job ID:    %s", job.job_id.c_str());
        ESP_LOGI(TAG, "  Version:   %s", job.firmware_version.c_str());
        ESP_LOGI(TAG, "  URL:       %s", job.firmware_url.c_str());
        ESP_LOGI(TAG, "  MD5:       %s", job.firmware_md5.c_str());
        ESP_LOGI(TAG, "  Size:      %u bytes", (unsigned)job.firmware_size);
        ESP_LOGI(TAG, "  Force:     %s", job.force_update ? "YES" : "NO");
        ESP_LOGI(TAG, "############################################################");
        ESP_LOGI(TAG, "");

        ESP_LOGI(TAG, "Processing FOTA job...");

#if FOTA_KILL_CONNECTION_AT_PCT > 0
        // Reset kill flag so the simulated disconnect fires on first attempt
        wifi_kill_fired = false;
#endif

        // ProcessFotaJob handles: status IN_PROGRESS → download → MD5 verify → status SUCCEEDED → reboot
        if (jobsHandler->ProcessFotaJob(job)) {
            // We should never reach here — ProcessFotaJob calls esp_restart() on success
            ESP_LOGI(TAG, "FOTA completed — rebooting...");
            esp_restart();
        } else {
            ESP_LOGE(TAG, "FOTA FAILED! Job status set to FAILED on cloud.");
#if FOTA_KILL_CONNECTION_AT_PCT > 0
            // Reconnect WiFi after simulated disconnect so next poll cycle works
            ESP_LOGW(TAG, "Reconnecting WiFi after connection-kill test...");
            if (!networkMgr.Connect(MY_WIFI_SSID, MY_WIFI_PASS)) {
                ESP_LOGE(TAG, "WiFi reconnect failed — rebooting to recover");
                esp_restart();
            }
            ESP_LOGI(TAG, "WiFi reconnected! Will retry on next poll cycle.");
#else
            ESP_LOGE(TAG, "Will retry on next poll cycle.");
#endif
        }

        vTaskDelay(pdMS_TO_TICKS(FOTA_POLL_INTERVAL_SEC * 1000));
    }
}

//==============================================================================
// MODE 2: ORIGINAL APPLICATION FIRMWARE
//==============================================================================
#elif TEST_MODE == 2

#include "hal/EspPlatform.h"
#include "StateMachines/ApplicationStateMachine.h"

static const char *TAG = "AppMain";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "--- Device Starting ---");

    // Flash window for safety. 3 s: with light sleep disabled in provisioning
    // (V13-013) the device stays awake and remains reflashable for the whole
    // provisioning window, so the long boot countdown is redundant and only
    // delayed the blue LED. Restore a longer window if/when light sleep returns.
    printf("\n\n=== LOGI4W Application Firmware ===\n");
    printf("Flash window: 3 seconds...\n");
    for (int i = 3; i > 0; i--) {
        printf("%d... ", i);
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    printf("\nStarting application...\n\n");

    if (!Platform::InitializeSystem()) {
        ESP_LOGE(TAG, "Core Platform Initialization Failed! Halting.");
        while (1) { vTaskDelay(pdMS_TO_TICKS(10000)); }
    }
    ESP_LOGI(TAG, "Core Platform Services Initialized.");

    ApplicationStateMachine app;

    if (!app.init()) {
        ESP_LOGE(TAG, "Application failed to initialize! Halting.");
        while (1) { vTaskDelay(pdMS_TO_TICKS(10000)); }
    }

    while (1) {
        app.update();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

#endif // TEST_MODE
