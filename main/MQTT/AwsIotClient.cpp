#include "AwsIotClient.h"
#include "DeviceShadowState.h"
#include "LogiSensorData.h"
#include "AwsIotConfig.h"
#include "ShadowParser.h"
#include "logi/ResetCounter.h"
#include "esp_log.h"
#include "esp_secure_cert_read.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "sdkconfig.h"
#include "cJSON.h"
#include <cmath>
#include <cstring>

// AWS Root CA Certificate - Amazon Root CA 1
const char AWS_ROOT_CA[] = R"(-----BEGIN CERTIFICATE-----
MIIDQTCCAimgAwIBAgITBmyfz5m/jAo54vB4ikPmljZbyjANBgkqhkiG9w0BAQsF
ADA5MQswCQYDVQQGEwJVUzEPMA0GA1UEChMGQW1hem9uMRkwFwYDVQQDExBBbWF6
b24gUm9vdCBDQSAxMB4XDTE1MDUyNjAwMDAwMFoXDTM4MDExNzAwMDAwMFowOTEL
MAkGA1UEBhMCVVMxDzANBgNVBAoTBkFtYXpvbjEZMBcGA1UEAxMQQW1hem9uIFJv
b3QgQ0EgMTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBALJ4gHHKeNXj
ca9HgFB0fW7Y14h29Jlo91ghYPl0hAEvrAIthtOgQ3pOsqTQNroBvo3bSMgHFzZM
9O6II8c+6zf1tRn4SWiw3te5djgdYZ6k/oI2peVKVuRF4fn9tBb6dNqcmzU5L/qw
IFAGbHrQgLKm+a/sRxmPUDgH3KKHOVj4utWp+UhnMJbulHheb4mjUcAwhmahRWa6
VOujw5H5SNz/0egwLX0tdHA114gk957EWW67c4cX8jJGKLhD+rcdqsq08p8kDi1L
93FcXmn/6pUCyziKrlA4b9v7LWIbxcceVOF34GfID5yHI9Y/QCB/IIDEgEw+OyQm
jgSubJrIqg0CAwEAAaNCMEAwDwYDVR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMC
AYYwHQYDVR0OBBYEFIQYzIU07LwMlJQuCFmcx7IQTgoIMA0GCSqGSIb3DQEBCwUA
A4IBAQCY8jdaQZChGsV2USggNiMOruYou6r4lK5IpDB/G/wkjUu0yKGX9rbxenDI
U5PMCCjjmCXPI6T53iHTfIUJrU6adTrCC2qJeHZERxhlbI1Bjjt/msv0tadQ1wUs
N+gDS63pYaACbvXy8MWy7Vu33PqUXHeeE6V/Uq2V8viTO96LXFvKWlJbYK8U90vv
o/ufQJVtMVT8QtPHRh8jrdkPSHCa2XV4cdFyQzR1bldZwgJcJmApzyMZFo6IQ6XU
5MsI+yMRQ+hDKXJioaldXgjUkK642M4UwtBV8ob2xJNDd2ZhwLnoQdeXeGADbkpy
rqXRfboQnoZsG4q5WTP468SQvvG5
-----END CERTIFICATE-----)";

// Device cert + private key are no longer compiled into the firmware. They
// live in the per-board `esp_secure_cert` partition (see partitions.csv) and
// are written at factory provisioning time alongside the Thing UUID in NVS.
// AwsIotClient::Initialize() pulls them via esp_secure_cert_mgr.

const char* AwsIotClient::TAG = "AwsIotClient";

static double roundToTwoDecimals(float value)
{
    return std::round(static_cast<double>(value) * 100.0) / 100.0;
}

AwsIotClient::AwsIotClient()
    : mqtt_client(nullptr),
      connected(false),
      connection_semaphore(nullptr),
      shadow_semaphore(nullptr),
      shadow_get_success(false),
      shadow_update_success(false)
{
    connection_semaphore = xSemaphoreCreateBinary();
    shadow_semaphore = xSemaphoreCreateBinary();
    telemetry_semaphore = xSemaphoreCreateBinary();
}

