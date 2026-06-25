#include "DeviceShadowState.h"
#include "ShadowParser.h"
#include "AwsIotConfig.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string>
#include <time.h>
#include <vector> 
#include <cctype>

static const char* TAG = "ShadowParser";

// REQ-SHADOW: per-field minimum values from REV B spec. Values below the
// minimum are rejected (logged WARN, previous value preserved). 0 means
// "field not set, do not apply" — used for delta detection elsewhere.
static constexpr uint32_t MIN_FILL_DWELL_TIME_S   = 300;
static constexpr uint32_t MIN_WIFI_TIMEOUT_S      = 15;
static constexpr uint8_t  MIN_FILL_ALARM_DELTA    = 10;
static constexpr uint32_t MIN_POST_DWELL_TIME_S   = 60;
static constexpr uint32_t MIN_MQTT_TIMEOUT_S      = AWS_IOT_MIN_WATERFALL_TIMEOUT_S;
static constexpr uint32_t DEFAULT_SENSOR_SAMPLE_RATE_MIN = 3;
static constexpr uint32_t MIN_SENSOR_SAMPLE_RATE_MIN = 1;
static constexpr uint32_t MAX_SENSOR_SAMPLE_RATE_MIN = 1440;

static std::string trimCopy(const std::string& value)
{
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
        first++;
    }

    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        last--;
    }

    return value.substr(first, last - first);
}

