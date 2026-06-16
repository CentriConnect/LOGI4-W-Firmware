// ----- File: main/hal/Platform.h -----
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdbool.h>
// Include headers needed for functions called within InitializeSystem
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"

/// <summary>
/// Provides centralized initialization for essential platform services required by HAL components.
/// </summary>
namespace Platform
{
    /// <summary>
    /// Initializes core platform services like NVS flash, TCP/IP adapter (netif),
    /// and the default system event loop. This should be called once, early in application startup.
    /// </summary>
    /// <returns>true if all initializations were successful, false otherwise.</returns>
    bool InitializeSystem();

} // namespace Platform

#endif // PLATFORM_H