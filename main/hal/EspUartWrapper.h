// ----- EspUartWrapper.h -----
#ifndef ESPUARTWRAPPER_H
#define ESPUARTWRAPPER_H

#include <cstdint>
#include "driver/uart.h"
#include "freertos/queue.h"
#include "esp_event.h"
#include "driver/gpio.h"

class EspUartWrapper {
public:
    static constexpr const char* TAG = "EspUartWrapper";

    EspUartWrapper(uart_port_t uart_num = UART_NUM_1,
                   gpio_num_t rx_io_num = GPIO_NUM_NC,
                   gpio_num_t tx_io_num = GPIO_NUM_NC,
                   esp_event_loop_handle_t event_loop_handle = nullptr);

    ~EspUartWrapper();

    esp_err_t init(int baud_rate = 115200);
    esp_err_t write(const uint8_t* data, size_t size);
    int read(uint8_t* data, size_t max_size, uint32_t timeout_ms);
    esp_err_t writeBreak();

    // --- NEW: Getter for the event loop handle ---
    esp_event_loop_handle_t getEventLoopHandle() const { return event_loop_handle_; }

    static esp_event_base_t UART_EVENT_BASE;
    enum uart_event_id_t {
        UART_DATA_RECEIVED,
        UART_BREAK_RECEIVED,
        UART_ERROR
    };

    struct UartEventData {
        size_t length;
        uint8_t data[1024];
    };

private:
    static void uart_event_task(void* pvParameters);
    uint32_t firstBaudrate;
    uint32_t breakBaudrate;
    uart_port_t uart_num_;
    gpio_num_t rx_io_num_;
    gpio_num_t tx_io_num_;
    QueueHandle_t uart_queue_;
    TaskHandle_t uart_task_handle_;
    esp_event_loop_handle_t event_loop_handle_;
};

#endif // ESPUARTWRAPPER_H