AwsIotClient::~AwsIotClient() {
    Disconnect();
    if (mqtt_client) 
    {
        esp_mqtt_client_destroy(mqtt_client);
    }
    if(connection_semaphore) 
    {
        vSemaphoreDelete(connection_semaphore);
    }
    if(shadow_semaphore) 
    {
        vSemaphoreDelete(shadow_semaphore);
    }
    if(telemetry_semaphore) 
    {
        vSemaphoreDelete(telemetry_semaphore);
    }
}

bool AwsIotClient::PublishTelemetryAndWait(const LogiSensorData& data)
{
    if (!connected) {
        ESP_LOGE(TAG, "Not connected to AWS IoT");
        return false;
    }
    
    std::string json = createTelemetryJson(data);
    ESP_LOGI(TAG, "Publishing telemetry and waiting for QoS: %s", json.c_str());

    // Clear the semaphore before publishing
    xSemaphoreTake(telemetry_semaphore, 0);

    if (!publishMessage(AWS_IOT_TELEMETRY_TOPIC, json.c_str(), 1)) 
    {
        ESP_LOGE(TAG, "Failed to publish telemetry message");
        return false;
    }

    // Wait for the MQTT_EVENT_PUBLISHED event to give the semaphore
    if (xSemaphoreTake(telemetry_semaphore, pdMS_TO_TICKS(AWS_IOT_MQTT_COMMAND_TIMEOUT_MS)) == pdTRUE) 
    {
        return true; // QoS ACK received
    }
    
    ESP_LOGE(TAG, "Telemetry publish timed out waiting for QoS ACK");
    return false; 
}

bool AwsIotClient::Initialize()
{
    ESP_LOGI(TAG, "Initializing AWS IoT Client...");

    // Amazon Root CA stays compile-time — it's a public, shared root for every
    // AWS IoT device on us-east-1. Only the device-unique material lives in
    // the per-board esp_secure_cert partition.
    aws_root_ca_pem = AWS_ROOT_CA;

    char* dev_cert = nullptr;
    uint32_t dev_cert_len = 0;
    esp_err_t err = esp_secure_cert_get_device_cert(&dev_cert, &dev_cert_len);
    if (err != ESP_OK || dev_cert == nullptr) {
        ESP_LOGE(TAG, "esp_secure_cert: device cert read failed (err=0x%x) — board not factory provisioned?", err);
        return false;
    }
    client_cert_pem = dev_cert;
    client_cert_len = 0;   // null-terminated PEM in flash, MQTT client will strlen

    char* priv_key = nullptr;
    uint32_t priv_key_len_local = 0;
    err = esp_secure_cert_get_priv_key(&priv_key, &priv_key_len_local);
    if (err != ESP_OK || priv_key == nullptr) {
        ESP_LOGE(TAG, "esp_secure_cert: private key read failed (err=0x%x)", err);
        return false;
    }
    private_key_pem = priv_key;
    private_key_len = 0;

    ESP_LOGI(TAG, "Certificates loaded from esp_secure_cert partition (cert=%lu B, key=%lu B)",
             (unsigned long)dev_cert_len, (unsigned long)priv_key_len_local);

    return true;
}

bool AwsIotClient::Connect() 
{
    return ConnectWithProfile(AwsIotConnectionProfile::Primary8883,
                              AWS_IOT_MQTT_COMMAND_TIMEOUT_MS * 2);
}

bool AwsIotClient::ConnectWithProfile(AwsIotConnectionProfile profile, uint32_t timeoutMs)
{
    if (profile == AwsIotConnectionProfile::Backup443)
    {
        return connectToEndpoint(AWS_IOT_BACKUP_ENDPOINT, AWS_IOT_BACKUP_PORT, timeoutMs, true);
    }

    return connectToEndpoint(AWS_IOT_ENDPOINT, AWS_IOT_PORT, timeoutMs, false);
}

