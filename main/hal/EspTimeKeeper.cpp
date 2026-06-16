#include "EspTimeKeeper.h"
#include "esp_log.h"
#include <sys/time.h>          // For timeval, time()
#include "freertos/FreeRTOS.h" // For Task utilities if needed
#include "freertos/task.h"     // For vTaskDelay

// Static Member Definitions
const char *EspTimeKeeper::TAG = "EspTimeKeeper";

/// <summary>
/// Constructs a TimeKeeper with the specified NTP server
/// </summary>
/// <param name="ntpServer">The NTP server address to use for time synchronization</param>
EspTimeKeeper::EspTimeKeeper(const char *ntpServer)
    : timeIsSynced(false),
      ntpServer(ntpServer)
{
    ESP_LOGI(TAG, "Time Keeper Initialized. NTP Server: %s", ntpServer);
    // ESP-IDF's C library time functions automatically use the RTC after SNTP sets it
}

/// <summary>
/// Destructor - stops SNTP service if running
/// </summary>
EspTimeKeeper::~EspTimeKeeper()
{
    if (esp_sntp_enabled())
    {
        esp_sntp_stop();
        ESP_LOGI(TAG, "SNTP service stopped.");
    }
}

/// <summary>
/// Synchronizes the system time with an NTP server
/// </summary>
/// <returns>True if synchronization was successful, false otherwise</returns>
bool EspTimeKeeper::SyncTime()
{
    ESP_LOGI(TAG, "Attempting to synchronize time via SNTP...");
    timeIsSynced = false; // Reset sync status flag for this specific attempt

    // Configure SNTP
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, ntpServer);

    // Ensure SNTP service is stopped before init (safer if called multiple times)
    if (esp_sntp_enabled())
    {
        esp_sntp_stop();
    }
    esp_sntp_init();

    // Wait for synchronization or timeout
    int retry = 0;
    const int retryCount = (SNTP_SYNC_TIMEOUT_MS / 200); // Check every 200ms
    sntp_sync_status_t status = SNTP_SYNC_STATUS_RESET;

    while (status == SNTP_SYNC_STATUS_RESET && ++retry <= retryCount)
    {
        status = sntp_get_sync_status();
        if (status == SNTP_SYNC_STATUS_RESET)
        {
            if ((retry % 5) == 0)
            {
                // Log every second or so
                ESP_LOGD(TAG, "Waiting for SNTP sync... Status: RESET (%d/%d)", retry, retryCount);
            }
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }

    // Check result after the loop finishes
    timeIsSynced = (status == SNTP_SYNC_STATUS_COMPLETED);

    if (timeIsSynced)
    {
        time_t now = 0;
        time(&now); // Get the newly set time
        ESP_LOGI(TAG, "Time synchronized successfully via SNTP. Current time: %ld", (long int)now);
    }
    else
    {
        ESP_LOGE(TAG, "Failed to synchronize time via SNTP. Final Status: %d (Timeout: %lu ms)", status, SNTP_SYNC_TIMEOUT_MS);
        timeIsSynced = false;
    }

    // Stop SNTP service regardless of success/failure
    esp_sntp_stop();
    ESP_LOGD(TAG, "SNTP service stopped after sync attempt.");

    return timeIsSynced;
}

/// <summary>
/// Gets the current system time if valid
/// </summary>
/// <returns>The current time, or 0 if the time hasn't been synchronized or is invalid</returns>
time_t EspTimeKeeper::GetCurrentTime()
{
    time_t now;
    time(&now); // Get current system time (should come from RTC if set)

    // Check if the time seems valid (i.e., not near epoch 0)
    if (now < MIN_VALID_EPOCH_TIME)
    {
        ESP_LOGW(TAG, "GetCurrentTime: time_t value (%ld) seems invalid/too old. Returning 0.", (long int)now);
        return 0; // Return 0 if time seems invalid
    }

    return now;
}

/// <summary>
/// Checks if the system time has been successfully synchronized
/// </summary>
/// <returns>True if the time is valid and has been synchronized, false otherwise</returns>
bool EspTimeKeeper::IsTimeSynced()
{
    // Check if the current system time (from RTC) is valid,
    // indicating it has been synced at least once previously.
    time_t now = 0;
    time(&now);
    bool isValid = (now >= MIN_VALID_EPOCH_TIME);

    if (!isValid)
    {
        ESP_LOGD(TAG, "IsTimeSynced: Check failed (time %ld < %lu)", (long int)now, MIN_VALID_EPOCH_TIME);
    }
    else
    {
        ESP_LOGD(TAG, "IsTimeSynced: Check passed (time %ld >= %lu)", (long int)now, MIN_VALID_EPOCH_TIME);
    }

    return isValid;
}