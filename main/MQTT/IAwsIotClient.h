// ----- File: main/MQTT/IAwsIotClient.h -----

#ifndef I_AWS_IOT_CLIENT_H
#define I_AWS_IOT_CLIENT_H

#include <stdint.h>
#include <string>
#include <functional>
#include "DeviceShadowState.h" // <--- ADD THIS LINE

// Forward declaration
struct LogiSensorData;

/**
 * @brief Interface for AWS IoT Client operations
 */
class IAwsIotClient {
public:
    virtual ~IAwsIotClient() = default;
    
    /**
     * @brief Initialize the AWS IoT client
     * @return true if initialization successful
     */
    virtual bool Initialize() = 0;
    
    /**
     * @brief Connect to AWS IoT Core
     * @return true if connection successful
     */
    virtual bool Connect() = 0;
    
    /**
     * @brief Disconnect from AWS IoT Core
     */
    virtual void Disconnect() = 0;
    
    /**
     * @brief Check if connected to AWS IoT
     * @return true if connected
     */
    virtual bool IsConnected() const = 0;
    
    /**
     * @brief Publish telemetry data to AWS IoT
     * @param sensorData The sensor data to publish
     * @return true if publish successful
     */
    virtual bool PublishTelemetry(const LogiSensorData& sensorData) = 0;
    
    /**
     * @brief Get the current device shadow state
     * @param state Output parameter for shadow state
     * @return true if shadow retrieved successfully
     */
    virtual bool GetDeviceShadow(DeviceShadowState& state) = 0;
    
    /**
     * @brief Update device shadow reported state
     * @param state The state to report
     * @return true if update successful
     */
    virtual bool UpdateDeviceShadow(const DeviceShadowState& state) = 0;
    
    /**
     * @brief Process MQTT messages (call periodically)
     * @param timeoutMs Timeout in milliseconds
     */
    virtual void ProcessMessages(uint32_t timeoutMs = 100) = 0;
    
    /**
     * @brief Set callback for shadow delta updates
     * @param callback Function to call when shadow delta received
     */
    virtual void SetShadowDeltaCallback(std::function<void(const DeviceShadowState&)> callback) = 0;
};

#endif // I_AWS_IOT_CLIENT_H