bool AwsIotClient::connectToEndpoint(const char* endpoint,
                                     uint16_t port,
                                     uint32_t timeoutMs,
                                     bool useAwsPort443Alpn)
{
    if (connected) 
    {
        ESP_LOGW(TAG, "Already connected");
        return true;
    }

    if (mqtt_client)
    {
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = nullptr;
    }
    
    ESP_LOGI(TAG, "Endpoint: %s:%u%s",
             endpoint,
             static_cast<unsigned>(port),
             useAwsPort443Alpn ? " (ALPN x-amzn-mqtt-ca)" : "");
    ESP_LOGI(TAG, "Client ID: %s", AWS_IOT_CLIENT_ID);

    xSemaphoreTake(connection_semaphore, 0);
    
    esp_mqtt_client_config_t mqtt_cfg = {};
    
    // Build the full URI
    char uri[256];
    snprintf(uri, sizeof(uri), "mqtts://%s:%u", endpoint, static_cast<unsigned>(port));
    mqtt_cfg.broker.address.uri = uri;

    static const char* awsPort443Alpn[] = { AWS_IOT_PORT443_ALPN, nullptr };
    if (useAwsPort443Alpn)
    {
        mqtt_cfg.broker.verification.alpn_protos = awsPort443Alpn;
    }
    
    // Set certificates - don't set lengths, let MQTT client handle it
    mqtt_cfg.broker.verification.certificate = aws_root_ca_pem;
    mqtt_cfg.credentials.authentication.certificate = client_cert_pem;
    mqtt_cfg.credentials.authentication.key = private_key_pem;
    
    // Set client ID
    mqtt_cfg.credentials.client_id = AWS_IOT_CLIENT_ID;
    
    // Additional settings
    mqtt_cfg.network.timeout_ms = AWS_IOT_MQTT_COMMAND_TIMEOUT_MS;
    mqtt_cfg.session.keepalive = AWS_IOT_MQTT_KEEPALIVE_S;
    mqtt_cfg.buffer.size = AWS_IOT_MQTT_TX_BUF_LEN;
    mqtt_cfg.buffer.out_size = AWS_IOT_MQTT_TX_BUF_LEN;
    
    // Set protocol version to MQTT 3.1.1 (AWS IoT Core requirement)
    mqtt_cfg.session.protocol_ver = MQTT_PROTOCOL_V_3_1_1;
    
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!mqtt_client) 
    {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        return false;
    }
    
    esp_mqtt_client_register_event(mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, &AwsIotClient::mqtt_event_handler_static, this);
    
    esp_err_t err = esp_mqtt_client_start(mqtt_client);
    if (err != ESP_OK) 
    {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = nullptr;
        return false;
    }
    
    // Wait for connection
    if (!waitForConnection(timeoutMs)) 
    {
        ESP_LOGE(TAG, "Connection timeout");
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = nullptr;
        return false;
    }
    
    ESP_LOGI(TAG, "Successfully connected to AWS IoT");
    current_profile = useAwsPort443Alpn ? AwsIotConnectionProfile::Backup443 : AwsIotConnectionProfile::Primary8883;
    return true;
}

void AwsIotClient::Disconnect() 
{
    if (mqtt_client) 
    {
        if (connected)
        {
            ESP_LOGI(TAG, "Disconnecting from AWS IoT...");
            esp_mqtt_client_disconnect(mqtt_client);
        }
        esp_mqtt_client_stop(mqtt_client);
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = nullptr;
        connected = false;
    }
}

bool AwsIotClient::IsConnected() const 
{
    return connected.load();
}

void AwsIotClient::mqtt_event_handler_static(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) 
{
    AwsIotClient* instance = static_cast<AwsIotClient*>(handler_args);
    instance->handleMqttEvent(static_cast<esp_mqtt_event_handle_t>(event_data));
}

