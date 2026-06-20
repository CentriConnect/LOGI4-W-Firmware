#include "EspNetworkManager.h"
#include "esp_log.h"
#include "string.h"
#include "logi/Faults.h"

#include "freertos/task.h" 

// --- Static Member Definitions ---
const char *EspNetworkManager::TAG = "EspNetworkManager";
EventGroupHandle_t EspNetworkManager::s_wifi_event_group = NULL;

// --- Constructor / Destructor ---

EspNetworkManager::EspNetworkManager() : m_sta_netif(nullptr),
                                         m_initialized(false),
                                         m_wifi_started(false),
                                         m_connected(false),
                                         m_retry_count(0),
                                         m_lastFailureType(WifiFailureType::WifiFailure_None),
                                         m_instance_any_id(nullptr),
                                         m_instance_got_ip(nullptr)
{

}

EspNetworkManager::~EspNetworkManager()
{
    Deinitialize();
}

// --- Private Initialization / Deinitialization ---

bool EspNetworkManager::Initialize()
{
    if (m_initialized)
    {
        ESP_LOGI(TAG, "Already initialized.");
        return true;
    }
    ESP_LOGI(TAG, "Initializing Network Stack...");

    // 1. Initialize TCP/IP Stack (esp-netif)
    esp_err_t err = esp_netif_init();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(err));
        return false;
    }

    // 2. Create Default Event Loop
    err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    { // Allow if already created
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(err));
        return false;
    }

    // 3. Create Wi-Fi STA Network Interface
    m_sta_netif = esp_netif_create_default_wifi_sta();
    if (m_sta_netif == nullptr)
    {
        ESP_LOGE(TAG, "esp_netif_create_default_wifi_sta failed");
        return false;
    }

    // Report a friendly DHCP hostname to the router instead of the ESP-IDF
    // default "espressif" (per Nick). Set on the STA netif now, before Wi-Fi
    // start/DHCP, so it is sent in the DHCP request on connect.
    const char *kStaHostname = "Centri-MyPropane";  // name shown on the router (was the ESP-IDF default "espressif")
    err = esp_netif_set_hostname(m_sta_netif, kStaHostname);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "esp_netif_set_hostname failed: %s (using default)", esp_err_to_name(err));
    }
    else
    {
        ESP_LOGI(TAG, "STA DHCP hostname set to '%s'", kStaHostname);
    }

    // 4. Initialize Wi-Fi Driver
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return false;
    }

    // 5. Create Event Group
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL)
    {
        ESP_LOGE(TAG, "Failed to create event group");
        return false; 
    }

    // 6. Register Event Handlers
    // WIFI_EVENT covers STA_START, STA_CONNECTED, STA_DISCONNECTED, etc.
    err = esp_event_handler_instance_register(WIFI_EVENT,
                                              ESP_EVENT_ANY_ID,
                                              &EspNetworkManager::EventHandler,
                                              this, 
                                              &m_instance_any_id);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register WIFI_EVENT handler: %s", esp_err_to_name(err));
        vEventGroupDelete(s_wifi_event_group); // Clean up event group
        return false;
    }

    // IP_EVENT covers IP_EVENT_STA_GOT_IP
    err = esp_event_handler_instance_register(IP_EVENT,
                                              IP_EVENT_STA_GOT_IP,
                                              &EspNetworkManager::EventHandler,
                                              this, 
                                              &m_instance_got_ip);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to register IP_EVENT handler: %s", esp_err_to_name(err));
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, m_instance_any_id); 
        vEventGroupDelete(s_wifi_event_group);
        return false;
    }

    // 7. Set Wi-Fi Mode (required before starting)
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to set Wi-Fi mode STA: %s", esp_err_to_name(err));
        Deinitialize(); 
        return false;
    }

    ESP_LOGI(TAG, "Network Stack Initialized successfully.");
    m_initialized = true;
    return true;
}

