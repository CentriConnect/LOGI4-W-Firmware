#ifndef IBOARDUTILS_H
#define IBOARDUTILS_H

#include <stdbool.h> // For bool type

/// <summary>
/// Interface for basic board utility functions (like LEDs, buttons).
/// Defines a contract for simple hardware interactions common across boards.
/// </summary>
class IBoardUtils {
public:
    /// <summary>
    /// Virtual destructor
    /// </summary>
    virtual ~IBoardUtils() = default;

    /// <summary>
    /// Sets the state of a status LED (if available).
    /// </summary>
    /// <param name="on">true to turn the LED on, false to turn it off.</param>
    virtual void SetDebugLed(bool on) = 0;
};

#endif // IBOARDUTILS_H