void AwsIotClient::handleMqttEvent(esp_mqtt_event_handle_t event) 
{
    switch (event->event_id) 
    {
        case MQTT_EVENT_CONNECTED:
        {
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            connected = true;
            xSemaphoreGive(connection_semaphore);
            
            // Subscribe to shadow topics
            subscribeToTopic(AWS_IOT_SHADOW_GET_ACCEPTED, 1);
            subscribeToTopic(AWS_IOT_SHADOW_GET_REJECTED, 1);
            subscribeToTopic(AWS_IOT_SHADOW_UPDATE_ACCEPTED, 1);
            subscribeToTopic(AWS_IOT_SHADOW_UPDATE_REJECTED, 1);
            break;
        }
        case MQTT_EVENT_DISCONNECTED:
        {
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            connected = false;
            break;
        }
        case MQTT_EVENT_PUBLISHED: 
        {
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            xSemaphoreGive(telemetry_semaphore);
            break;
        }
        case MQTT_EVENT_DATA:
        {
            ESP_LOGD(TAG, "Received Topic: %.*s", event->topic_len, event->topic);
            std::string topic(event->topic, event->topic_len);
            std::string data;

            if (event->total_data_len > event->data_len)
            {
                if (event->current_data_offset == 0)
                {
                    mqtt_fragment_topic = topic;
                    mqtt_fragment_data.clear();
                    mqtt_fragment_data.resize(event->total_data_len);
                }

                if (mqtt_fragment_data.size() == static_cast<size_t>(event->total_data_len) &&
                    event->current_data_offset >= 0 &&
                    event->data_len >= 0 &&
                    (event->current_data_offset + event->data_len) <= event->total_data_len)
                {
                    memcpy(&mqtt_fragment_data[event->current_data_offset], event->data, event->data_len);
                }
                else
                {
                    ESP_LOGE(TAG, "Invalid fragmented MQTT data: offset=%d len=%d total=%d",
                             event->current_data_offset,
                             event->data_len,
                             event->total_data_len);
                    mqtt_fragment_topic.clear();
                    mqtt_fragment_data.clear();
                    break;
                }

                if ((event->current_data_offset + event->data_len) < event->total_data_len)
                {
                    ESP_LOGD(TAG, "Buffered MQTT fragment: offset=%d len=%d total=%d",
                             event->current_data_offset,
                             event->data_len,
                             event->total_data_len);
                    break;
                }

                topic = mqtt_fragment_topic;
                data = mqtt_fragment_data;
                mqtt_fragment_topic.clear();
                mqtt_fragment_data.clear();
                ESP_LOGD(TAG, "Reassembled MQTT payload, len=%u", static_cast<unsigned>(data.size()));
            }
            else
            {
                data.assign(event->data, event->data_len);
            }

            if (topic.find("/shadow/get/accepted") != std::string::npos) 
            {
                if (parseShadowDocument(data.c_str(), current_shadow_state)) 
                {
                    shadow_get_success = true;
                }
                xSemaphoreGive(shadow_semaphore);
            } 
            else if (topic.find("/shadow/get/rejected") != std::string::npos) 
            {
                ESP_LOGE(TAG, "Shadow get rejected: %s", data.c_str());
                shadow_get_success = false;
                xSemaphoreGive(shadow_semaphore);
            }
            else if (topic.find("/shadow/update/accepted") != std::string::npos) 
            {
                shadow_update_success = true;
                xSemaphoreGive(shadow_semaphore);
            }
            else if (topic.find("/shadow/update/rejected") != std::string::npos) 
            {
                ESP_LOGE(TAG, "Shadow update rejected: %s", data.c_str());
                shadow_update_success = false;
                xSemaphoreGive(shadow_semaphore);
            }
            else if (generic_message_callback)
            {
                // Forward unhandled topics (e.g. job notifications) to registered handler
                generic_message_callback(topic, data);
            }
            break;
        }
        
        case MQTT_EVENT_ERROR:
        {
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) 
            {
                ESP_LOGE(TAG, "TCP Error code: %d", event->error_handle->esp_transport_sock_errno);
            } 
            else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) 
            {
                ESP_LOGE(TAG, "Connection refused error: %d", event->error_handle->connect_return_code);
            } 
            else 
            {
                ESP_LOGE(TAG, "Unknown error type: %d", event->error_handle->error_type);
            }
            break;
        }
        default:
            ESP_LOGD(TAG, "Other event id:%d", event->event_id);
            break;
    }
}

bool AwsIotClient::waitForConnection(uint32_t timeoutMs) 
{
    if (xSemaphoreTake(connection_semaphore, pdMS_TO_TICKS(timeoutMs)) == pdTRUE) 
    {
        return true;
    }
    ESP_LOGE(TAG, "Connection timed out");
    return false;
}

