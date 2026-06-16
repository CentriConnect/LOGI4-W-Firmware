#ifndef IBLUETOOTH_MANAGER_H
#define IBLUETOOTH_MANAGER_H

#include <stdbool.h>

/// <summary>
/// Interface for managing Bluetooth Low Energy (BLE) functionality.
/// Defines a platform-independent way to initialize, advertise, and manage BLE.
/// </summary>
class IBluetoothManager
{
public:
    /// <summary>
    /// Virtual destructor.
    /// </summary>
    virtual ~IBluetoothManager() = default;

    /// <summary>
    /// Initializes the Bluetooth stack.
    /// </summary>
    virtual void init() = 0;

    /// <summary>
    /// Starts the Bluetooth host.
    /// </summary>
    virtual void startHost() = 0;

    /// <summary>
    /// Stops the Bluetooth host.
    /// </summary>
    virtual void stopHost() = 0;

    /// <summary>
    /// Deinitializes the Bluetooth stack.
    /// </summary>
    virtual void deinit() = 0;

    /// <summary>
    /// Starts BLE advertising with the specified device name.
    /// </summary>
    /// <param name="name">The device name to advertise.</param>
    /// <returns>true if advertising started successfully, false otherwise.</returns>
    virtual bool startAdvertising(const char* name) = 0;

    /// <summary>
    /// Stops BLE advertising.
    /// </summary>
    virtual void stopAdvertising() = 0;

    /// <summary>
    /// Checks if the Bluetooth stack is initialized.
    /// </summary>
    /// <returns>true if initialized, false otherwise.</returns>
    virtual bool isInitialized() const = 0;

    /// <summary>
    /// Checks if the device is currently advertising.
    /// </summary>
    /// <returns>true if advertising, false otherwise.</returns>
    virtual bool isAdvertising() const = 0;
};

#endif // IBLUETOOTH_MANAGER_H
