#ifndef AWS_IOT_CLIENT_H
#define AWS_IOT_CLIENT_H

#include "IAwsIotClient.h"
#include "LogiSensorData.h"
#include "AwsIotConfig.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include <string>
#include <atomic>
#include <functional>

enum class AwsIotConnectionProfile
{
    Primary8883,
    Backup443,
};

class AwsIotClient : public IAwsIotClient 
{
public:
    AwsIotClient();
    virtual ~AwsIotClient();

    bool Initialize() override;
    bool Connect() override;
    bool ConnectWithProfile(AwsIotConnectionProfile profile, uint32_t timeoutMs);
    void Disconnect() override;
    bool IsConnected() const override;
    AwsIotConnectionProfile GetConnectionProfile() const { return current_profile; }
    bool IsBackup443Connection() const { return current_profile == AwsIotConnectionProfile::Backup443; }

    bool PublishTelemetry(const LogiSensorData& sensorData) override;
    bool PublishTelemetryAndWait(const LogiSensorData& sensorData);

    /**
     * @brief Publish telemetry in LOGI4-compatible format with full context
     * @param sensorData Sensor readings
     * @param context Extended telemetry context with device info, settings, etc.
     * @return true if published successfully
     */
    bool PublishTelemetryLogi4Format(const LogiSensorData& sensorData, const TelemetryContext& context);

    bool GetDeviceShadow(DeviceShadowState& state) override;
    bool UpdateDeviceShadow(const DeviceShadowState& state) override;
    void ProcessMessages(uint32_t timeoutMs = 100) override;
    void SetShadowDeltaCallback(std::function<void(const DeviceShadowState&)> callback) override;
    
    // Add these public methods for Jobs handler access
    bool subscribeToTopic(const char* topic, int qos);
    bool publishMessage(const char* topic, const char* message, int qos);

    /**
     * @brief Set callback for MQTT messages not handled by shadow logic.
     * Used to forward job topic messages to AwsIotJobsHandler.
     */
    using GenericMessageCallback = std::function<void(const std::string& topic, const std::string& data)>;
    void SetGenericMessageCallback(GenericMessageCallback callback) { generic_message_callback = callback; }

private:
    static void mqtt_event_handler_static(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
    void handleMqttEvent(esp_mqtt_event_handle_t event);

    bool waitForConnection(uint32_t timeoutMs);
    bool waitForShadowResponse(uint32_t timeoutMs);
    bool connectToEndpoint(const char* endpoint,
                           uint16_t port,
                           uint32_t timeoutMs,
                           bool useAwsPort443Alpn);

    std::string createTelemetryJson(const LogiSensorData& sensorData);
    std::string createTelemetryJsonLogi4Format(const LogiSensorData& sensorData, const TelemetryContext& context);
    std::string createShadowUpdateJson(const DeviceShadowState& state);
    std::string createShadowUpdateJsonWithSensorData(const DeviceShadowState& state, 
                                                     const LogiSensorData& sensorData);
    bool parseShadowDocument(const char* json, DeviceShadowState& state);

    esp_mqtt_client_handle_t mqtt_client;
    std::atomic<bool> connected;
    AwsIotConnectionProfile current_profile = AwsIotConnectionProfile::Primary8883;

    SemaphoreHandle_t connection_semaphore;
    SemaphoreHandle_t shadow_semaphore;
    SemaphoreHandle_t telemetry_semaphore;

    // --- Pointers for certificates loaded from secure storage ---
    const char* client_cert_pem = nullptr;
    uint32_t client_cert_len = 0;
    const char* private_key_pem = nullptr;
    uint32_t private_key_len = 0;
    const char* aws_root_ca_pem = nullptr;

    DeviceShadowState current_shadow_state;
    bool shadow_get_success;
    bool shadow_update_success;

    std::function<void(const DeviceShadowState&)> shadow_delta_callback;
    GenericMessageCallback generic_message_callback;
    std::string mqtt_fragment_topic;
    std::string mqtt_fragment_data;

    static const char* TAG;
};

#endif // AWS_IOT_CLIENT_H
