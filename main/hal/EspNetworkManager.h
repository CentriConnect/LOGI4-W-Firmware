#ifndef ESP_NETWORK_MANAGER_H
#define ESP_NETWORK_MANAGER_H

#include "interfaces/INetworkManager.h"
#include "StateMachines/StatesCommon.h"  // For WifiFailureType

// ESP-IDF Headers
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"

// FreeRTOS Headers
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

// Configuration Defaults
#define WIFI_MAXIMUM_RETRY CONFIG_ESP_MAXIMUM_STA_CONN_RETRY // Use Kconfig value or define explicitly (e.g., 5)
#define WIFI_CONNECT_TIMEOUT_MS 15000                        // Timeout for connection attempt (milliseconds)

/// <summary>
/// Concrete implementation of INetworkManager using ESP-IDF Wi-Fi STA mode.
/// Handles asynchronous connection events internally to provide a blocking Connect method.
/// </summary>
class EspNetworkManager : public INetworkManager
{

    // --- Private Members ---
    static const char *TAG;   // Logger tag
    esp_netif_t *m_sta_netif; // Handle for the STA network interface
    bool m_initialized;       // Flag: Core network stack initialized?
    bool m_wifi_started;      // Flag: Wi-Fi driver started?
    bool m_connected;         // Flag: Connected to AP and Got IP?
    int m_retry_count;        // Counter for connection retries
    WifiFailureType m_lastFailureType; // Last connection failure type for provisioning logic

    // Event handling
    static EventGroupHandle_t s_wifi_event_group;   // Static Event group to signal connection status
    esp_event_handler_instance_t m_instance_any_id; // Instance handle for WIFI_EVENT handler
    esp_event_handler_instance_t m_instance_got_ip; // Instance handle for IP_EVENT_STA_GOT_IP handler

    // Event bits (use static const or define)
    static const int WIFI_CONNECTED_BIT = BIT0;
    static const int WIFI_FAIL_BIT = BIT1;


    /// <summary>
    /// Deinitializes network components.
    /// </summary>
    void Deinitialize();

    /// <summary>
    /// Static event handler callback for Wi-Fi and IP events.
    /// </summary>
    static void EventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

public:
    /// <summary>
    /// Constructor. Initializes the network manager.
    /// </summary>
    EspNetworkManager();

    /// <summary>
    /// Initializes the underlying network stack (NetIF, Event Loop, Wi-Fi Driver).
    /// Should be called only once.
    /// </summary>
    /// <returns>true on success, false otherwise.</returns>
    bool Initialize();

    /// <summary>
    /// Destructor. Deinitializes network components.
    /// </summary>
    virtual ~EspNetworkManager();

    // --- Delete copy constructor and assignment operator ---
    EspNetworkManager(const EspNetworkManager &) = delete;
    EspNetworkManager &operator=(const EspNetworkManager &) = delete;

    /// <summary>
    /// Attempts to connect to the Wi-Fi network. Blocks until connection
    /// succeeds (gets IP), fails, or times out.
    /// </summary>
    /// <param name="ssid">Wi-Fi SSID.</param>
    /// <param name="password">Wi-Fi Password.</param>
    /// <returns>true on success, false on failure or timeout.</returns>
    bool Connect(const char *ssid, const char *password) override;

    /// <summary>
    /// Disconnects from the Wi-Fi network and stops the Wi-Fi driver.
    /// </summary>
    void Disconnect() override;

    /// <summary>
    /// Checks if currently connected to Wi-Fi and have an IP address.
    /// </summary>
    /// <returns>true if connected, false otherwise.</returns>
    bool IsConnected() override;

    /// <summary>
    /// Gets the last connection failure type.
    /// Used by provisioning logic to determine re-provisioning triggers.
    /// </summary>
    /// <returns>WifiFailureType indicating the reason for last failure.</returns>
    WifiFailureType GetLastFailureType() const { return m_lastFailureType; }

    /// <summary>
    /// Resets the last failure type to None.
    /// Should be called after successful connection or after handling the failure.
    /// </summary>
    void ClearLastFailureType() { m_lastFailureType = WifiFailureType::WifiFailure_None; }
};

#endif // ESP_NETWORK_MANAGER_H