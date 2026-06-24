#include "EspUartWrapper.h"
#include "esp_log.h"
#include <cstring>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

esp_event_base_t EspUartWrapper::UART_EVENT_BASE = "UART_EVENT";

EspUartWrapper::EspUartWrapper(uart_port_t uart_num, gpio_num_t rx_io_num, gpio_num_t tx_io_num, esp_event_loop_handle_t event_loop_handle)
    : uart_num_(uart_num),
      rx_io_num_(rx_io_num),
      tx_io_num_(tx_io_num),
      uart_queue_(nullptr),
      uart_task_handle_(nullptr),
      event_loop_handle_(event_loop_handle) {
}

EspUartWrapper::~EspUartWrapper() {
    if (uart_task_handle_) {
        vTaskDelete(uart_task_handle_);
    }
    uart_driver_delete(uart_num_);
}

esp_err_t EspUartWrapper::init(int baud_rate) {
    uart_config_t uart_config = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    firstBaudrate = baud_rate;
    breakBaudrate = ((firstBaudrate * 9)/13);

    // Install UART driver - simple configuration like your working code
    ESP_ERROR_CHECK(uart_driver_install(uart_num_, 2048, 0, 10, &uart_queue_, 0));
    ESP_ERROR_CHECK(uart_param_config(uart_num_, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uart_num_, tx_io_num_, rx_io_num_, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // Enable pull-up on RX pin to prevent floating pin issues
    if (rx_io_num_ != GPIO_NUM_NC) {
        gpio_set_pull_mode(rx_io_num_, GPIO_PULLUP_ONLY);
        ESP_LOGI(TAG, "Enabled pull-up on RX pin GPIO%d", rx_io_num_);
    }

    // DO NOT enable pattern detection - it's not needed for GPS
    // Pattern detection causes "pattern queue is full" errors with continuous GPS data

    // Create a task to handle UART events
    xTaskCreate(uart_event_task, "uart_event_task", 4096, this, 12, &uart_task_handle_);
    
    ESP_LOGI(TAG, "UART initialized - Port: %d, Baud: %d, RX: %d, TX: %d", 
             uart_num_, baud_rate, rx_io_num_, tx_io_num_);
    
    return ESP_OK;
}

esp_err_t EspUartWrapper::writeBreak() {
    uint8_t zero = 0;
    uart_set_baudrate(uart_num_, breakBaudrate);
    (void)uart_write_bytes(uart_num_, (char*)&zero, 1);
    esp_err_t err = uart_set_baudrate(uart_num_, firstBaudrate);
    return err;
}

esp_err_t EspUartWrapper::write(const uint8_t* data, size_t size) {
    int bytes_written = uart_write_bytes(uart_num_, reinterpret_cast<const char*>(data), size);
    if (bytes_written < 0) {
        ESP_LOGE(TAG, "uart_write_bytes failed");
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void parse_nmea_and_print_lat_lon(const char* nmea_data) {
    const char* line = nmea_data;
    while (*line) {
        const char* next_line = strchr(line, '\n');
        size_t len = next_line ? (size_t)(next_line - line) : strlen(line);

        char buffer[128] = {0};
        strncpy(buffer, line, len);
        buffer[len] = '\0';

        if (strncmp(buffer, "$GNGGA", 6) == 0 || strncmp(buffer, "$GNGLL", 6) == 0 || strncmp(buffer, "$GNRMC", 6) == 0) {
            char* tokens[20] = {nullptr};
            int token_count = 0;
            char* token = strtok(buffer, ",");
            while (token && token_count < 20) {
                tokens[token_count++] = token;
                token = strtok(nullptr, ",");
            }

            const char* lat_str = nullptr;
            const char* lat_dir = nullptr;
            const char* lon_str = nullptr;
            const char* lon_dir = nullptr;

            if (strncmp(tokens[0], "$GNGGA", 6) == 0 && token_count >= 6) {
                lat_str = tokens[2];
                lat_dir = tokens[3];
                lon_str = tokens[4];
                lon_dir = tokens[5];
            } else if (strncmp(tokens[0], "$GNGLL", 6) == 0 && token_count >= 5) {
                lat_str = tokens[1];
                lat_dir = tokens[2];
                lon_str = tokens[3];
                lon_dir = tokens[4];
            } else if (strncmp(tokens[0], "$GNRMC", 6) == 0 && token_count >= 7) {
                lat_str = tokens[3];
                lat_dir = tokens[4];
                lon_str = tokens[5];
                lon_dir = tokens[6];
            }

            if (lat_str && lon_str && lat_dir && lon_dir) {
                double lat_deg = 0.0;
                double lon_deg = 0.0;

                // Convert latitude
                if (strlen(lat_str) >= 4) {
                    char deg_buf[3] = {lat_str[0], lat_str[1], '\0'};
                    double deg = atof(deg_buf);
                    double min = atof(lat_str + 2);
                    lat_deg = deg + (min / 60.0);
                    if (*lat_dir == 'S') lat_deg = -lat_deg;
                }

                // Convert longitude
                if (strlen(lon_str) >= 5) {
                    char deg_buf[4] = {lon_str[0], lon_str[1], lon_str[2], '\0'};
                    double deg = atof(deg_buf);
                    double min = atof(lon_str + 3);
                    lon_deg = deg + (min / 60.0);
                    if (*lon_dir == 'W') lon_deg = -lon_deg;
                }

                ESP_LOGD("EspUartWrapper", "Latitude: %.6f, Longitude: %.6f", lat_deg, lon_deg);
            }
        }

        line = next_line ? (next_line + 1) : nullptr;
        if (!line) break;
    }
}

void EspUartWrapper::uart_event_task(void* pvParameters) {
    EspUartWrapper* self = static_cast<EspUartWrapper*>(pvParameters);
    uart_event_t event;
    UartEventData event_data;
    static uint32_t total_bytes_received = 0;
    static uint32_t event_count = 0;

    ESP_LOGI(TAG, "UART event task started");

    while (true) {
        if (xQueueReceive(self->uart_queue_, (void*)&event, pdMS_TO_TICKS(5000))) {
            event_count++;
            // ESP_LOGI(TAG, "UART event #%lu, type: %d", event_count, event.type);
            switch (event.type) {
                case UART_DATA: {
                    size_t len = event.size;
                    if (len > 0 && len <= sizeof(event_data.data)) {
                        int read_len = uart_read_bytes(self->uart_num_, event_data.data, len, portMAX_DELAY);
                        if (read_len > 0) {
                            total_bytes_received += read_len;
                            event_data.length = read_len;
                            event_data.data[read_len] = '\0';  // Null terminate

                            // ESP_LOGI(TAG, "Read %d bytes (total: %lu)", read_len, total_bytes_received);
                            
                            // Print the actual data content if it looks like NMEA
                            if (event_data.data[0] == '$') {
                                // Print up to 80 characters of the NMEA sentence
                                //ESP_LOGI(TAG, "GPS NMEA: %.80s%s", event_data.data, read_len > 80 ? "..." : "");
                            }
                            
                            // Also show hex dump for debugging
                            //ESP_LOG_BUFFER_HEXDUMP(TAG, event_data.data, read_len > 32 ? 32 : read_len, ESP_LOG_DEBUG);

                            parse_nmea_and_print_lat_lon((const char*)event_data.data);

                            // Post event
                            esp_err_t err;
                            if (self->event_loop_handle_ != nullptr) {
                                err = esp_event_post_to(
                                    self->event_loop_handle_,
                                    UART_EVENT_BASE,
                                    UART_DATA_RECEIVED,
                                    &event_data,
                                    sizeof(UartEventData),
                                    0  // Don't block
                                );
                            } else {
                                err = esp_event_post(
                                    UART_EVENT_BASE,
                                    UART_DATA_RECEIVED,
                                    &event_data,
                                    sizeof(UartEventData),
                                    0  // Don't block
                                );
                            }
                            
                            if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
                                ESP_LOGE(TAG, "Failed to post event: %s", esp_err_to_name(err));
                            }
                        }
                    }
                    break;
                }
                
                case UART_BREAK: {
                    // Should be rare now with pull-up enabled
                    ESP_LOGD(TAG, "UART_BREAK event");
                    uart_flush_input(self->uart_num_);
                    break;
                }
                
                case UART_FIFO_OVF: {
                    ESP_LOGW(TAG, "UART FIFO Overflow");
                    uart_flush_input(self->uart_num_);
                    xQueueReset(self->uart_queue_);
                    
                    esp_err_t err;
                    if (self->event_loop_handle_ != nullptr) {
                        err = esp_event_post_to(
                            self->event_loop_handle_,
                            UART_EVENT_BASE,
                            UART_ERROR,
                            nullptr,
                            0,
                            0
                        );
                    } else {
                        err = esp_event_post(
                            UART_EVENT_BASE,
                            UART_ERROR,
                            nullptr,
                            0,
                            0
                        );
                    }
                    break;
                }
                
                default:
                    ESP_LOGD(TAG, "Unhandled UART event: %d", event.type);
                    break;
            }
        } else {
            // Queue receive timed out - no UART events for 5 seconds
            ESP_LOGD(TAG, "No UART data received in last 5 seconds (total bytes so far: %lu)", total_bytes_received);
        }
    }
    vTaskDelete(NULL);
}

// Add this method for direct reading if needed
int EspUartWrapper::read(uint8_t* data, size_t max_size, uint32_t timeout_ms) {
    return uart_read_bytes(uart_num_, data, max_size, timeout_ms / portTICK_PERIOD_MS);
}