bool AwsIotClient::waitForShadowResponse(uint32_t timeoutMs) 
{
    return xSemaphoreTake(shadow_semaphore, pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

bool AwsIotClient::subscribeToTopic(const char* topic, int qos) 
{
    if (!IsConnected()) return false;
    int msg_id = esp_mqtt_client_subscribe(mqtt_client, topic, qos);
    ESP_LOGD(TAG, "Subscribed to %s, msg_id=%d", topic, msg_id);
    return msg_id != -1;
}

bool AwsIotClient::publishMessage(const char* topic, const char* message, int qos) 
{
    if (!IsConnected()) return false;
    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, message, 0, qos, 0);
    return msg_id != -1;
}

std::string AwsIotClient::createTelemetryJson(const LogiSensorData& data)
{
    cJSON* root = cJSON_CreateObject();

    if (AWS_IOT_THING_NAME != nullptr && AWS_IOT_THING_NAME[0] != '\0') {
        cJSON_AddStringToObject(root, "dev", AWS_IOT_THING_NAME);
    }

    time_t now;
    time(&now);
    if (now >= 1609459200) {
        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);
        char timestamp[32];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S.000", &timeinfo);
        cJSON_AddStringToObject(root, "dts", timestamp);
    }

    char schema[16];
    snprintf(schema, sizeof(schema), "%d.%d.%d",
             CONFIG_LOGI_MQTT_VERSION_MAJOR,
             CONFIG_LOGI_MQTT_VERSION_MINOR,
             CONFIG_LOGI_MQTT_VERSION_REVISION);
    cJSON_AddStringToObject(root, "sch", schema);

    char version[64];
    snprintf(version, sizeof(version), "%d.%d,%d.%d,%d.%d.%d",
             CONFIG_LOGI_HARDWARE_VERSION_MAJOR,
             CONFIG_LOGI_HARDWARE_VERSION_MINOR,
             CONFIG_LOGI_SOFTWARE_VERSION_MAJOR,
             CONFIG_LOGI_SOFTWARE_VERSION_MINOR,
             CONFIG_LOGI_MQTT_VERSION_MAJOR,
             CONFIG_LOGI_MQTT_VERSION_MINOR,
             CONFIG_LOGI_MQTT_VERSION_REVISION);
    cJSON_AddStringToObject(root, "ver", version);

    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        cJSON_AddNumberToObject(root, "lsq", ap_info.rssi);
    }

    cJSON_AddNumberToObject(root, "bat", data.AnalogBatteryVoltage);
    cJSON_AddNumberToObject(root, "ful", data.PublishedFuelLevel);
    cJSON_AddNumberToObject(root, "amb", data.MeasuredTemperatureC);
    cJSON_AddNumberToObject(root, "sol", data.SolarVoltage);
    cJSON_AddNumberToObject(root, "chg", data.PublishedBatteryLevel);
    cJSON_AddNumberToObject(root, "rst", LogiResetCounter_Get());
    cJSON_AddNumberToObject(root, "lat", data.GPSData.latitude);
    cJSON_AddNumberToObject(root, "lon", data.GPSData.longitude);
    cJSON_AddNumberToObject(root, "alt", data.GPSData.altitude);

    char gpsQuality[32];
    snprintf(gpsQuality, sizeof(gpsQuality), "%d,0", data.GPSData.rssi);
    cJSON_AddStringToObject(root, "gsq", gpsQuality);
    cJSON_AddStringToObject(root, "err", "");
    cJSON_AddNumberToObject(root, "raw", roundToTwoDecimals(data.AnalogFuelVoltage));
    cJSON_AddNumberToObject(root, "supv", roundToTwoDecimals(data.SensorSupplyVoltage));

    cJSON_AddStringToObject(root, "psch", "00:00;00, 00:00;00, 00:00;00, 00:00;00, 00:00;00, 00:00;00, 00:00;00, 00:00;00");
    cJSON_AddNumberToObject(root, "fdt", 0);
    cJSON_AddNumberToObject(root, "wto", 0);
    cJSON_AddNumberToObject(root, "fpd", 0);
    cJSON_AddNumberToObject(root, "pdt", 0);

    char* json_str = cJSON_PrintUnformatted(root);
    std::string result(json_str);
    cJSON_Delete(root);
    free(json_str);
    return result;
}

