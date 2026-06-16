// ----- File: main/MQTT/AwsIotConfig.h -----

#ifndef AWS_IOT_CONFIG_H
#define AWS_IOT_CONFIG_H

// AWS IoT Core Configuration
#define AWS_IOT_ENDPOINT        "a28v8anidrrkyj-ats.iot.us-east-1.amazonaws.com"
#define AWS_IOT_PORT            8883

// Identity + topics resolved at runtime from the Thing UUID written to NVS at
// factory provisioning. Pointers below point into static buffers populated by
// AwsIotConfig_Init(); they are valid for the lifetime of the process.
//
// Call AwsIotConfig_Init(thingName) once after DeviceSettings loads, before
// any AWS-related code runs. Same pattern LOGI4 (cellular) uses — there the
// client_id is pulled from DeviceSettings inside aws_module.c::setup().
extern const char* AWS_IOT_THING_NAME;
extern const char* AWS_IOT_CLIENT_ID;
extern const char* AWS_IOT_TELEMETRY_TOPIC;
extern const char* AWS_IOT_SHADOW_GET_TOPIC;
extern const char* AWS_IOT_SHADOW_GET_ACCEPTED;
extern const char* AWS_IOT_SHADOW_GET_REJECTED;
extern const char* AWS_IOT_SHADOW_UPDATE_TOPIC;
extern const char* AWS_IOT_SHADOW_UPDATE_DELTA;
extern const char* AWS_IOT_SHADOW_UPDATE_ACCEPTED;
extern const char* AWS_IOT_SHADOW_UPDATE_REJECTED;

#ifdef __cplusplus
extern "C" {
#endif

// Populates the topic + clientId buffers from the supplied Thing UUID.
// Returns false if thingName is null/empty/the default unprovisioned UUID.
bool AwsIotConfig_Init(const char* thingName);

#ifdef __cplusplus
}
#endif

// MQTT Configuration
#define AWS_IOT_MQTT_TX_BUF_LEN         2048
#define AWS_IOT_MQTT_RX_BUF_LEN         2048
#define AWS_IOT_MQTT_KEEPALIVE_S        60
#define AWS_IOT_MQTT_COMMAND_TIMEOUT_MS 10000
#define AWS_IOT_MQTT_QOS                1     // QoS 1 for at least once delivery

// Retry Configuration
#define AWS_IOT_MAX_RECONNECT_ATTEMPTS  3
#define AWS_IOT_RECONNECT_DELAY_MS      5000

// Task Configuration (if using FreeRTOS task)
#define AWS_IOT_TASK_STACK_SIZE         8192
#define AWS_IOT_TASK_PRIORITY           5

#endif // AWS_IOT_CONFIG_H
