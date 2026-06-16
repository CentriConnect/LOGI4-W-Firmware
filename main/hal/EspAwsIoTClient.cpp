// hal/EspAwsIotClient.cpp
#include "hal/EspAwsIotClient.h"
#include "esp_log.h"
#include "esp_system.h"
#include <string.h>
#include <stdio.h>

// AWS IoT SDK includes
#include "aws_iot_config.h"
#include "aws_iot_mqtt_client_interface.h"
#include "aws_iot_shadow_interface.h"
#include "aws_iot_version.h"
#include "aws_iot_error.h"

// JSON parsing
#include "cJSON.h"

// FreeRTOS
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char* TAG = "EspAwsIotClient";

// Buffer sizes
#define SHADOW_RX_BUFFER_SIZE       1024
#define JSON_BUFFER_SIZE            1024
#define SHADOW_OPERATION_TIMEOUT_MS 5000

// Topic templates
static const char* TELEMETRY_TOPIC_FMT = "dt/logi/%s/telemetry";
static const char* ALERT_TOPIC_FMT = "dt/logi/%s/alerts";

// Implementation structure
struct EspAwsIotClient::Impl {
    AWS_IoT_Client mqttClient;
    AwsIotConfig_t config;
    
    bool isInitialized;
    bool isConnected;
    SemaphoreHandle_t connectionMutex;
    
    // Callbacks
    ShadowUpdateCallback_t shadowUpdateCallback;
    ConnectionStatusCallback_t connectionStatusCallback;
    void* pShadowCallbackUserData;
    void* pConnectionCallbackUserData;
    
    // Shadow handling
    char shadowRxBuffer[SHADOW_RX_BUFFER_SIZE];
    
    // Shadow document tracking
    DeviceShadow_t lastKnownShadow;
    bool shadowUpdatePending;
    
    // Working buffers
    char topicBuffer[AWS_IOT_MAX_TOPIC_LEN];
    char jsonBuffer[JSON_BUFFER_SIZE];
};

EspAwsIotClient::EspAwsIotClient() : m_pImpl(nullptr) {
    m_pImpl = (Impl*)pvPortMalloc(sizeof(Impl));
    if (m_pImpl) {
        memset(m_pImpl, 0, sizeof(Impl));
        m_pImpl->connectionMutex = xSemaphoreCreateMutex();
        if (!m_pImpl->connectionMutex) {
            ESP_LOGE(TAG, "Failed to create mutex");
            vPortFree(m_pImpl);
            m_pImpl = nullptr;
        }
    } else {
        ESP_LOGE(TAG, "Failed to allocate implementation");
    }
}

EspAwsIotClient::~EspAwsIotClient() {
    if (m_pImpl) {
        if (m_pImpl->isConnected) {
            Disconnect();
        }
        if (m_pImpl->connectionMutex) {
            vSemaphoreDelete(m_pImpl->connectionMutex);
        }
        vPortFree(m_pImpl);
    }
}

bool EspAwsIotClient::Initialize(const AwsIotConfig_t* pConfig) {
    ESP_LOGI(TAG, "Initializing AWS IoT Client");
    
    if (!m_pImpl || !pConfig) {
        ESP_LOGE(TAG, "Invalid parameters");
        return false;
    }
    
    if (m_pImpl->isInitialized) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }
    
    // Copy configuration
    memcpy(&m_pImpl->config, pConfig, sizeof(AwsIotConfig_t));
    
    // Initialize Shadow parameters
    ShadowInitParameters_t sp = ShadowInitParametersDefault;
    sp.pHost = m_pImpl->config.endpoint;
    sp.port = m_pImpl->config.port;
    sp.pClientCRT = (char*)pConfig->pClientCert;
    sp.pClientKey = (char*)pConfig->pPrivateKey;
    sp.pRootCA = (char*)pConfig->pRootCA;
    sp.enableAutoReconnect = pConfig->autoReconnect;
    sp.disconnectHandler = EspAwsIotClient::disconnectCallback;
    sp.disconnectHandlerData = this;
    
    ESP_LOGI(TAG, "Initializing AWS IoT Shadow");
    IoT_Error_t rc = aws_iot_shadow_init(&m_pImpl->mqttClient, &sp);
    if (SUCCESS != rc) {
        ESP_LOGE(TAG, "Shadow init failed: %d", rc);
        return false;
    }
    
    m_pImpl->isInitialized = true;
    ESP_LOGI(TAG, "AWS IoT Client initialized successfully");
    return true;
}