std::string AwsIotClient::createTelemetryJsonLogi4Format(const LogiSensorData& data, const TelemetryContext& ctx)
{
    cJSON* root = cJSON_CreateObject();

    if (ctx.deviceIdValid)
    {
        cJSON_AddStringToObject(root, "dev", ctx.deviceId);
    }

    if (ctx.dateTimeIsoValid)
    {
        cJSON_AddStringToObject(root, "dts", ctx.dateTimeIso);
    }
    if (ctx.mqttSchemaValid)
    {
        cJSON_AddStringToObject(root, "sch", ctx.mqttSchema);
    }
    if (ctx.deviceVersionValid)
    {
        cJSON_AddStringToObject(root, "ver", ctx.deviceVersion);
    }

    if (ctx.lteSignalQualityValid)
    {
        cJSON_AddNumberToObject(root, "lsq", ctx.lteSignalQuality);
    }

    cJSON_AddNumberToObject(root, "bat", data.AnalogBatteryVoltage);
    cJSON_AddNumberToObject(root, "ful", data.PublishedFuelLevel);
    cJSON_AddNumberToObject(root, "amb", data.MeasuredTemperatureC);
    cJSON_AddNumberToObject(root, "sol", data.SolarVoltage);
    cJSON_AddNumberToObject(root, "chg", data.PublishedBatteryLevel);
    if (ctx.resetCounterValid)
    {
        cJSON_AddNumberToObject(root, "rst", ctx.resetCounter);
    }
    cJSON_AddNumberToObject(root, "lat", data.GPSData.latitude);
    cJSON_AddNumberToObject(root, "lon", data.GPSData.longitude);
    cJSON_AddNumberToObject(root, "alt", data.GPSData.altitude);

    char gpsQuality[32];
    snprintf(gpsQuality, sizeof(gpsQuality), "%d,0", data.GPSData.rssi);
    cJSON_AddStringToObject(root, "gsq", gpsQuality);
    cJSON_AddStringToObject(root, "err", ctx.errorLogValid ? ctx.errorLog : "");
    cJSON_AddNumberToObject(root, "raw", roundToTwoDecimals(data.AnalogFuelVoltage));
    cJSON_AddNumberToObject(root, "supv", roundToTwoDecimals(data.SensorSupplyVoltage));

    if (ctx.postingScheduleValid)
    {
        cJSON_AddStringToObject(root, "psch", ctx.postingSchedule);
    }
    if (ctx.fillDwellTimeValid)
    {
        cJSON_AddNumberToObject(root, "fdt", ctx.fillDwellTime);
    }
    if (ctx.fillPostDeltaValueValid)
    {
        cJSON_AddNumberToObject(root, "fpd", ctx.fillPostDeltaValue);
    }
    if (ctx.postDwellTimeValid)
    {
        cJSON_AddNumberToObject(root, "pdt", ctx.postDwellTime);
    }
    if (ctx.lteAttemptTimeoutValid)
    {
        cJSON_AddNumberToObject(root, "wto", ctx.lteAttemptTimeout);
    }
    if (ctx.sensorSampleRateMinutesValid)
    {
        cJSON_AddNumberToObject(root, "ssr", ctx.sensorSampleRateMinutes);
    }
    if (ctx.mqttPortValid)
    {
        cJSON_AddStringToObject(root, "port", ctx.mqttPort);
    }
    if (ctx.mqttScheduledPostValid)
    {
        cJSON_AddStringToObject(root, "mqt", ctx.mqttScheduledPost);
    }
    if (ctx.eventPostsValid)
    {
        cJSON_AddBoolToObject(root, "evtpst", ctx.eventPosts);
    }
    if (ctx.eventThresholdsPctValid && ctx.eventThresholdsPct[0] != '\0')
    {
        cJSON_AddStringToObject(root, "evtlmt", ctx.eventThresholdsPct);
    }

    char* json_str = cJSON_PrintUnformatted(root);
    std::string result(json_str);
    cJSON_Delete(root);
    free(json_str);
    return result;
}