void EspNetworkManager::Deinitialize()
{
    ESP_LOGI(TAG, "Deinitializing Network Stack...");
    if (m_instance_got_ip)
    {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, m_instance_got_ip);
        m_instance_got_ip = nullptr;
    }
    if (m_instance_any_id)
    {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, m_instance_any_id);
        m_instance_any_id = nullptr;
    }
    if (s_wifi_event_group)
    {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }

    // Assuming Wi-Fi is stopped/disconnected by caller or in Disconnect()
    if (m_wifi_started)
    {
        esp_wifi_stop(); 
        m_wifi_started = false;
    }

    // Deinit Wi-Fi driver
    esp_wifi_deinit(); 
    if (m_sta_netif)
    {
        esp_netif_destroy_default_wifi(m_sta_netif); // Destroy STA interface
        m_sta_netif = nullptr;
    }

    // Don't delete default event loop or deinit netif usually,
    // unless this is the ONLY user in the entire application.
    // esp_event_loop_delete_default();
    // esp_netif_deinit();
    m_initialized = false;
    ESP_LOGI(TAG, "Network Stack Deinitialized.");
}

// --- Public Methods ---

bool EspNetworkManager::Connect(const char *ssid, const char *password)
{
    if (!m_initialized)
    {
        ESP_LOGE(TAG, "Connect failed: Network Manager not initialized.");
        return false;
    }
    if (m_connected)
    {
        ESP_LOGI(TAG, "Already connected.");
        return true;
    }

    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);

    // 0. Clear event bits and reset state
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);
    m_retry_count = 0;
    m_connected = false; // Ensure state reflects starting condition
    m_lastFailureType = WifiFailureType::WifiFailure_None;  // Clear previous failure

    // 1. Set Wi-Fi Configuration
    wifi_config_t wifi_config = {}; 
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

    // Ensure null termination just in case strncpy didn't hit null
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0';
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0';
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK; 

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_set_config failed: %s", esp_err_to_name(err));
        return false;
    }

    // 2. Start Wi-Fi Driver (if not already started)
    // Starting can trigger STA_START event where connect could be called,
    // but calling connect explicitly after start is also common and perhaps simpler.
    if (!m_wifi_started)
    {
        err = esp_wifi_start();
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
            return false;
        }
        m_wifi_started = true;
    }

    // 3. Initiate Connection
    err = esp_wifi_connect();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        // Might need to call esp_wifi_stop() here?
        return false;
    }

    ESP_LOGI(TAG, "Waiting for connection (Timeout: %d ms)...", WIFI_CONNECT_TIMEOUT_MS);

    // 4. Wait for connection or failure events
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE,
                                           pdFALSE, 
                                           pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));

    // 5. Check result
    if (bits & WIFI_CONNECTED_BIT)
    {
        ESP_LOGI(TAG, "Connection successful (Got IP).");
        m_connected = true;
        m_lastFailureType = WifiFailureType::WifiFailure_None;  // Clear on success
        return true;
    }
    else if (bits & WIFI_FAIL_BIT)
    {
        ESP_LOGE(TAG, "Connection failed (Disconnected or max retries reached).");
        Faults_Set(FAULT_WIFI);
        // m_lastFailureType is already set by EventHandler based on disconnect reason
        // Don't necessarily need to call disconnect, as wifi driver might be stopped by event handler
        // Or maybe stop it explicitly to be sure? Let's stop it.
        if (m_wifi_started)
        {
            esp_wifi_stop(); // Stop retries etc.
            m_wifi_started = false;
        }
        m_connected = false;
        return false;
    }
    else
    {
        ESP_LOGE(TAG, "Connection attempt timed out.");
        Faults_Set(FAULT_WIFI);
        m_lastFailureType = WifiFailureType::WifiFailure_Timeout;  // Mark as timeout
        if (m_wifi_started)
        {
            // Explicitly disconnect and stop on timeout
            esp_wifi_disconnect();
            esp_wifi_stop();
            m_wifi_started = false;
        }
        m_connected = false;
        return false;
    }
}

