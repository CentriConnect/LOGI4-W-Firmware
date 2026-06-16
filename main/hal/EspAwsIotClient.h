// hal/EspAwsIotClient.h
#ifndef ESP_AWS_IOT_CLIENT_H
#define ESP_AWS_IOT_CLIENT_H

#include "interfaces/IAwsIotClient.h"

// Forward declarations to avoid exposing AWS IoT headers
struct AWS_IoT_Client;

class EspAwsIotClient : public IAwsIotClient {
public:
    EspAwsIotClient();
    ~EspAwsIotClient() override;
    
    // IAwsIotClient implementation
    bool Initialize(const AwsIotConfig_t* pConfig) override;
    bool Connect() override;
    void Disconnect() override;
    bool IsConnected() const override;
    
    bool PublishTelemetry(const LogiSensorData* pData) override;
    bool PublishAlert(const char* alertType, const char* message) override;
    
    bool GetShadow(DeviceShadow_t* pShadow) override;
    bool UpdateShadowReported(const DeviceShadowReported_t* pReported) override;
    void RegisterShadowUpdateCallback(ShadowUpdateCallback_t callback, void* pUserData) override;
    
    void RegisterConnectionStatusCallback(ConnectionStatusCallback_t callback, void* pUserData) override;
    void ProcessMessages(uint32_t timeoutMs) override;
    
private:
    // Private implementation structure
    struct Impl;
    Impl* m_pImpl;
    
    // Helper methods
    void CreateTelemetryTopic(char* pBuffer, size_t bufferSize) const;
    void CreateAlertTopic(char* pBuffer, size_t bufferSize) const;
    int SerializeTelemetry(const LogiSensorData* pData, char* pBuffer, size_t bufferSize) const;
    int SerializeShadowUpdate(const DeviceShadowReported_t* pReported, char* pBuffer, size_t bufferSize) const;
    bool ParseShadowDocument(const char* pJsonDoc, size_t docLength, DeviceShadow_t* pShadow);
    
    // Static callbacks for AWS IoT SDK
    static void shadowUpdateCallback(const char* pJsonString, uint32_t jsonStringDataLen, void* pContextData);
    static void disconnectCallback(AWS_IoT_Client* pClient, void* data);
    
    // Disable copy constructor and assignment
    EspAwsIotClient(const EspAwsIotClient&) = delete;
    EspAwsIotClient& operator=(const EspAwsIotClient&) = delete;
};

#endif // ESP_AWS_IOT_CLIENT_H