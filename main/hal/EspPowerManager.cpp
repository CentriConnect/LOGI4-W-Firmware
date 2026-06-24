#include "EspPowerManager.h"
#include "esp_log.h"
#include "esp_system.h" // For esp_reset_reason_t and esp_get_reset_reason
#include "freertos/FreeRTOS.h" // For vTaskDelay
#include "driver/usb_serial_jtag.h" // For usb_serial_jtag_is_connected (USB-attached deep-sleep guard)


// Define the static const char* TAG member
const char* EspPowerManager::TAG = "EspPowerManager";

EspPowerManager::EspPowerManager() {
    // REQ-PROV-02: configure PM framework. CONFIG_PM_ENABLE must be y.
    // CPU runs at default (max 160 MHz) when awake; floor to XTAL (40 MHz) when idle.
    // light_sleep_enable=true lets FreeRTOS idle enter light sleep IF no PM lock is held.
    esp_pm_config_t pm_config = {
        .max_freq_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ,
        .min_freq_mhz = 40,
        .light_sleep_enable = true,
    };
    esp_err_t err = esp_pm_configure(&pm_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_pm_configure failed: %s", esp_err_to_name(err));
    }

    // Create the "no light sleep" lock. Default state = ACQUIRED (chip stays awake).
    // Provisioning state machine releases it on start so light sleep can run during
    // the 48h prov window; re-acquires it when leaving prov mode.
    err = esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "no-light-sleep", &_noLightSleepLock);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_pm_lock_create failed: %s", esp_err_to_name(err));
    } else {
        esp_pm_lock_acquire(_noLightSleepLock);
        _lightSleepAllowed = false;
    }

    ESP_LOGI(TAG, "Power Manager Initialized (PM framework + NO_LIGHT_SLEEP lock acquired).");
}

EspPowerManager::~EspPowerManager() {
    if (_noLightSleepLock) {
        if (!_lightSleepAllowed) esp_pm_lock_release(_noLightSleepLock);
        esp_pm_lock_delete(_noLightSleepLock);
        _noLightSleepLock = nullptr;
    }
}

void EspPowerManager::AllowLightSleep(bool allow) {
    if (!_noLightSleepLock) return;
    if (allow == _lightSleepAllowed) return;  // idempotent
    if (allow) {
        esp_pm_lock_release(_noLightSleepLock);
        ESP_LOGD(TAG, "Light sleep ALLOWED (PM lock released)");  // dev-only, silenced at INFO log level
    } else {
        esp_pm_lock_acquire(_noLightSleepLock);
        ESP_LOGD(TAG, "Light sleep BLOCKED (PM lock acquired)");  // dev-only, silenced at INFO log level
    }
    _lightSleepAllowed = allow;
}

void EspPowerManager::Sleep(uint64_t durationUs) {
#if CONFIG_LOGI_USB_BLOCKS_DEEP_SLEEP
    // v1.2.1 bench/test guard: deep sleep powers down the USB-Serial-JTAG
    // controller, making a connected unit unreachable until the ~20 s boot
    // window after its next wake. If a USB HOST is enumerated (a PC - plain
    // 5 V chargers do not enumerate), wait out the sleep duration awake and
    // then restart, which lands in the same fresh-boot path as a deep-sleep
    // wake (reset reason SW instead of DEEPSLEEP - non-POWERON, so NVS
    // credentials are preserved per REQ-PROV-01). Re-checked once a minute so
    // a unit unplugged mid-wait falls through to real deep sleep.
    if (usb_serial_jtag_is_connected()) {
        ESP_LOGW(TAG, "USB host attached - holding awake for %llu ms instead of deep sleep "
                      "(CONFIG_LOGI_USB_BLOCKS_DEEP_SLEEP; field units unaffected).",
                 durationUs / 1000ULL);
        uint64_t remainingUs = durationUs;
        const uint64_t checkIntervalUs = 60ULL * 1000 * 1000;
        while (remainingUs > 0) {
            uint64_t sliceUs = (remainingUs < checkIntervalUs) ? remainingUs : checkIntervalUs;
            vTaskDelay(pdMS_TO_TICKS(sliceUs / 1000ULL));
            remainingUs -= sliceUs;
            if (!usb_serial_jtag_is_connected()) {
                ESP_LOGW(TAG, "USB host detached mid-wait - entering real deep sleep for remaining %llu ms.",
                         remainingUs / 1000ULL);
                break;
            }
        }
        if (remainingUs == 0) {
            ESP_LOGW(TAG, "Awake-wait complete - restarting (mimics deep-sleep wake).");
            Reboot(); // does not return
        }
        durationUs = remainingUs; // USB detached: fall through to real deep sleep
    }
#endif

    ESP_LOGI(TAG, "Configuring deep sleep for %llu microseconds.", durationUs);

    esp_err_t err = esp_sleep_enable_timer_wakeup(durationUs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable timer wakeup (0x%X). Aborting sleep.", err);
        // Consider rebooting or another fallback? For now, just log and return.
        // If we return, the caller needs to handle this failure.
        // In a real scenario, maybe Reboot() is safer?
        return;
    }

    ESP_LOGI(TAG, "Entering deep sleep now...");
    // Ensure logs are flushed before sleeping
    esp_log_level_set("*", ESP_LOG_NONE); // Optional: Reduce boot logs if desired
    vTaskDelay(pdMS_TO_TICKS(10)); // Short delay to allow log flush

    esp_deep_sleep_start(); // This function does not return

    // Code below this line should not be reached
     ESP_LOGE(TAG, "!!! Deep sleep failed to start !!!");
}

void EspPowerManager::Reboot() {
    ESP_LOGW(TAG, "Rebooting device now...");
    // Ensure logs are flushed before rebooting
    vTaskDelay(pdMS_TO_TICKS(10));

    esp_restart(); // This function does not return

    // Code below this line should not be reached
     ESP_LOGE(TAG, "!!! Restart failed !!!");
}

WakeupReason EspPowerManager::GetWakeupReason() {
    // First, check the system reset reason. A deep sleep wake-up is also a reset.
    esp_reset_reason_t resetReason = esp_reset_reason();

    if (resetReason == ESP_RST_DEEPSLEEP) {
        // If it was a deep sleep reset, check the specific wake-up source
        esp_sleep_source_t wakeupSource = esp_sleep_get_wakeup_cause();
        switch (wakeupSource) {
            case ESP_SLEEP_WAKEUP_TIMER:
                ESP_LOGI(TAG, "Wakeup reason: Deep Sleep Timer");
                return WAKEUP_REASON_TIMER;
            case ESP_SLEEP_WAKEUP_EXT0:
            case ESP_SLEEP_WAKEUP_EXT1:
            case ESP_SLEEP_WAKEUP_GPIO: // Grouping GPIO sources
                ESP_LOGI(TAG, "Wakeup reason: Deep Sleep GPIO");
                return WAKEUP_REASON_GPIO;
            // Add cases for other sources if you enable them (ULP, Touch, etc.)
            // case ESP_SLEEP_WAKEUP_ULP: return WAKEUP_REASON_ULP;
            default:
                 ESP_LOGI(TAG, "Wakeup reason: Deep Sleep (Source %d)", wakeupSource);
                 return WAKEUP_REASON_UNKNOWN; // Woke from deep sleep, but source not handled/expected
        }
    } else {
        // If it wasn't a deep sleep reset, report the reset reason
        ESP_LOGI(TAG, "Wakeup/Reset reason: System Reset (%d)", resetReason);
        // You could map esp_reset_reason_t to more WakeupReason values if needed
        return WAKEUP_REASON_RESET; // General reset category
    }
}
