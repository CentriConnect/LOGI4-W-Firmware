#ifndef ESP_PWR_MANAGER_H
#define ESP_PWR_MANAGER_H

#include "interfaces/IPowerManager.h"
#include "esp_sleep.h" // Required for esp_sleep types and functions
#include "esp_pm.h"    // REQ-PROV-02: PM lock for light-sleep gating

/// <summary>
/// Concrete implementation of IPowerManager using ESP-IDF sleep functions for ESP32-C6.
/// </summary>
class EspPowerManager : public IPowerManager {
    // Private tag for logging
    static const char* TAG;

public:
    /// <summary>
    /// Default constructor.
    /// </summary>
    EspPowerManager();

    /// <summary>
    /// Default destructor.
    /// </summary>
    virtual ~EspPowerManager();

    // --- Delete copy constructor and assignment operator ---
    EspPowerManager(const EspPowerManager&) = delete;
    EspPowerManager& operator=(const EspPowerManager&) = delete;

    /// <summary>
    /// Puts the device into deep sleep using esp_deep_sleep_start.
    /// Configures wake-up via timer before sleeping.
    /// </summary>
    /// <param name="durationUs">Duration to sleep in microseconds.</param>
    void Sleep(uint64_t durationUs) override;

    /// <summary>
    /// Reboots the device using esp_restart.
    /// </summary>
    void Reboot() override;

    /// <summary>
    /// Determines the wake-up reason using esp_sleep_get_wakeup_cause.
    /// Also checks reset reason to differentiate sleep wake-up from resets.
    /// </summary>
    /// <returns>The WakeupReason enum value.</returns>
    WakeupReason GetWakeupReason() override;

    /// REQ-PROV-02: gate auto light sleep via PM lock.
    /// allow=true  -> release the NO_LIGHT_SLEEP lock; FreeRTOS idle can enter light sleep.
    /// allow=false -> acquire the lock; chip stays awake (normal duty cycle).
    void AllowLightSleep(bool allow);

private:
    esp_pm_lock_handle_t _noLightSleepLock = nullptr;
    bool _lightSleepAllowed = false;  // tracks state to make acquire/release idempotent
};

#endif // ESP_PWR_MANAGER_H