void EspNetworkManager::Disconnect()
{
    if (!m_initialized)
    {
        ESP_LOGE(TAG, "Disconnect failed: Network Manager not initialized.");
        return;
    }
    if (!m_wifi_started && !m_connected)
    {
        ESP_LOGI(TAG, "Already disconnected/stopped.");
        return;
    }

    ESP_LOGI(TAG, "Disconnecting from Wi-Fi...");
    // Unregister handlers *before* stopping/deiniting might be safer
    // if Deinitialize isn't immediately called after.
    // Let's assume Disconnect is used *during* operation, Deinitialize on cleanup.
    esp_err_t err_d = esp_wifi_disconnect();
    esp_err_t err_s = esp_wifi_stop();
    if (err_d != ESP_OK)
        ESP_LOGW(TAG, "esp_wifi_disconnect failed: %s", esp_err_to_name(err_d));
    if (err_s != ESP_OK)
        ESP_LOGW(TAG, "esp_wifi_stop failed: %s", esp_err_to_name(err_s));

    m_wifi_started = false;                                                       
    m_connected = false;                                                          
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT); 
    ESP_LOGI(TAG, "Wi-Fi disconnected and stopped.");
}

bool EspNetworkManager::IsConnected()
{
    // Return the state managed internally based on events
    return m_initialized && m_connected;
}

// --- Static Event Handler ---

void EspNetworkManager::EventHandler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    // Get pointer to instance
    EspNetworkManager *instance = static_cast<EspNetworkManager *>(arg); 

    if (event_base == WIFI_EVENT)
    {
        switch (event_id)
        {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "Event: Wi-Fi STA Started");
            // Don't connect here, Connect() method drives connection
            break;

        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "Event: Wi-Fi Connected to AP");

            // Reset retry counter on successful connection to AP
            // Waiting for IP address now...
            instance->m_retry_count = 0; 
            break;

        case WIFI_EVENT_STA_DISCONNECTED:
        {
            wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*)event_data;
            ESP_LOGW(TAG, "Event: Wi-Fi Disconnected from AP (reason: %d)", disconnected->reason);

            // Categorize disconnection reason for provisioning logic
            // See esp_wifi_types.h for WIFI_REASON_* values
            switch (disconnected->reason) {
                case WIFI_REASON_AUTH_FAIL:
                case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
                case WIFI_REASON_HANDSHAKE_TIMEOUT:
                case WIFI_REASON_MIC_FAILURE:
                case WIFI_REASON_AUTH_EXPIRE:
                    ESP_LOGE(TAG, "Authentication failure detected");
                    instance->m_lastFailureType = WifiFailureType::WifiFailure_AuthError;
                    break;

                case WIFI_REASON_NO_AP_FOUND:
                    ESP_LOGE(TAG, "AP not found (SSID changed or out of range)");
                    instance->m_lastFailureType = WifiFailureType::WifiFailure_ApNotFound;
                    break;

                default:
                    // Other reasons (connection lost, etc.) - don't trigger re-provisioning
                    ESP_LOGW(TAG, "Disconnection reason %d - not marking as auth/AP failure",
                             disconnected->reason);
                    // Keep existing failure type if already set, otherwise leave as None
                    break;
            }

            // Connection lost or failed. Signal failure.
            instance->m_connected = false;
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            break;
        }

        default:
            ESP_LOGD(TAG, "Event: Unhandled WIFI_EVENT (%ld)", event_id);
            break;
        }
    }
    else if (event_base == IP_EVENT)
    {
        switch (event_id)
        {
        case IP_EVENT_STA_GOT_IP:
        {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            ESP_LOGI(TAG, "Event: Got IP Address: " IPSTR, IP2STR(&event->ip_info.ip));

            // Reset retry counter on success
            instance->m_retry_count = 0; 
            // Signal successful connection (including IP)
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            // Note: m_connected is set by Connect() after wait succeeds
            break;
        }
        default:
            ESP_LOGD(TAG, "Event: Unhandled IP_EVENT (%ld)", event_id);
            break;
        }
    }
    else
    {
        ESP_LOGW(TAG, "Event: Unhandled event_base (%s)", event_base);
    }
}