// ----- File: main/hal/Platform.cpp -----
#include "hal/EspPlatform.h" // Use relative path from main source dir
#include "esp_log.h"
#include "soc/usb_serial_jtag_struct.h"  // REQ-PROV-01: USB-Serial-JTAG self-reset disable
#include "driver/usb_serial_jtag.h"      // usb_serial_jtag_is_connected (bench reflash gate)
#include "soc/rtc.h"                     // rtc_clk_slow_src_get (v1.3.0 crystal verification)

namespace Platform
{

    static const char *TAG = "Platform";

    /// <summary>
    /// Initializes core platform services.
    /// </summary>
    /// <returns>true on success, false on failure.</returns>
    bool InitializeSystem()
    {
        ESP_LOGI(TAG, "Initializing core platform services...");

        // Suppress C6 USB-Serial-JTAG self-triggered chip resets. The on-die USB
        // peripheral can spuriously assert a CORE_USB_UART reset (raw HW reason
        // 0x15) during normal operation — known C6 quirk, not host-induced. This
        // bit disables that path. Doesn't help the very first cold boot (bit
        // isn't set yet when that reset fires; the RTC magic-cookie handles that),
        // but prevents subsequent runtime spurious resets. The config_update bit
        // is REQUIRED — without it the disable bit silently has no effect.
        //
        // v1.2.1: the SAME bit also blocks esptool's host-initiated reset into
        // the ROM bootloader, making a running unit un-reflashable (bench-proven
        // 2026-06-12: esptool "Write timeout" against a live app). When a USB
        // HOST is enumerated, keep the path ENABLED: remote reflash works at any
        // time, and a spurious 0x15 reset on a bench unit is harmless (it lands
        // in the boot flash window). Field units (no USB host) keep the quirk
        // suppression - behavior unchanged.
#if CONFIG_LOGI_USB_ALLOWS_CHIP_RESET
        if (usb_serial_jtag_is_connected())
        {
            ESP_LOGI(TAG, "USB host attached - USB-Serial-JTAG chip-reset path stays ENABLED (remote reflash; CONFIG_LOGI_USB_ALLOWS_CHIP_RESET).");
        }
        else
#endif
        {
            USB_SERIAL_JTAG.chip_rst.usb_uart_chip_rst_dis = 1;
            USB_SERIAL_JTAG.config_update.config_update = 1;
            ESP_LOGI(TAG, "USB-Serial-JTAG chip-reset path disabled (C6 quirk workaround).");
        }

        // v1.3.0: report which RTC slow clock actually won. With
        // CONFIG_RTC_CLK_SRC_EXT_CRYS=y, rev-4.1+ boards should report the
        // 32.768 kHz crystal (XTAL32K); a rev-4.0 board (no crystal) falls
        // back to the internal RC and logs a bootloader warning we can't see
        // over USB - this line is the remotely-visible truth.
        {
            soc_rtc_slow_clk_src_t slow_src = rtc_clk_slow_src_get();
            ESP_LOGI(TAG, "RTC slow clock source: %s (%d)",
                     slow_src == SOC_RTC_SLOW_CLK_SRC_XTAL32K ? "external 32.768 kHz crystal"
                     : slow_src == SOC_RTC_SLOW_CLK_SRC_RC_SLOW ? "internal RC (no/failed crystal)"
                                                                : "other",
                     (int)slow_src);
        }

        esp_err_t ret; // Use variable to check return codes

        // 1. Initialize NVS Flash
        // This is required by Wi-Fi driver and for application settings storage.
        ESP_LOGD(TAG, "Initializing NVS flash...");
        ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {
            ESP_LOGW(TAG, "NVS init failed (0x%X), erasing partition and retrying...", ret);
            // ESP_ERROR_CHECK used here because NVS failure is often considered fatal for boot.
            // If more graceful handling is needed, return false instead.
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        // Check the final result after potential erase/retry
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to initialize NVS flash after potential erase (0x%X)", ret);
            return false; // Return failure
        }
        ESP_LOGI(TAG, "NVS flash initialized successfully.");

        // 2. Initialize TCP/IP Stack (esp-netif)
        // Required by network components (Wi-Fi, Ethernet).
        ESP_LOGD(TAG, "Initializing TCP/IP adapter (esp-netif)...");
        ret = esp_netif_init();
        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(ret));
            return false;
        }
        ESP_LOGI(TAG, "TCP/IP adapter initialized successfully.");

        // 3. Create Default System Event Loop
        // Required for handling events from Wi-Fi, TCP/IP, etc.
        ESP_LOGD(TAG, "Creating default event loop...");
        ret = esp_event_loop_create_default();
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
        { // Allow if already created by another component (less likely here)
            ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(ret));
            // If netif init succeeded, maybe try to deinit it here? Or let caller handle partial failure.
            return false;
        }
        ESP_LOGI(TAG, "Default event loop created successfully (or already exists).");

        ESP_LOGI(TAG, "Core platform services initialized successfully.");
        return true; // All essential services initialized
    }

} // namespace Platform