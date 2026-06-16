#ifndef ESP_TIME_KEEPER_H
#define ESP_TIME_KEEPER_H

#include "interfaces/ITimeKeeper.h"
#include "esp_sntp.h"

/// <summary>
/// ESP32 implementation of the ITimeKeeper interface
/// </summary>
class EspTimeKeeper : public ITimeKeeper
{
public:
    /// <summary>
    /// Constructs a TimeKeeper with the specified NTP server
    /// </summary>
    /// <param name="ntpServer">The NTP server address to use for time synchronization</param>
    EspTimeKeeper(const char *ntpServer = "us.pool.ntp.org");

    /// <summary>
    /// Destructor - stops SNTP service if running
    /// </summary>
    virtual ~EspTimeKeeper();

    /// <summary>
    /// Synchronizes the system time with an NTP server
    /// </summary>
    /// <returns>True if synchronization was successful, false otherwise</returns>
    bool SyncTime() override;

    /// <summary>
    /// Gets the current system time if valid
    /// </summary>
    /// <returns>The current time, or 0 if the time hasn't been synchronized or is invalid</returns>
    time_t GetCurrentTime() override;

    /// <summary>
    /// Checks if the system time has been successfully synchronized
    /// </summary>
    /// <returns>True if the time is valid and has been synchronized, false otherwise</returns>
    bool IsTimeSynced() override;

private:
    bool timeIsSynced;      // Tracks sync success this wake cycle
    const char *ntpServer;  // NTP server hostname
    static const char *TAG; // Logging tag

    // Constants
    static const uint32_t SNTP_SYNC_TIMEOUT_MS = 10000;      // 10 second timeout for SNTP sync
    static const uint32_t MIN_VALID_EPOCH_TIME = 1577836800; // 2020-01-01 00:00:00 UTC
};

#endif // ESP_TIME_KEEPER_H