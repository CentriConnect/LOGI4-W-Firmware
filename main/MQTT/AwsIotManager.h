#ifndef AWS_IOT_MANAGER_H
#define AWS_IOT_MANAGER_H

#include "MQTT/AwsIotClient.h"
#include "LogiSensorData.h"
#include "DeviceShadowState.h" 
#include "AwsIotJobsHandler.h"
#include "ShadowParser.h"
#include <memory>

/**
 * @brief Manages AWS IoT operations for the propane tank monitor
 * * This class provides a simplified interface for main.cpp to:
 * - Post telemetry data based on schedule from device shadow
 * - Handle connection lifecycle
 * - Update device state
 * - Manage FOTA updates
 */
class AwsIotManager {
public:
    AwsIotManager();
    ~AwsIotManager();
    
    /**
     * @brief Initialize AWS IoT connection
     * @return true if successful
     */
    bool Initialize();
    
    /**
     * @brief Connect to AWS IoT (call when network is available)
     * @return true if successful
     */
    bool Connect();
    
    /**
     * @brief Disconnect from AWS IoT (call before network disconnect)
     */
    void Disconnect();
    
    /**
     * @brief Check if it's time to post telemetry based on shadow schedule
     * @param minutesSinceLastPost Minutes since last telemetry post
     * @return true if telemetry should be posted
     */
    bool ShouldPostTelemetry(uint32_t minutesSinceLastPost);
    
    /**
     * @brief Post telemetry data to AWS IoT
     * @param sensorData The sensor data to post
     * @return true if successful
     */
    bool PostTelemetry(const LogiSensorData& sensorData);

    /**
     * @brief Post telemetry and wait for QoS 1 acknowledgment.
     * @param sensorData The sensor data to post
     * @return true if post was successful and acknowledged by the server.
     */
    bool PostTelemetryAndWait(const LogiSensorData& sensorData);

    /**
     * @brief Post telemetry in LOGI4-compatible format with full context.
     * @param sensorData Sensor readings
     * @param context Extended telemetry context with device info, settings, etc.
     * @return true if published successfully
     */
    bool PostTelemetryLogi4Format(const LogiSensorData& sensorData, const TelemetryContext& context);
    
    /**
     * @brief Sync with device shadow to get latest configuration
     * @return true if successful
     */
    bool SyncDeviceShadow();
    
    /**
     * @brief Get current posting interval from shadow
     * @return Posting interval in minutes
     */
    uint32_t GetPostingIntervalMinutes() const;
    
    /**
     * @brief Check if posting is enabled
     * @return true if posting is enabled
     */
    bool IsPostingEnabled() const;
    
    // Enhanced methods
    
    /**
     * @brief Get enhanced shadow with full configuration
     */
    bool GetEnhancedShadow(DeviceShadowState& state);
    
    /**
     * @brief Post telemetry with device status
     */
    bool PostEnhancedTelemetry(const LogiSensorData& sensorData,
                               const DeviceShadowState& deviceState);
    
    /**
     * @brief Update shadow with device status and sensor data
     */
    bool UpdateShadowWithStatus(const DeviceShadowState& state,
                               const LogiSensorData* sensorData = nullptr);
    
    /**
     * @brief Get current shadow state
     */
    void GetCurrentShadowState(DeviceShadowState& state) const { state = shadow_state; }
    
    /**
     * @brief Get AWS IoT client for Jobs handler
     */
    AwsIotClient* GetAwsClient() { return static_cast<AwsIotClient*>(aws_client.get()); }
    
    /**
     * @brief Initialize Jobs handler
     */
    bool InitializeJobsHandler(const std::string& thingName);
    
    /**
     * @brief Get Jobs handler
     */
    AwsIotJobsHandler* GetJobsHandler() { return jobs_handler.get(); }

    bool GetDeltaReceivedFlag()
    {
        return delta_received_flag;
    }
    
private:
    std::unique_ptr<IAwsIotClient> aws_client;
    std::unique_ptr<AwsIotJobsHandler> jobs_handler;
    DeviceShadowState shadow_state;
    bool shadow_synced;
    bool delta_received_flag;
    
    // Callback for shadow delta updates
    void onShadowDelta(const DeviceShadowState& newState);
    
    static const char* TAG;
};

#endif // AWS_IOT_MANAGER_H