bool EspAwsIotClient::Connect() {
    ESP_LOGI(TAG, "Connecting to AWS IoT");
    
    if (!m_pImpl || !m_pImpl->isInitialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }
    
    if (xSemaphoreTake(m_pImpl->connectionMutex, portMAX_DELAY) != pdTRUE) {
        return false;
    }
    
    bool result = false;
    
    if (m_pImpl->isConnected) {
        ESP_LOGW(TAG, "Already connected");
        result = true;
    } else {
        ShadowConnectParameters_t scp = ShadowConnectParametersDefault;
        scp.pMyThingName = m_pImpl->config.thingName;
        scp.pMqttClientId = m_pImpl->config.clientId;
        scp.mqttClientIdLen = (uint16_t)strlen(m_pImpl->config.clientId);
        scp.deleteActionHandler = nullptr;
        scp.deltaHandler = nullptr;
        
        IoT_Error_t rc = aws_iot_shadow_connect(&m_pImpl->mqttClient, &scp);
        if (SUCCESS != rc) {
            ESP_LOGE(TAG, "Shadow connect failed: %d", rc);
        } else {
            // Register persistent shadow delta callback
            rc = aws_iot_shadow_register_delta(&m_pImpl->mqttClient, shadowUpdateCallback, this);
            if (SUCCESS != rc && SHADOW_JSON_BUFFER_FULL != rc) {
                ESP_LOGE(TAG, "Shadow register delta failed: %d", rc);
                aws_iot_shadow_disconnect(&m_pImpl->mqttClient);
            } else {
                m_pImpl->isConnected = true;
                result = true;
                
                if (m_pImpl->connectionStatusCallback) {
                    m_pImpl->connectionStatusCallback(true, m_pImpl->pConnectionCallbackUserData);
                }
                
                ESP_LOGI(TAG, "Connected to AWS IoT");
            }
        }
    }
    
    xSemaphoreGive(m_pImpl->connectionMutex);
    return result;
}

void EspAwsIotClient::Disconnect() {
    if (!m_pImpl) {
        return;
    }
    
    if (xSemaphoreTake(m_pImpl->connectionMutex, portMAX_DELAY) != pdTRUE) {
        return;
    }
    
    if (m_pImpl->isConnected) {
        ESP_LOGI(TAG, "Disconnecting from AWS IoT");
        
        IoT_Error_t rc = aws_iot_shadow_disconnect(&m_pImpl->mqttClient);
        if (SUCCESS != rc) {
            ESP_LOGW(TAG, "Shadow disconnect returned: %d", rc);
        }
        
        m_pImpl->isConnected = false;
        
        if (m_pImpl->connectionStatusCallback) {
            m_pImpl->connectionStatusCallback(false, m_pImpl->pConnectionCallbackUserData);
        }
    }
    
    xSemaphoreGive(m_pImpl->connectionMutex);
}

