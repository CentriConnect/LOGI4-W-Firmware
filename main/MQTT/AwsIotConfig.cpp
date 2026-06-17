// ----- File: main/MQTT/AwsIotConfig.cpp -----
//
// Runtime construction of the AWS IoT topic strings and clientId from the
// per-board Thing UUID stored in NVS. Mirrors the LOGI4 (cellular) pattern
// where aws_module.c::setup() pulls client_id from DeviceSettings.

#include "AwsIotConfig.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>

static const char* TAG = "AwsIotConfig";

// UUID is 36 chars + null. Topic worst case: "$aws/things/<UUID>/shadow/update/accepted"
// = 12 + 36 + 25 = 73 chars. 96 leaves headroom for future suffixes.
static constexpr size_t THING_BUF = 40;
static constexpr size_t TOPIC_BUF = 96;
static constexpr const char* THING_DEFAULT = "00000000-0000-0000-0000-000000000000";

static char s_thing[THING_BUF];
static char s_telemetry[TOPIC_BUF];
static char s_shadow_get[TOPIC_BUF];
static char s_shadow_get_accepted[TOPIC_BUF];
static char s_shadow_get_rejected[TOPIC_BUF];
static char s_shadow_update[TOPIC_BUF];
static char s_shadow_update_delta[TOPIC_BUF];
static char s_shadow_update_accepted[TOPIC_BUF];
static char s_shadow_update_rejected[TOPIC_BUF];

const char* AWS_IOT_THING_NAME            = s_thing;
const char* AWS_IOT_CLIENT_ID             = s_thing;
const char* AWS_IOT_TELEMETRY_TOPIC       = s_telemetry;
const char* AWS_IOT_SHADOW_GET_TOPIC      = s_shadow_get;
const char* AWS_IOT_SHADOW_GET_ACCEPTED   = s_shadow_get_accepted;
const char* AWS_IOT_SHADOW_GET_REJECTED   = s_shadow_get_rejected;
const char* AWS_IOT_SHADOW_UPDATE_TOPIC   = s_shadow_update;
const char* AWS_IOT_SHADOW_UPDATE_DELTA   = s_shadow_update_delta;
const char* AWS_IOT_SHADOW_UPDATE_ACCEPTED = s_shadow_update_accepted;
const char* AWS_IOT_SHADOW_UPDATE_REJECTED = s_shadow_update_rejected;

extern "C" bool AwsIotConfig_Init(const char* thingName)
{
    if (thingName == nullptr || thingName[0] == '\0' || strcmp(thingName, THING_DEFAULT) == 0) {
        ESP_LOGE(TAG, "Thing UUID not provisioned — AWS topics not initialized");
        return false;
    }

    strncpy(s_thing, thingName, sizeof(s_thing) - 1);
    s_thing[sizeof(s_thing) - 1] = '\0';

    snprintf(s_telemetry,              TOPIC_BUF, "logi4wifi/device/%s",                   s_thing);
    snprintf(s_shadow_get,             TOPIC_BUF, "$aws/things/%s/shadow/get",             s_thing);
    snprintf(s_shadow_get_accepted,    TOPIC_BUF, "$aws/things/%s/shadow/get/accepted",    s_thing);
    snprintf(s_shadow_get_rejected,    TOPIC_BUF, "$aws/things/%s/shadow/get/rejected",    s_thing);
    snprintf(s_shadow_update,          TOPIC_BUF, "$aws/things/%s/shadow/update",          s_thing);
    snprintf(s_shadow_update_delta,    TOPIC_BUF, "$aws/things/%s/shadow/update/delta",    s_thing);
    snprintf(s_shadow_update_accepted, TOPIC_BUF, "$aws/things/%s/shadow/update/accepted", s_thing);
    snprintf(s_shadow_update_rejected, TOPIC_BUF, "$aws/things/%s/shadow/update/rejected", s_thing);

    ESP_LOGI(TAG, "AWS IoT topics initialized for Thing: %s", s_thing);
    return true;
}
