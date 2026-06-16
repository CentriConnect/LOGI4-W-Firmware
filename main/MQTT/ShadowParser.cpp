#include "DeviceShadowState.h"
#include "ShadowParser.h"
#include "cJSON.h"
#include "esp_log.h"
#include <string>
#include <time.h>
#include <vector> 

static const char* TAG = "ShadowParser";

// REQ-SHADOW: per-field minimum values from REV B spec. Values below the
// minimum are rejected (logged WARN, previous value preserved). 0 means
// "field not set, do not apply" — used for delta detection elsewhere.
static constexpr uint32_t MIN_FILL_DWELL_TIME_S   = 300;
static constexpr uint32_t MIN_WIFI_TIMEOUT_S      = 300;
static constexpr uint8_t  MIN_FILL_ALARM_DELTA    = 10;
static constexpr uint32_t MIN_POST_DWELL_TIME_S   = 60;
static constexpr uint32_t MIN_BLE_ADV_TIME_MS     = 1000;

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
        cJSON_GetObjectItem(state, "desired"),
        cJSON_GetObjectItem(state, "reported"),
        cJSON_GetObjectItem(state, "delta"),
        state
    };
    
    for (int i = 0; i < 4; i++) {
        cJSON* config = configs[i];
        if (!config) continue;
        
        cJSON* scheduleArray = cJSON_GetObjectItem(config, "post_schedule");
        if (scheduleArray && cJSON_IsArray(scheduleArray)) {
            for(int j = 0; j < MAX_SCHEDULE_ENTRIES; ++j) {
                stateOut.post_schedule[j].clear();
            }

            int index = 0;
            cJSON* scheduleItem;
            cJSON_ArrayForEach(scheduleItem, scheduleArray) {
                if (index < MAX_SCHEDULE_ENTRIES && cJSON_IsString(scheduleItem) && (scheduleItem->valuestring != NULL)) {
                    stateOut.post_schedule[index] = scheduleItem->valuestring;
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

        // REQ-SHADOW: ble_adv_time (REV B). Units: ms. Min 1000, default 8000.
        item = cJSON_GetObjectItem(config, "ble_adv_time");
        if (item && cJSON_IsNumber(item)) {
            uint32_t v = (uint32_t)item->valueint;
            if (v < MIN_BLE_ADV_TIME_MS) { ESP_LOGW(TAG, "ble_adv_time %u below min %u ms — rejected", (unsigned)v, (unsigned)MIN_BLE_ADV_TIME_MS); }
            else { stateOut.ble_adv_time = v; success = true; }
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
    std::vector<const char*> c_str_vector;
    for(int i = 0; i < MAX_SCHEDULE_ENTRIES; ++i) {
        c_str_vector.push_back(state.post_schedule[i].c_str());
    }
    cJSON* scheduleArray = cJSON_CreateStringArray(c_str_vector.data(), c_str_vector.size());
    cJSON_AddItemToObject(reported, "post_schedule", scheduleArray);

    // 2. Configuration values
    cJSON_AddNumberToObject(reported, "fill_dwell_time", state.fill_dwell_time);
    // REV B renames lte_timeout -> wifi_timeout. Report under both keys until the
    // cellular-schema cutover is complete so both parsers can read the value.
    cJSON_AddNumberToObject(reported, "wifi_timeout", state.lte_timeout);
    cJSON_AddNumberToObject(reported, "lte_timeout", state.lte_timeout);
    cJSON_AddNumberToObject(reported, "fill_alarm_delta", state.fill_alarm_delta);
    cJSON_AddNumberToObject(reported, "post_dwell_time", state.post_dwell_time);
    cJSON_AddNumberToObject(reported, "ble_adv_time", state.ble_adv_time);

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
    if (deltaState.ble_adv_time != 0) {
        currentState.ble_adv_time = deltaState.ble_adv_time;
        ESP_LOGI(TAG, "Updated ble_adv_time to %u ms", (unsigned int)currentState.ble_adv_time);
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