#ifndef ITIME_KEEPER_H
#define ITIME_KEEPER_H

#include <stdbool.h>
#include <time.h> // For time_t standard type

/// <summary>
/// Interface for synchronizing and retrieving system time (wall clock time).
/// </summary>
class ITimeKeeper
{
public:
    /// <summary>
    /// Virtual destructor.
    /// </summary>
    virtual ~ITimeKeeper() = default;

    /// <summary>
    /// Attempts to synchronize the system's real-time clock with an external
    /// time source (e.g., an NTP server). Requires network connectivity.
    /// This is expected to be a blocking call with a timeout.
    /// </summary>
    /// <returns>true if time synchronization was successful within a reasonable timeout, false otherwise.</returns>
    virtual bool SyncTime() = 0;

    /// <summary>
    /// Gets the current system time as seconds since the Epoch (Unix Time).
    /// </summary>
    /// <returns>The current time_t value. Returns 0 if time has not been successfully
    /// synchronized during this wake cycle or an error occurs.</returns>
    virtual time_t GetCurrentTime() = 0;

    /// <summary>
    /// Checks if the system time has been successfully synchronized during this
    /// wake cycle.
    /// </summary>
    /// <returns>true if time has been synchronized successfully at least once, false otherwise.</returns>
    virtual bool IsTimeSynced() = 0;
};

#endif // ITIME_KEEPER_H