bool EspAwsIotClient::IsConnected() const {
    if (!m_pImpl) {
        return false;
    }
    
    bool connected = false;
    if (xSemaphoreTake(m_pImpl->connectionMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        connected = m_pImpl->isConnected && 
                   (NETWORK_CONNECTED == aws_iot_mqtt_is_client_connected(&m_pImpl->mqttClient));
        xSemaphoreGive(m_pImpl->connectionMutex);
    }
    return connected;
}

bool EspAwsIotClient::PublishTelemetry(const LogiSensorData* pData) {
    if (!m_pImpl || !pData) {
        return false;
    }
    
    if (!IsConnected()) {
        ESP_LOGE(TAG, "Not connected");
        return false;
    }
    
    // Create topic
    CreateTelemetryTopic(m_pImpl->topicBuffer, sizeof(m_pImpl->topicBuffer));
    
    // Serialize data
    int payloadLen = SerializeTelemetry(pData, m_pImpl->jsonBuffer, sizeof(m_pImpl->jsonBuffer));
    if (payloadLen <= 0) {
        ESP_LOGE(TAG, "Failed to serialize telemetry");
        return false;
    }
    
    IoT_Publish_Message_Params params;
    params.qos = QOS1;
    params.isRetained = 0;
    params.payload = m_pImpl->jsonBuffer;
    params.payloadLen = (size_t)payloadLen;
    
    IoT_Error_t rc = aws_iot_mqtt_publish(&m_pImpl->mqttClient, 
                                          m_pImpl->topicBuffer, 
                                          (uint16_t)strlen(m_pImpl->topicBuffer), 
                                          &params);
    
    if (SUCCESS != rc) {
        ESP_LOGE(TAG, "Telemetry publish failed: %d", rc);
        return false;
    }
    
    ESP_LOGI(TAG, "Telemetry published to %s", m_pImpl->topicBuffer);
    return true;
}

bool EspAwsIotClient::PublishAlert(const char* alertType, const char* message) {
    if (!m_pImpl || !alertType || !message) {
        return false;
    }
    
    if (!IsConnected()) {
        ESP_LOGE(TAG, "Not connected");
        return false;
    }
    
    // Create topic
    CreateAlertTopic(m_pImpl->topicBuffer, sizeof(m_pImpl->topicBuffer));
    
    // Create alert JSON
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        return false;
    }
    
    cJSON_AddStringToObject(root, "deviceId", m_pImpl->config.thingName);
    cJSON_AddStringToObject(root, "alertType", alertType);
    cJSON_AddStringToObject(root, "message", message);
    cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));
    
    bool result = false;
    if (cJSON_PrintPreallocated(root, m_pImpl->jsonBuffer, sizeof(m_pImpl->jsonBuffer), false)) {
        IoT_Publish_Message_Params params;
        params.qos = QOS1;
        params.isRetained = 0;
        params.payload = m_pImpl->jsonBuffer;
        params.payloadLen = strlen(m_pImpl->jsonBuffer);
        
        IoT_Error_t rc = aws_iot_mqtt_publish(&m_pImpl->mqttClient, 
                                              m_pImpl->topicBuffer, 
                                              (uint16_t)strlen(m_pImpl->topicBuffer), 
                                              &params);
        
        if (SUCCESS != rc) {
            ESP_LOGE(TAG, "Alert publish failed: %d", rc);
        } else {
            ESP_LOGI(TAG, "Alert published to %s", m_pImpl->topicBuffer);
            result = true;
        }
    } else {
        ESP_LOGE(TAG, "JSON buffer too small");
    }
    
    cJSON_Delete(root);
    return result;
}

bool EspAwsIotClient::GetShadow(DeviceShadow_t* pShadow) {
    if (!m_pImpl || !pShadow) {
        return false;
    }
    
    if (!IsConnected()) {
        ESP_LOGE(TAG, "Not connected");
        return false;
    }
    
    IoT_Error_t rc = aws_iot_shadow_get(&m_pImpl->mqttClient, 
                                        m_pImpl->config.thingName, 
                                        nullptr, 
                                        nullptr, 
                                        SHADOW_OPERATION_TIMEOUT_MS);
    
    if (SUCCESS != rc) {
        ESP_LOGE(TAG, "Shadow get failed: %d", rc);
        return false;
    }
    
    // The shadow will be received via the registered callback
    // For now, return the last known shadow
    memcpy(pShadow, &m_pImpl->lastKnownShadow, sizeof(DeviceShadow_t));
    return true;
}

bool EspAwsIotClient::UpdateShadowReported(const DeviceShadowReported_t* pReported) {
    if (!m_pImpl || !pReported) {
        return false;
    }
    
    if (!IsConnected()) {
        ESP_LOGE(TAG, "Not connected");
        return false;
    }
    
    int shadowUpdateLen = SerializeShadowUpdate(pReported, m_pImpl->jsonBuffer, sizeof(m_pImpl->jsonBuffer));
    if (shadowUpdateLen <= 0) {
        ESP_LOGE(TAG, "Failed to serialize shadow update");
        return false;
    }
    
    IoT_Error_t rc = aws_iot_shadow_update(&m_pImpl->mqttClient,
                                           m_pImpl->config.thingName,
                                           m_pImpl->jsonBuffer,
                                           nullptr,
                                           nullptr,
                                           SHADOW_OPERATION_TIMEOUT_MS);
    
    if (SUCCESS != rc) {
        ESP_LOGE(TAG, "Shadow update failed: %d", rc);
        return false;
    }
    
    ESP_LOGI(TAG, "Shadow reported state updated");
    return true;
}