bool AwsIotClient::PublishTelemetryLogi4Format(const LogiSensorData& data, const TelemetryContext& context)
{
    if (!connected)
    {
        ESP_LOGE(TAG, "Not connected to AWS IoT");
        return false;
    }

    std::string json = createTelemetryJsonLogi4Format(data, context);
    ESP_LOGI(TAG, "Publishing LOGI4 telemetry: %s", json.c_str());

    // Clear the semaphore before publishing
    xSemaphoreTake(telemetry_semaphore, 0);

    if (!publishMessage(AWS_IOT_TELEMETRY_TOPIC, json.c_str(), 1))
    {
        ESP_LOGE(TAG, "Failed to publish LOGI4 telemetry message");
        return false;
    }

    // Wait for QoS acknowledgment
    if (xSemaphoreTake(telemetry_semaphore, pdMS_TO_TICKS(AWS_IOT_MQTT_COMMAND_TIMEOUT_MS)) == pdTRUE)
    {
        ESP_LOGI(TAG, "LOGI4 telemetry published and acknowledged");
        return true;
    }

    ESP_LOGE(TAG, "LOGI4 telemetry publish timed out waiting for QoS ACK");
    return false;
}

std::string AwsIotClient::createShadowUpdateJson(const DeviceShadowState& state) 
{
    // Use the enhanced shadow update creator
    return CreateEnhancedShadowUpdate(state, nullptr);
}

std::string AwsIotClient::createShadowUpdateJsonWithSensorData(const DeviceShadowState& state, const LogiSensorData& sensorData) 
{
    return CreateEnhancedShadowUpdate(state, &sensorData);
}

bool AwsIotClient::parseShadowDocument(const char* payload, DeviceShadowState& stateOut) 
{
    // Use the enhanced parser
    return ParseEnhancedShadowDocument(payload, stateOut);
}

bool AwsIotClient::GetDeviceShadow(DeviceShadowState& outState) 
{
    if (!connected) 
    {
        ESP_LOGE(TAG, "Not connected to AWS IoT");
        return false;
    }
    
    ESP_LOGI(TAG, "Getting device shadow...");
    
    // Clear previous state
    shadow_get_success = false;
    xSemaphoreTake(shadow_semaphore, 0); 
    
    // Request shadow
    if (!publishMessage(AWS_IOT_SHADOW_GET_TOPIC, "{}", 1)) 
    {
        ESP_LOGE(TAG, "Failed to publish shadow get request");
        return false;
    }
    
    // Wait for response
    if (!waitForShadowResponse(AWS_IOT_MQTT_COMMAND_TIMEOUT_MS)) 
    {
        ESP_LOGE(TAG, "Shadow get timeout");
        return false;
    }
    
    if (shadow_get_success) 
    {
        ESP_LOGI(TAG, "Received device shadow!");
        outState = current_shadow_state;
        return true;
    }
    
    return false;
}

bool AwsIotClient::UpdateDeviceShadow(const DeviceShadowState& state) 
{
    if (!connected) 
    {
        ESP_LOGE(TAG, "Not connected to AWS IoT");
        return false;
    }
    
    ESP_LOGI(TAG, "Updating device shadow...");
    
    std::string json = createShadowUpdateJson(state);
    
    // Clear previous state
    shadow_update_success = false;
    xSemaphoreTake(shadow_semaphore, 0); // Clear semaphore
    
    // Update shadow
    if (!publishMessage(AWS_IOT_SHADOW_UPDATE_TOPIC, json.c_str(), 1)) 
    {
        ESP_LOGE(TAG, "Failed to publish shadow update");
        return false;
    }
    
    // Wait for response
    if (!waitForShadowResponse(AWS_IOT_MQTT_COMMAND_TIMEOUT_MS)) 
    {
        ESP_LOGE(TAG, "Shadow update timeout");
        return false;
    }

    ESP_LOGI(TAG, "Updated device shadow!");
    
    return shadow_update_success;
}

bool AwsIotClient::PublishTelemetry(const LogiSensorData& data) 
{
    if (!connected) 
    {
        ESP_LOGE(TAG, "Not connected to AWS IoT");
        return false;
    }
    
    std::string json = createTelemetryJson(data);
    ESP_LOGI(TAG, "Publishing telemetry: %s", json.c_str());
    
    return publishMessage(AWS_IOT_TELEMETRY_TOPIC, json.c_str(), 1);
}

void AwsIotClient::ProcessMessages(uint32_t timeoutMs) 
{
    // ESP-IDF MQTT client handles messages in its own task
    vTaskDelay(pdMS_TO_TICKS(timeoutMs));
}

void AwsIotClient::SetShadowDeltaCallback(std::function<void(const DeviceShadowState&)> callback) 
{
    shadow_delta_callback = callback;
}