static bool parseScheduleString(const char* scheduleString, DeviceShadowState& stateOut)
{
    if (scheduleString == nullptr) {
        return false;
    }

    for (int j = 0; j < MAX_SCHEDULE_ENTRIES; ++j) {
        stateOut.post_schedule[j].clear();
    }

    std::string input(scheduleString);
    int index = 0;
    size_t start = 0;
    while (start <= input.size() && index < MAX_SCHEDULE_ENTRIES) {
        size_t comma = input.find(',', start);
        std::string token = trimCopy(input.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (!token.empty()) {
            stateOut.post_schedule[index++] = token;
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }

    return index > 0;
}

bool ParseEnhancedShadowDocument(const char* payload, DeviceShadowState& stateOut) {
    if (!payload) {
        ESP_LOGE(TAG, "Null payload provided");
        return false;
    }
    
    cJSON* root = cJSON_Parse(payload);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse shadow JSON: %s", cJSON_GetErrorPtr());
        return false;
    }
    
    bool success = false;
    
    cJSON* state = cJSON_GetObjectItem(root, "state");
    if (!state) state = root;

    cJSON* configs[] = {
        cJSON_GetObjectItem(state, "desired")
    };
    
    for (int i = 0; i < 1; i++) {
        cJSON* config = configs[i];
        if (!config) continue;
        
        cJSON* schedule = cJSON_GetObjectItem(config, "post_schedule");
        if (schedule && cJSON_IsString(schedule) && schedule->valuestring != NULL) {
            if (parseScheduleString(schedule->valuestring, stateOut)) {
                success = true;
            }
        } else if (schedule && cJSON_IsArray(schedule)) {
            for(int j = 0; j < MAX_SCHEDULE_ENTRIES; ++j) {
                stateOut.post_schedule[j].clear();
            }

            int index = 0;
            cJSON* scheduleItem;
            cJSON_ArrayForEach(scheduleItem, schedule) {
                if (index < MAX_SCHEDULE_ENTRIES && cJSON_IsString(scheduleItem) && (scheduleItem->valuestring != NULL)) {
                    stateOut.post_schedule[index] = trimCopy(scheduleItem->valuestring);
                    index++;
                }
            }
            success = true;
        }

        cJSON* item;
        item = cJSON_GetObjectItem(config, "fill_dwell_time");
        if (item && cJSON_IsNumber(item)) {
            uint32_t v = (uint32_t)item->valueint;
            if (v < MIN_FILL_DWELL_TIME_S) { ESP_LOGW(TAG, "fill_dwell_time %u below min %u — rejected", (unsigned)v, (unsigned)MIN_FILL_DWELL_TIME_S); }
            else { stateOut.fill_dwell_time = v; success = true; }
        }

        // REV B renames lte_timeout -> wifi_timeout; accept BOTH for cellular-schema compat.
        // wifi_timeout takes precedence if both present.
        item = cJSON_GetObjectItem(config, "lte_timeout");
        if (item && cJSON_IsNumber(item)) {
            uint32_t v = (uint32_t)item->valueint;
            if (v < MIN_WIFI_TIMEOUT_S) { ESP_LOGW(TAG, "lte_timeout %u below min %u — rejected", (unsigned)v, (unsigned)MIN_WIFI_TIMEOUT_S); }
            else { stateOut.lte_timeout = v; success = true; }
        }
        item = cJSON_GetObjectItem(config, "wifi_timeout");
        if (item && cJSON_IsNumber(item)) {
            uint32_t v = (uint32_t)item->valueint;
            if (v < MIN_WIFI_TIMEOUT_S) { ESP_LOGW(TAG, "wifi_timeout %u below min %u — rejected", (unsigned)v, (unsigned)MIN_WIFI_TIMEOUT_S); }
            else { stateOut.lte_timeout = v; success = true; }  // same internal field
        }

        item = cJSON_GetObjectItem(config, "fill_alarm_delta");
        if (item && cJSON_IsNumber(item)) {
            uint8_t v = (uint8_t)item->valueint;
            if (v < MIN_FILL_ALARM_DELTA) { ESP_LOGW(TAG, "fill_alarm_delta %u below min %u — rejected", v, MIN_FILL_ALARM_DELTA); }
            else { stateOut.fill_alarm_delta = v; success = true; }
        }

        item = cJSON_GetObjectItem(config, "post_dwell_time");
        if (item && cJSON_IsNumber(item)) {
            uint32_t v = (uint32_t)item->valueint;
            if (v < MIN_POST_DWELL_TIME_S) { ESP_LOGW(TAG, "post_dwell_time %u below min %u — rejected", (unsigned)v, (unsigned)MIN_POST_DWELL_TIME_S); }
            else { stateOut.post_dwell_time = v; success = true; }
        }

        item = cJSON_GetObjectItem(config, "mqtt_scheduled_post");
        if (item && cJSON_IsString(item) && item->valuestring != NULL) {
            stateOut.mqtt_scheduled_post = trimCopy(item->valuestring);
            success = true;
        }

        item = cJSON_GetObjectItem(config, "event_posts");
        if (item && cJSON_IsBool(item)) {
            stateOut.event_posts = cJSON_IsTrue(item);
            stateOut.event_posts_valid = true;
            success = true;
        }

        item = cJSON_GetObjectItem(config, "event_thresholds_pct");
        if (item && cJSON_IsString(item) && item->valuestring != NULL) {
            stateOut.event_thresholds_pct = trimCopy(item->valuestring);
            success = true;
        }

        item = cJSON_GetObjectItem(config, "event_direction");
        if (item && cJSON_IsString(item) && item->valuestring != NULL) {
            std::string direction = trimCopy(item->valuestring);
            if (direction != "up" && direction != "down") {
                ESP_LOGW(TAG, "event_direction '%s' invalid; defaulting to down", direction.c_str());
                direction = "down";
            }
            stateOut.event_direction = direction;
            success = true;
        }

        item = cJSON_GetObjectItem(config, "sensor_sample_rate");
        if (item && cJSON_IsNumber(item)) {
            uint32_t v = (uint32_t)item->valueint;
            if (v < MIN_SENSOR_SAMPLE_RATE_MIN || v > MAX_SENSOR_SAMPLE_RATE_MIN) {
                ESP_LOGW(TAG, "sensor_sample_rate %u invalid; using default %u",
                         (unsigned)v,
                         (unsigned)DEFAULT_SENSOR_SAMPLE_RATE_MIN);
                v = DEFAULT_SENSOR_SAMPLE_RATE_MIN;
            }
            stateOut.sensor_sample_rate = v;
            success = true;
        }

        item = cJSON_GetObjectItem(config, "acquire_gps");
        if (item && cJSON_IsBool(item)) {
            stateOut.acquire_gps = cJSON_IsTrue(item);
            stateOut.acquire_gps_valid = true;
            success = true;
        }

        item = cJSON_GetObjectItem(config, "mqtt_timeout");
        if (item && cJSON_IsNumber(item)) {
            uint32_t v = (uint32_t)item->valueint;
            if (v < MIN_MQTT_TIMEOUT_S) {
                ESP_LOGW(TAG, "mqtt_timeout %u below min %u; clamping", (unsigned)v, (unsigned)MIN_MQTT_TIMEOUT_S);
                v = MIN_MQTT_TIMEOUT_S;
            }
            stateOut.mqtt_timeout = v;
            success = true;
        }

        item = cJSON_GetObjectItem(config, "fota_enabled");
        if (item && cJSON_IsBool(item)) { stateOut.fota_enabled = cJSON_IsTrue(item); }
    }
    
    cJSON_Delete(root);
    return success;
}


std::string CreateEnhancedShadowUpdate(const DeviceShadowState& state, const LogiSensorData* sensorData) {
    cJSON* root = cJSON_CreateObject();
    cJSON* stateObj = cJSON_CreateObject();
    cJSON* reported = cJSON_CreateObject();

    // 1. Post Schedule
    std::string scheduleString;
    for(int i = 0; i < MAX_SCHEDULE_ENTRIES; ++i) {
        if (i > 0) {
            scheduleString += ",";
        }
        scheduleString += state.post_schedule[i].empty() ? "00:00;00" : state.post_schedule[i];
    }
    cJSON_AddStringToObject(reported, "post_schedule", scheduleString.c_str());

    // 2. Configuration values
    cJSON_AddNumberToObject(reported, "fill_dwell_time", state.fill_dwell_time);
    cJSON_AddNumberToObject(reported, "wifi_timeout", state.lte_timeout);
    cJSON_AddNumberToObject(reported, "fill_alarm_delta", state.fill_alarm_delta);
    cJSON_AddNumberToObject(reported, "post_dwell_time", state.post_dwell_time);
    if (!state.mqtt_scheduled_post.empty()) cJSON_AddStringToObject(reported, "mqtt_scheduled_post", state.mqtt_scheduled_post.c_str());
    if (state.event_posts_valid) cJSON_AddBoolToObject(reported, "event_posts", state.event_posts);
    if (!state.event_thresholds_pct.empty()) cJSON_AddStringToObject(reported, "event_thresholds_pct", state.event_thresholds_pct.c_str());
    if (!state.event_direction.empty()) cJSON_AddStringToObject(reported, "event_direction", state.event_direction.c_str());
    if (state.sensor_sample_rate != 0) cJSON_AddNumberToObject(reported, "sensor_sample_rate", state.sensor_sample_rate);
    if (state.acquire_gps_valid) cJSON_AddBoolToObject(reported, "acquire_gps", state.acquire_gps);
    if (state.mqtt_timeout != 0) cJSON_AddNumberToObject(reported, "mqtt_timeout", state.mqtt_timeout);

    // NOTE: All old fields like 'postingConfig', 'fotaConfig', and 'deviceStatus'
    // are now intentionally omitted to create the clean format you want.

    // Construct the final JSON document
    cJSON_AddItemToObject(stateObj, "reported", reported);
    cJSON_AddItemToObject(root, "state", stateObj);
    
    char* json_str = cJSON_PrintUnformatted(root);
    std::string result(json_str);
    cJSON_Delete(root);
    free(json_str);
    
    return result;
}


void MergeShadowDelta(DeviceShadowState& currentState, const DeviceShadowState& deltaState)
{
    if (!deltaState.post_schedule[0].empty()) {
        for(int i = 0; i < MAX_SCHEDULE_ENTRIES; ++i) {
            currentState.post_schedule[i] = deltaState.post_schedule[i];
        }
        ESP_LOGI(TAG, "Updated post_schedule.");
    }
    if (deltaState.fill_dwell_time != 0) {
        currentState.fill_dwell_time = deltaState.fill_dwell_time;
        ESP_LOGI(TAG, "Updated fill_dwell_time to %u", (unsigned int)currentState.fill_dwell_time);
    }
    if (deltaState.lte_timeout != 0) {
        currentState.lte_timeout = deltaState.lte_timeout;
        ESP_LOGI(TAG, "Updated lte_timeout to %u", (unsigned int)currentState.lte_timeout);
    }
    if (deltaState.fill_alarm_delta != 0) {
        currentState.fill_alarm_delta = deltaState.fill_alarm_delta;
        ESP_LOGI(TAG, "Updated fill_alarm_delta to %u", currentState.fill_alarm_delta);
    }
    if (deltaState.post_dwell_time != 0) {
        currentState.post_dwell_time = deltaState.post_dwell_time;
        ESP_LOGI(TAG, "Updated post_dwell_time to %u", (unsigned int)currentState.post_dwell_time);
    }
    if (!deltaState.mqtt_scheduled_post.empty()) {
        currentState.mqtt_scheduled_post = deltaState.mqtt_scheduled_post;
        ESP_LOGI(TAG, "Updated mqtt_scheduled_post to %s", currentState.mqtt_scheduled_post.c_str());
    }
    if (!deltaState.event_thresholds_pct.empty()) {
        currentState.event_thresholds_pct = deltaState.event_thresholds_pct;
        ESP_LOGI(TAG, "Updated event_thresholds_pct to %s", currentState.event_thresholds_pct.c_str());
    }
    if (!deltaState.event_direction.empty()) {
        currentState.event_direction = deltaState.event_direction;
        ESP_LOGI(TAG, "Updated event_direction to %s", currentState.event_direction.c_str());
    }
    if (deltaState.sensor_sample_rate != 0) {
        currentState.sensor_sample_rate = deltaState.sensor_sample_rate;
        ESP_LOGI(TAG, "Updated sensor_sample_rate to %u", (unsigned int)currentState.sensor_sample_rate);
    }
    if (deltaState.mqtt_timeout != 0) {
        currentState.mqtt_timeout = deltaState.mqtt_timeout;
        ESP_LOGI(TAG, "Updated mqtt_timeout to %u", (unsigned int)currentState.mqtt_timeout);
    }
    if (deltaState.event_posts_valid) {
        currentState.event_posts = deltaState.event_posts;
        currentState.event_posts_valid = true;
        ESP_LOGI(TAG, "Updated event_posts to %s", currentState.event_posts ? "true" : "false");
    }
    if (deltaState.acquire_gps_valid) {
        currentState.acquire_gps = deltaState.acquire_gps;
        currentState.acquire_gps_valid = true;
        ESP_LOGI(TAG, "Updated acquire_gps to %s", currentState.acquire_gps ? "true" : "false");
    }
}

int ConvertShadowSchedulesToWeekly(const DeviceShadowState& shadowState,
                                    WeeklySchedule schedulesOut[MAX_SCHEDULE_ENTRIES])
{
    int validCount = 0;

    // Clear all output schedules first
    for (int i = 0; i < MAX_SCHEDULE_ENTRIES; ++i) {
        schedulesOut[i].DaysOfWeek = 0;
        schedulesOut[i].StartTime.Hour = 0;
        schedulesOut[i].StartTime.Minute = 0;
        schedulesOut[i].StartTime.Second = 0;
        schedulesOut[i].StartTime.Millisecond = 0;
    }

    for (int i = 0; i < MAX_SCHEDULE_ENTRIES; ++i) {
        const std::string& schedule_str = shadowState.post_schedule[i];
        if (schedule_str.empty()) {
            continue;
        }

        int hour = 0, minute = 0;
        unsigned int day_byte = 0;

        // Parse format: "HH:MM;XX" where XX is hex (bit 7=enable, bits 0-6=days)
        if (sscanf(schedule_str.c_str(), "%d:%d;%x", &hour, &minute, &day_byte) != 3) {
            ESP_LOGW(TAG, "Failed to parse schedule[%d]: %s", i, schedule_str.c_str());
            continue;
        }

        // Check enable bit (bit 7)
        if (!(day_byte & 0x80)) {
            ESP_LOGD(TAG, "Schedule[%d] disabled: %s", i, schedule_str.c_str());
            continue;
        }

        // Validate time
        if (hour < 0 || hour > 23 || minute < 0 || minute > 59) {
            ESP_LOGW(TAG, "Invalid time in schedule[%d]: %s", i, schedule_str.c_str());
            continue;
        }

        // Convert to WeeklySchedule
        schedulesOut[validCount].StartTime.Hour = (uint8_t)hour;
        schedulesOut[validCount].StartTime.Minute = (uint8_t)minute;
        schedulesOut[validCount].StartTime.Second = 0;
        schedulesOut[validCount].StartTime.Millisecond = 0;
        schedulesOut[validCount].DaysOfWeek = (uint8_t)(day_byte & 0x7F);  // Mask off enable bit

        ESP_LOGI(TAG, "Parsed schedule[%d]: %02d:%02d, days=0x%02X",
                 validCount, hour, minute, schedulesOut[validCount].DaysOfWeek);
        validCount++;
    }

    return validCount;
}
