#ifndef IPWR_MANAGER_H
#define IPWR_MANAGER_H

#include <stdint.h> // For uint64_t

/// <summary>
/// Enumeration defining the reasons why the device woke up from sleep.
/// </summary>
typedef enum {
    WAKEUP_REASON_UNKNOWN = 0, // Default or unknown reason
    WAKEUP_REASON_TIMER,       // Woke up due to the RTC timer
    WAKEUP_REASON_GPIO,        // Woke up due to an external GPIO interrupt (EXT0/EXT1/GPIO)
    WAKEUP_REASON_RESET,       // Device was reset (Power-on reset, Brownout, etc.) - useful context
    // Add other specific reasons if needed (e.g., ULP, Touchpad, UART)
} WakeupReason;

/// <summary>
/// Interface for controlling device power states (sleep, reboot) and querying wake-up reasons.
/// This interface abstracts the power management functionality, allowing for platform-independent code.
/// </summary>
class IPowerManager {
public:
    /// <summary>
    /// Virtual destructor.
    /// </summary>
    virtual ~IPowerManager() = default;

    /// <summary>
    /// Puts the device into its lowest power deep sleep mode for a specified duration.
    /// This function typically does not return; the device execution restarts from the beginning on wake-up.
    /// </summary>
    /// <param name="durationUs">The duration to sleep in microseconds.</param>
    virtual void Sleep(uint64_t durationUs) = 0;

    /// <summary>
    /// Reboots the device immediately.
    /// This function typically does not return.
    /// </summary>
    virtual void Reboot() = 0;

    /// <summary>
    /// Gets the reason why the device woke up from a previous sleep cycle or was reset.
    /// </summary>
    /// <returns>The WakeupReason enum value.</returns>
    virtual WakeupReason GetWakeupReason() = 0;
};

#endif // IPWR_MANAGER_H