void EspAwsIotClient::ProcessMessages(uint32_t timeoutMs) {
    if (!m_pImpl || !IsConnected()) {
        return;
    }
    
    IoT_Error_t rc = aws_iot_shadow_yield(&m_pImpl->mqttClient, timeoutMs);
    if (SUCCESS != rc && NETWORK_ATTEMPTING_RECONNECT != rc) {
        ESP_LOGW(TAG, "Shadow yield error: %d", rc);
    }
}

void EspAwsIotClient::RegisterShadowUpdateCallback(ShadowUpdateCallback_t callback, void* pUserData) {
    if (m_pImpl) {
        m_pImpl->shadowUpdateCallback = callback;
        m_pImpl->pShadowCallbackUserData = pUserData;
    }
}

void EspAwsIotClient::RegisterConnectionStatusCallback(ConnectionStatusCallback_t callback, void* pUserData) {
    if (m_pImpl) {
        m_pImpl->connectionStatusCallback = callback;
        m_pImpl->pConnectionCallbackUserData = pUserData;
    }
}

// Helper methods
void EspAwsIotClient::CreateTelemetryTopic(char* pBuffer, size_t bufferSize) const {
    if (pBuffer && m_pImpl) {
        snprintf(pBuffer, bufferSize, TELEMETRY_TOPIC_FMT, m_pImpl->config.thingName);
    }
}

void EspAwsIotClient::CreateAlertTopic(char* pBuffer, size_t bufferSize) const {
    if (pBuffer && m_pImpl) {
        snprintf(pBuffer, bufferSize, ALERT_TOPIC_FMT, m_pImpl->config.thingName);
    }
}

int EspAwsIotClient::SerializeTelemetry(const LogiSensorData* pData, char* pBuffer, size_t bufferSize) const {
    if (!pData || !pBuffer || !m_pImpl) {
        return -1;
    }
    
    cJSON* root = cJSON_CreateObject();
    if (!root) {
        return -1;
    }
    
    // Device info
    cJSON_AddStringToObject(root, "deviceId", m_pImpl->config.thingName);
    cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));
    
    // Sensor data
    cJSON* sensors = cJSON_CreateObject();
    if (sensors) {
        cJSON_AddNumberToObject(sensors, "temperatureC", pData->MeasuredTemperatureC);
        cJSON_AddNumberToObject(sensors, "humidityPercent", pData->MeasuredHumidityPercentage);
        cJSON_AddNumberToObject(sensors, "batteryPercent", pData->PublishedBatteryLevel);
        cJSON_AddNumberToObject(sensors, "fuelLevelPercent", pData->PublishedFuelLevel);
        cJSON_AddNumberToObject(sensors, "batteryVoltage", pData->AnalogBatteryVoltage);
        cJSON_AddNumberToObject(sensors, "solarVoltage", pData->SolarVoltage);
        cJSON_AddNumberToObject(sensors, "batteryTemperatureC", pData->BatteryTemperatureC);
        cJSON_AddNumberToObject(sensors, "sensorSupplyVoltage", pData->SensorSupplyVoltage);
        cJSON_AddItemToObject(root, "sensors", sensors);
    }
    
    int result = -1;
    if (cJSON_PrintPreallocated(root, pBuffer, bufferSize, false)) {
        result = strlen(pBuffer);
    }
    
    cJSON_Delete(root);
    return result;
}

