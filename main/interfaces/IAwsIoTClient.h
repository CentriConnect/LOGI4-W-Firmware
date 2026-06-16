// interfaces/IAwsIotClient.h
#ifndef I_AWS_IOT_CLIENT_H
#define I_AWS_IOT_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include "LogiSensorData.h"

// Maximum string lengths
#define AWS_IOT_MAX_ENDPOINT_LEN    128
#define AWS_IOT_MAX_CLIENT_ID_LEN   64
#define AWS_IOT_MAX_THING_NAME_LEN  64
#define AWS_IOT_MAX_TOPIC_LEN       128
#define AWS_IOT_MAX_ALERT_MSG_LEN   256
#define AWS_IOT_MAX_VERSION_LEN     32
#define AWS_IOT_MAX_TIMESTAMP_LEN   32

// Shadow document structure for device configuration
typedef struct {
    uint32_t reportingIntervalMinutes;
    uint32_t measurementIntervalMinutes;
    bool enableDetailedLogging;
} DeviceShadowDesired_t;

typedef struct {
    uint32_t reportingIntervalMinutes;
    uint32_t measurementIntervalMinutes;
    bool enableDetailedLogging;
    char lastReportTime[AWS_IOT_MAX_TIMESTAMP_LEN];
    char firmwareVersion[AWS_IOT_MAX_VERSION_LEN];
    int batteryLevel;
    int fuelLevel;
} DeviceShadowReported_t;

typedef struct {
    DeviceShadowDesired_t desired;
    DeviceShadowReported_t reported;
    uint32_t version;
} DeviceShadow_t;

// AWS IoT connection configuration
typedef struct {
    char endpoint[AWS_IOT_MAX_ENDPOINT_LEN];
    uint16_t port;
    char clientId[AWS_IOT_MAX_CLIENT_ID_LEN];
    char thingName[AWS_IOT_MAX_THING_NAME_LEN];
    
    // Certificate content or paths
    const char* pRootCA;
    const char* pClientCert;
    const char* pPrivateKey;
    
    // Connection parameters
    uint32_t keepAliveIntervalSec;
    uint32_t connectTimeoutMs;
    bool autoReconnect;
} AwsIotConfig_t;

// Callback function types
typedef void (*ShadowUpdateCallback_t)(const DeviceShadow_t* pShadow, void* pUserData);
typedef void (*ConnectionStatusCallback_t)(bool isConnected, void* pUserData);

// AWS IoT Client Interface
class IAwsIotClient {
public:
    virtual ~IAwsIotClient() = default;
    
    // Lifecycle methods
    virtual bool Initialize(const AwsIotConfig_t* pConfig) = 0;
    virtual bool Connect() = 0;
    virtual void Disconnect() = 0;
    virtual bool IsConnected() const = 0;
    
    // Telemetry publishing
    virtual bool PublishTelemetry(const LogiSensorData* pData) = 0;
    virtual bool PublishAlert(const char* alertType, const char* message) = 0;
    
    // Shadow operations
    virtual bool GetShadow(DeviceShadow_t* pShadow) = 0;
    virtual bool UpdateShadowReported(const DeviceShadowReported_t* pReported) = 0;
    virtual void RegisterShadowUpdateCallback(ShadowUpdateCallback_t callback, void* pUserData) = 0;
    
    // Connection status
    virtual void RegisterConnectionStatusCallback(ConnectionStatusCallback_t callback, void* pUserData) = 0;
    
    // Process pending operations (call periodically)
    virtual void ProcessMessages(uint32_t timeoutMs) = 0;
};

#endif // I_AWS_IOT_CLIENT_H