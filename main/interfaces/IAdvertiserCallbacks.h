#ifndef I_ADVERTISER_CALLBACKS_H
#define I_ADVERTISER_CALLBACKS_H

/// <summary>
/// Interface for receiving callbacks about advertising events from BleAdvertiser.
/// Implemented by the application layer to control advertising flow.
/// </summary>
class IAdvertiserCallbacks
{
public:
    virtual ~IAdvertiserCallbacks() = default;

    /// <summary>
    /// Called when advertising stops for any reason other than a direct Stop() call
    /// (e.g., duration expired, connection established, connection failed).
    /// </summary>
    /// <param name="reason">The reason advertising stopped (NimBLE host error code, 0 for connection success).</param>
    virtual void OnAdvertisingStopped(int reason) = 0;

    // Add other callbacks if needed (e.g., OnAdvertisingStarted)
};

#endif // I_ADVERTISER_CALLBACKS_H