int EspAwsIotClient::SerializeShadowUpdate(const DeviceShadowReported_t* pReported, char* pBuffer, size_t bufferSize) const {
    if (!pReported || !pBuffer) {
        return -1;
    }
    
    cJSON* root = cJSON_CreateObject();
    cJSON* state = cJSON_CreateObject();
    cJSON* reportedObj = cJSON_CreateObject();
    
    if (!root || !state || !reportedObj) {
        cJSON_Delete(root);
        return -1;
    }
    
    // Add reported values
    cJSON_AddNumberToObject(reportedObj, "reportingIntervalMinutes", pReported->reportingIntervalMinutes);
    cJSON_AddNumberToObject(reportedObj, "measurementIntervalMinutes", pReported->measurementIntervalMinutes);
    cJSON_AddBoolToObject(reportedObj, "enableDetailedLogging", pReported->enableDetailedLogging);
    cJSON_AddStringToObject(reportedObj, "lastReportTime", pReported->lastReportTime);
    cJSON_AddStringToObject(reportedObj, "firmwareVersion", pReported->firmwareVersion);
    cJSON_AddNumberToObject(reportedObj, "batteryLevel", pReported->batteryLevel);
    cJSON_AddNumberToObject(reportedObj, "fuelLevel", pReported->fuelLevel);
    
    cJSON_AddItemToObject(state, "reported", reportedObj);
    cJSON_AddItemToObject(root, "state", state);
    
    int result = -1;
    if (cJSON_PrintPreallocated(root, pBuffer, bufferSize, false)) {
        result = strlen(pBuffer);
    }
    
    cJSON_Delete(root);
    return result;
}

bool EspAwsIotClient::ParseShadowDocument(const char* pJsonDoc, size_t docLength, DeviceShadow_t* pShadow) {
    if (!pJsonDoc || !pShadow) {
        return false;
    }
    
    cJSON* root = cJSON_ParseWithLength(pJsonDoc, docLength);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse shadow JSON");
        return false;
    }
    
    bool result = false;
    
    // Parse state
    cJSON* state = cJSON_GetObjectItem(root, "state");
    if (state) {
        // Parse desired
        cJSON* desired = cJSON_GetObjectItem(state, "desired");
        if (desired) {
            cJSON* item = cJSON_GetObjectItem(desired, "reportingIntervalMinutes");
            if (item && cJSON_IsNumber(item)) {
                pShadow->desired.reportingIntervalMinutes = (uint32_t)item->valueint;
            }
            
            item = cJSON_GetObjectItem(desired, "measurementIntervalMinutes");
            if (item && cJSON_IsNumber(item)) {
                pShadow->desired.measurementIntervalMinutes = (uint32_t)item->valueint;
            }
            
            item = cJSON_GetObjectItem(desired, "enableDetailedLogging");
            if (item && cJSON_IsBool(item)) {
                pShadow->desired.enableDetailedLogging = cJSON_IsTrue(item);
            }
        }
        
        // Parse reported if needed
        cJSON* reported = cJSON_GetObjectItem(state, "reported");
        if (reported) {
            // Parse reported state if needed
        }
        
        result = true;
    }
    
    // Parse version
    cJSON* version = cJSON_GetObjectItem(root, "version");
    if (version && cJSON_IsNumber(version)) {
        pShadow->version = (uint32_t)version->valueint;
    }
    
    cJSON_Delete(root);
    return result;
}

// Static callbacks
void EspAwsIotClient::shadowUpdateCallback(const char* pJsonString, uint32_t jsonStringDataLen, void* pContextData) {
    ESP_LOGI(TAG, "Shadow update received");
    
    EspAwsIotClient* pClient = static_cast<EspAwsIotClient*>(pContextData);
    if (!pClient || !pClient->m_pImpl) {
        return;
    }
    
    DeviceShadow_t shadow;
    if (pClient->ParseShadowDocument(pJsonString, jsonStringDataLen, &shadow)) {
        memcpy(&pClient->m_pImpl->lastKnownShadow, &shadow, sizeof(DeviceShadow_t));
        
        if (pClient->m_pImpl->shadowUpdateCallback) {
            pClient->m_pImpl->shadowUpdateCallback(&shadow, pClient->m_pImpl->pShadowCallbackUserData);
        }
    }
}

void EspAwsIotClient::disconnectCallback(AWS_IoT_Client* pClient, void* data) {
    ESP_LOGW(TAG, "AWS IoT disconnected");
    
    EspAwsIotClient* pAwsClient = static_cast<EspAwsIotClient*>(data);
    if (!pAwsClient || !pAwsClient->m_pImpl) {
        return;
    }
    
    pAwsClient->m_pImpl->isConnected = false;
    
    if (pAwsClient->m_pImpl->connectionStatusCallback) {
        pAwsClient->m_pImpl->connectionStatusCallback(false, pAwsClient->m_pImpl->pConnectionCallbackUserData);
    }
}