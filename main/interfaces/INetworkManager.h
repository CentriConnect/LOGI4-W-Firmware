#ifndef INETWORK_MANAGER_H
#define INETWORK_MANAGER_H

#include <stdbool.h> // For bool type

/// <summary>
/// Interface for managing the device's network connection (e.g., Wi-Fi).
/// Defines a platform-independent way to connect, disconnect, and check connection status.
/// </summary>
class INetworkManager
{
public:
    /// <summary>
    /// Virtual destructor.
    /// </summary>
    virtual ~INetworkManager() = default;

    /// <summary>
    /// Attempts to connect to the network using provided credentials.
    /// This might be a blocking call with a timeout, or it might initiate
    /// a connection and return status immediately, requiring IsConnected()
    /// to be polled or using a callback mechanism (if added later).
    /// Simple blocking with timeout is often suitable for deep sleep devices
    /// that connect infrequently.
    /// </summary>
    /// <param name="ssid">The network SSID (null-terminated C-string).</param>
    /// <param name="password">The network password (null-terminated C-string).</param>
    /// <returns>true if connection (including obtaining an IP address) was successful within a reasonable timeout, false otherwise.</returns>
    virtual bool Connect(const char *ssid, const char *password) = 0;

    /// <summary>
    /// Disconnects from the network.
    /// </summary>
    virtual void Disconnect() = 0;

    /// <summary>
    /// Checks the current connection status.
    /// </summary>
    /// <returns>true if the device is currently connected and has an IP address, false otherwise.</returns>
    virtual bool IsConnected() = 0;
};

#endif // INETWORK_MANAGER_H