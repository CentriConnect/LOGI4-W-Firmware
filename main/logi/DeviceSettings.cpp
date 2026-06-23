#include "DeviceSettings.h"
#include <string.h> // For memcpy, strcpy, strncpy, strcmp
#include "esp_log.h"

// --- Static Member Definitions ---
const char *DeviceSettings::TAG = "DeviceSettings";

// NVS Keys
const char *DeviceSettings::KEY_DEVICE_ID = "DeviceId";
const char *DeviceSettings::KEY_SW_RESET_STATUS = "SwResetStatus";
const char *DeviceSettings::KEY_WEEKLY_SCHED = "WeeklySched";
const char *DeviceSettings::KEY_FILL_DWELL_T = "FillDwellT";
const char *DeviceSettings::KEY_LTE_TIMEOUT = "LteTimeout";
const char *DeviceSettings::KEY_FILL_ALARM_D = "FillAlarmD";
const char *DeviceSettings::KEY_POST_DWELL_T = "PostDwellT";
const char *DeviceSettings::KEY_BLE_ADV_T    = "BleAdvT";       // REQ-SHADOW: BLE adv interval (ms)
const char *DeviceSettings::KEY_EVENT_POSTS = "EventPosts";
const char *DeviceSettings::KEY_MQTT_SCHED_POST = "MqttSched";
const char *DeviceSettings::KEY_DEVICE_ID_VALID = "DeviceIdValid";
const char *DeviceSettings::KEY_DEFAULTS_SET = "DefaultsSet";

// --- Default Device ID Fallback ---
// Use this if the Kconfig option isn't defined during compilation
#ifndef CONFIG_LOGI_DEFAULT_DEVICE_ID
#define CONFIG_LOGI_DEFAULT_DEVICE_ID "00000000-0000-0000-0000-000000000000"
#endif

// --- Constructor ---
DeviceSettings::DeviceSettings(ISettingsService &settingsService) : _settingsService(settingsService),
                                                                    _initialized(false),
                                                                    _fillDwellTime(0), // Initialize members
                                                                    _lteTimeout(0),
                                                                    _fillAlarmDelta(0),
                                                                    _postDwellTime(0),
                                                                    _bleAdvTime(0),
                                                                    _eventPosts(false),

                                                                    _deviceIdValidFlag(0),
                                                                    _defaultsAppliedFlag(0)
{
    ESP_LOGI(TAG, "DeviceSettings instance created.");
    // Clear string buffers and schedule array initially
    memset(_deviceId, 0, sizeof(_deviceId));
    memset(_softwareResetStatusStr, 0, sizeof(_softwareResetStatusStr));
    memset(_mqttScheduledPost, 0, sizeof(_mqttScheduledPost));
    memset(_weeklySchedules, 0, sizeof(_weeklySchedules));
}

// --- Initialization ---
bool DeviceSettings::Initialize()
{
    if (_initialized)
    {
        ESP_LOGW(TAG, "Already initialized.");
        return true;
    }

    ESP_LOGI(TAG, "Initializing DeviceSettings...");

    if (!_settingsService.IsInitialized())
    {
        ESP_LOGE(TAG, "Underlying ISettingsService is not initialized!");
        return false;
    }

    bool success = true;

    // Load the "DefaultsApplied" flag first
    // Use the specific load helper for basic types now
    success &= loadOrDefaultBasicTypeSetting<uint16_t>(KEY_DEFAULTS_SET, _defaultsAppliedFlag, 0);

    // If defaults have never been applied (flag is not the expected value), apply them now.
    if (!success || _defaultsAppliedFlag != DEFAULTS_APPLIED_FLAG_VALUE)
    {
        ESP_LOGW(TAG, "DefaultsApplied flag not set or incorrect (Value: 0x%X). Applying default settings...", _defaultsAppliedFlag);
        if (!ApplyDefaults())
        {
            ESP_LOGE(TAG, "Failed to apply and save default settings!");
            return false; // Critical failure if defaults can't be applied
        }
        // ApplyDefaults already loaded members and saved flags, so we can mark as initialized
        _initialized = true;
        ESP_LOGI(TAG, "DeviceSettings initialized with default values.");
        return true;
    }

    // Defaults were previously applied, load all other settings
    ESP_LOGI(TAG, "Defaults previously applied. Loading settings from NVS...");
    success &= loadOrDefaultDeviceId();
    success &= loadOrDefaultSwResetStatus();
    success &= loadOrDefaultWeeklySchedules();
    // Use the specific load helper for basic types
    success &= loadOrDefaultBasicTypeSetting<uint32_t>(KEY_FILL_DWELL_T, _fillDwellTime, CONFIG_LOGI_DEFAULT_FILL_DWELL_TIME_S);
    success &= loadOrDefaultBasicTypeSetting<uint32_t>(KEY_LTE_TIMEOUT, _lteTimeout, CONFIG_LOGI_DEFAULT_LTE_TIMEOUT_S);
    success &= loadOrDefaultBasicTypeSetting<uint8_t>(KEY_FILL_ALARM_D, _fillAlarmDelta, CONFIG_LOGI_DEFAULT_FILL_PERCENT_DELTA);
    success &= loadOrDefaultBasicTypeSetting<uint32_t>(KEY_POST_DWELL_T, _postDwellTime, CONFIG_LOGI_DEFAULT_POST_DWELL_TIME_S);
    success &= loadOrDefaultBasicTypeSetting<uint32_t>(KEY_BLE_ADV_T, _bleAdvTime, CONFIG_LOGI_DEFAULT_BLE_ADV_TIME_MS);
    success &= loadOrDefaultBasicTypeSetting<bool>(KEY_EVENT_POSTS, _eventPosts, false);
    success &= loadOrDefaultMqttScheduledPost();
    success &= loadOrDefaultBasicTypeSetting<uint16_t>(KEY_DEVICE_ID_VALID, _deviceIdValidFlag, 0); // Default DeviceId is not valid

    if (!success)
    {
        ESP_LOGE(TAG, "Failed to load one or more settings during initialization!");
        // Consider returning false or attempt to ApplyDefaults again?
        // For now, mark as not initialized if any load failed after defaults were supposedly set.
        _initialized = false;
    }
    else
    {
        _initialized = true;
    }

    if (_initialized)
    {
        ESP_LOGI(TAG, "DeviceSettings initialized successfully from NVS.");
        // Optional: Log loaded values for debugging
        ESP_LOGD(TAG, "Loaded DeviceId: %s (Valid: %s)", _deviceId, isDeviceIdValid() ? "YES" : "NO");
        ESP_LOGD(TAG, "Loaded SwResetStatus: %s", _softwareResetStatusStr);
        ESP_LOGD(TAG, "Loaded FillDwellTime: %lu", _fillDwellTime);
        ESP_LOGD(TAG, "Loaded LteTimeout: %lu", _lteTimeout);
        ESP_LOGD(TAG, "Loaded FillAlarmDelta: %u", _fillAlarmDelta);
        ESP_LOGD(TAG, "Loaded PostDwellTime: %lu", _postDwellTime);
    }
    else
    {
        ESP_LOGE(TAG, "DeviceSettings initialization failed!");
    }

    return _initialized;
}

bool DeviceSettings::IsInitialized() const
{
    return _initialized;
}

bool DeviceSettings::Commit()
{
    if (!_initialized)
    {
        ESP_LOGE(TAG, "Commit failed: Not initialized.");
        return false;
    }
    return _settingsService.Commit();
}

// --- Apply Defaults ---
bool DeviceSettings::ApplyDefaults()
{
    ESP_LOGI(TAG, "Applying default settings...");
    bool success = true;

    // Device identity (DeviceId + valid flag) is factory-provisioned, not a
    // user-configurable setting. A board provisioned via tools/provision-board.ps1
    // already has its Thing UUID in NVS before defaults are ever applied — load
    // it rather than clobbering it. loadOrDefault* fall back to the unprovisioned
    // default ("0000…", invalid) only when NVS genuinely has nothing.
    success &= loadOrDefaultDeviceId();
    success &= loadOrDefaultBasicTypeSetting<uint16_t>(KEY_DEVICE_ID_VALID, _deviceIdValidFlag, 0);

    // Set members to default values
    strncpy(_softwareResetStatusStr, getSoftwareResetStatusString(SOFTWARE_RESET_STATUS_NONE), SW_RESET_STATUS_BUFFER_SIZE - 1);
    _softwareResetStatusStr[SW_RESET_STATUS_BUFFER_SIZE - 1] = '\0';

    _fillDwellTime = CONFIG_LOGI_DEFAULT_FILL_DWELL_TIME_S;
    _lteTimeout = CONFIG_LOGI_DEFAULT_LTE_TIMEOUT_S;
    _fillAlarmDelta = CONFIG_LOGI_DEFAULT_FILL_PERCENT_DELTA;
    _postDwellTime = CONFIG_LOGI_DEFAULT_POST_DWELL_TIME_S;
    _bleAdvTime = CONFIG_LOGI_DEFAULT_BLE_ADV_TIME_MS;
    _eventPosts = false;
    _mqttScheduledPost[0] = '\0';

    // Default Weekly Schedule
    memset(_weeklySchedules, 0, sizeof(_weeklySchedules));
    _weeklySchedules[0].DaysOfWeek = 0xFF; // All days
    _weeklySchedules[0].StartTime.Hour = 17;
    _weeklySchedules[0].StartTime.Minute = 0;
    _weeklySchedules[0].StartTime.Second = 0;
    // Other schedules remain zeroed out by memset

    // Set the flag indicating defaults are now applied
    _defaultsAppliedFlag = DEFAULTS_APPLIED_FLAG_VALUE;

    // --- Save all settings back to NVS using BLOB method for basic types ---
    // DeviceId / DeviceIdValid are intentionally NOT written here — loadOrDefault*
    // above already persisted them (factory value if present, default if not).
    success &= _settingsService.SetValue(KEY_SW_RESET_STATUS, reinterpret_cast<const uint8_t *>(_softwareResetStatusStr), strlen(_softwareResetStatusStr) + 1);
    success &= _settingsService.SetValue(KEY_WEEKLY_SCHED, reinterpret_cast<const uint8_t *>(_weeklySchedules), sizeof(_weeklySchedules));
    success &= _settingsService.SetValue(KEY_FILL_DWELL_T, reinterpret_cast<const uint8_t *>(&_fillDwellTime), sizeof(_fillDwellTime));
    success &= _settingsService.SetValue(KEY_LTE_TIMEOUT, reinterpret_cast<const uint8_t *>(&_lteTimeout), sizeof(_lteTimeout));
    success &= _settingsService.SetValue(KEY_FILL_ALARM_D, reinterpret_cast<const uint8_t *>(&_fillAlarmDelta), sizeof(_fillAlarmDelta));
    success &= _settingsService.SetValue(KEY_POST_DWELL_T, reinterpret_cast<const uint8_t *>(&_postDwellTime), sizeof(_postDwellTime));
    success &= _settingsService.SetValue(KEY_BLE_ADV_T, reinterpret_cast<const uint8_t *>(&_bleAdvTime), sizeof(_bleAdvTime));
    success &= _settingsService.SetValue(KEY_EVENT_POSTS, _eventPosts);
    success &= _settingsService.SetValue(KEY_MQTT_SCHED_POST, reinterpret_cast<const uint8_t *>(_mqttScheduledPost), strlen(_mqttScheduledPost) + 1);
    success &= _settingsService.SetValue(KEY_DEFAULTS_SET, reinterpret_cast<const uint8_t *>(&_defaultsAppliedFlag), sizeof(_defaultsAppliedFlag));
    // --- End Save ---

    if (success)
    {
        // Commit via the settings service directly, not DeviceSettings::Commit().
        // ApplyDefaults() runs inside Initialize() before _initialized is set, so
        // the public Commit()'s _initialized guard would reject it. This matches
        // the direct _settingsService.SetValue() calls used above.
        success &= _settingsService.Commit();
        if (!success)
        {
            ESP_LOGE(TAG, "Failed to commit default settings!");
        }
        else
        {
            ESP_LOGI(TAG, "Default settings applied and saved.");
        }
    }
    else
    {
        ESP_LOGE(TAG, "Failed to save one or more default settings to NVS!");
    }

    return success;
}

// --- Private Load Helpers ---

// Template Helper using BLOB Get/Set for basic types
template <typename T>
bool DeviceSettings::loadOrDefaultBasicTypeSetting(const char *key, T &valueRef, T defaultValue)
{
    size_t required_size = sizeof(T);
    size_t loaded_size = _settingsService.GetValue(key, reinterpret_cast<uint8_t *>(&valueRef), required_size);

    if (loaded_size == required_size)
    {
        // Successfully loaded
        return true;
    }
    else
    {
        // Not found (loaded_size == 0) or size mismatch (loaded_size != required_size but > 0)
        ESP_LOGW(TAG, "Setting '%s' not found or size mismatch in NVS (loaded %d, expected %d). Applying default.", key, loaded_size, required_size);
        valueRef = defaultValue;
        // Save the default back as a blob
        if (!_settingsService.SetValue(key, reinterpret_cast<const uint8_t *>(&valueRef), sizeof(T)))
        {
            ESP_LOGE(TAG, "Failed to save default value for setting '%s'!", key);
            return false;
        }
        // Consider committing here if needed, or rely on later commit
    }
    return true; // Return true even if default applied, unless save failed
}

// Specialization for loading strings (stored as blobs)
bool DeviceSettings::loadOrDefaultDeviceId()
{
    // Use the blob GetValue method
    size_t actual_size = _settingsService.GetValue(KEY_DEVICE_ID, reinterpret_cast<uint8_t *>(_deviceId), DEVICE_ID_BUFFER_SIZE);

    if (actual_size == 0) // Key not found or error getting size/loading
    {
        ESP_LOGW(TAG, "Setting '%s' not found in NVS. Applying default.", KEY_DEVICE_ID);
        strncpy(_deviceId, CONFIG_LOGI_DEFAULT_DEVICE_ID, DEVICE_ID_BUFFER_SIZE - 1);
        _deviceId[DEVICE_ID_BUFFER_SIZE - 1] = '\0'; // Ensure null termination
        // Save the default back (including null terminator) as a blob
        if (!_settingsService.SetValue(KEY_DEVICE_ID, reinterpret_cast<const uint8_t *>(_deviceId), strlen(_deviceId) + 1))
        {
            ESP_LOGE(TAG, "Failed to save default value for setting '%s'!", KEY_DEVICE_ID);
            return false;
        }
    }
    else if (actual_size > DEVICE_ID_BUFFER_SIZE)
    {
        ESP_LOGE(TAG, "Buffer too small for '%s' (required %d, available %d). Loading default.", KEY_DEVICE_ID, actual_size, DEVICE_ID_BUFFER_SIZE);
        strncpy(_deviceId, CONFIG_LOGI_DEFAULT_DEVICE_ID, DEVICE_ID_BUFFER_SIZE - 1);
        _deviceId[DEVICE_ID_BUFFER_SIZE - 1] = '\0';
        // Attempt to save default back
        _settingsService.SetValue(KEY_DEVICE_ID, reinterpret_cast<const uint8_t *>(_deviceId), strlen(_deviceId) + 1);
        return false; // Indicate an issue occurred
    }
    // Ensure null termination even if loaded successfully (blob doesn't guarantee it fits exactly)
    // Find the actual end if loaded_size < buffer_size, otherwise terminate at end of buffer
    _deviceId[(actual_size < DEVICE_ID_BUFFER_SIZE) ? actual_size : (DEVICE_ID_BUFFER_SIZE - 1)] = '\0';
    return true;
}

bool DeviceSettings::loadOrDefaultSwResetStatus()
{
    size_t actual_size = _settingsService.GetValue(KEY_SW_RESET_STATUS, reinterpret_cast<uint8_t *>(_softwareResetStatusStr), SW_RESET_STATUS_BUFFER_SIZE);

    if (actual_size == 0)
    {
        ESP_LOGW(TAG, "Setting '%s' not found in NVS. Applying default.", KEY_SW_RESET_STATUS);
        const char *defaultStatus = getSoftwareResetStatusString(SOFTWARE_RESET_STATUS_NONE);
        strncpy(_softwareResetStatusStr, defaultStatus, SW_RESET_STATUS_BUFFER_SIZE - 1);
        _softwareResetStatusStr[SW_RESET_STATUS_BUFFER_SIZE - 1] = '\0';
        if (!_settingsService.SetValue(KEY_SW_RESET_STATUS, reinterpret_cast<const uint8_t *>(_softwareResetStatusStr), strlen(_softwareResetStatusStr) + 1))
        {
            ESP_LOGE(TAG, "Failed to save default value for setting '%s'!", KEY_SW_RESET_STATUS);
            return false;
        }
    }
    else if (actual_size > SW_RESET_STATUS_BUFFER_SIZE)
    {
        ESP_LOGE(TAG, "Buffer too small for '%s' (required %d, available %d). Loading default.", KEY_SW_RESET_STATUS, actual_size, SW_RESET_STATUS_BUFFER_SIZE);
        const char *defaultStatus = getSoftwareResetStatusString(SOFTWARE_RESET_STATUS_NONE);
        strncpy(_softwareResetStatusStr, defaultStatus, SW_RESET_STATUS_BUFFER_SIZE - 1);
        _softwareResetStatusStr[SW_RESET_STATUS_BUFFER_SIZE - 1] = '\0';
        _settingsService.SetValue(KEY_SW_RESET_STATUS, reinterpret_cast<const uint8_t *>(_softwareResetStatusStr), strlen(_softwareResetStatusStr) + 1);
        return false;
    }
    // Ensure null termination
    _softwareResetStatusStr[(actual_size < SW_RESET_STATUS_BUFFER_SIZE) ? actual_size : (SW_RESET_STATUS_BUFFER_SIZE - 1)] = '\0';
    return true;
}

bool DeviceSettings::loadOrDefaultWeeklySchedules()
{
    size_t required_size = sizeof(_weeklySchedules);
    size_t actual_size = _settingsService.GetValue(KEY_WEEKLY_SCHED, reinterpret_cast<uint8_t *>(_weeklySchedules), required_size);

    if (actual_size == 0) // Not found or error getting size/loading
    {
        ESP_LOGW(TAG, "Setting '%s' not found in NVS. Applying default.", KEY_WEEKLY_SCHED);
        // Apply default schedule
        memset(_weeklySchedules, 0, sizeof(_weeklySchedules));
        _weeklySchedules[0].DaysOfWeek = 0xFF; // All days
        _weeklySchedules[0].StartTime.Hour = 17;
        _weeklySchedules[0].StartTime.Minute = 0;
        _weeklySchedules[0].StartTime.Second = 0;
        // Save default back
        if (!_settingsService.SetValue(KEY_WEEKLY_SCHED, reinterpret_cast<const uint8_t *>(_weeklySchedules), sizeof(_weeklySchedules)))
        {
            ESP_LOGE(TAG, "Failed to save default value for setting '%s'!", KEY_WEEKLY_SCHED);
            return false;
        }
    }
    else if (actual_size != required_size)
    {
        // Size mismatch - data corruption likely or size changed between versions
        ESP_LOGE(TAG, "Size mismatch for '%s' (expected %d, got %d). Applying default.", KEY_WEEKLY_SCHED, required_size, actual_size);
        memset(_weeklySchedules, 0, sizeof(_weeklySchedules));
        _weeklySchedules[0].DaysOfWeek = 0xFF;
        _weeklySchedules[0].StartTime.Hour = 17;
        // Save default back
        _settingsService.SetValue(KEY_WEEKLY_SCHED, reinterpret_cast<const uint8_t *>(_weeklySchedules), sizeof(_weeklySchedules));
        return false; // Indicate an issue occurred
    }
    // Load was successful and size matches
    return true;
}

bool DeviceSettings::loadOrDefaultMqttScheduledPost()
{
    size_t actual_size = _settingsService.GetValue(KEY_MQTT_SCHED_POST, reinterpret_cast<uint8_t *>(_mqttScheduledPost), MQTT_SCHEDULED_POST_BUFFER_SIZE);
    if (actual_size > 0 && actual_size <= MQTT_SCHEDULED_POST_BUFFER_SIZE)
    {
        _mqttScheduledPost[MQTT_SCHEDULED_POST_BUFFER_SIZE - 1] = '\0';
        return true;
    }

    ESP_LOGW(TAG, "Setting '%s' not found in NVS. Applying default.", KEY_MQTT_SCHED_POST);
    _mqttScheduledPost[0] = '\0';
    return _settingsService.SetValue(KEY_MQTT_SCHED_POST, reinterpret_cast<const uint8_t *>(_mqttScheduledPost), strlen(_mqttScheduledPost) + 1);
}

// --- Getters ---

bool DeviceSettings::getDeviceId(char *buffer, size_t bufferSize) const
{
    if (!_initialized)
    {
        ESP_LOGE(TAG, "getDeviceId failed: Not initialized.");
        if (buffer && bufferSize > 0)
            buffer[0] = '\0';
        return false;
    }
    if (!buffer || bufferSize < DEVICE_ID_BUFFER_SIZE)
    {
        // Allow smaller buffers but log warning? No, require full buffer for UUID.
        ESP_LOGE(TAG, "getDeviceId failed: Invalid buffer or buffer too small (Need %d, Got %d)", DEVICE_ID_BUFFER_SIZE, bufferSize);
        if (buffer && bufferSize > 0)
            buffer[0] = '\0';
        return false;
    }
    strncpy(buffer, _deviceId, bufferSize);
    buffer[bufferSize - 1] = '\0'; // Ensure null termination
    return true;
}

DeviceSettings::SoftwareResetStatus DeviceSettings::getSoftwareResetStatus() const
{
    if (!_initialized)
    {
        ESP_LOGE(TAG, "getSoftwareResetStatus failed: Not initialized.");
        return SOFTWARE_RESET_STATUS_NONE; // Return default on error
    }
    return getSoftwareResetStatusFromString();
}

bool DeviceSettings::getWeeklySchedules(WeeklySchedule schedules[MAX_WEEKLY_SCHEDULES]) const
{
    if (!_initialized)
    {
        ESP_LOGE(TAG, "getWeeklySchedules failed: Not initialized.");
        memset(schedules, 0, sizeof(WeeklySchedule) * MAX_WEEKLY_SCHEDULES);
        return false;
    }
    if (!schedules)
    {
        ESP_LOGE(TAG, "getWeeklySchedules failed: Output buffer is null.");
        return false;
    }
    memcpy(schedules, _weeklySchedules, sizeof(_weeklySchedules));
    return true;
}

uint32_t DeviceSettings::getFillDwellTime() const
{
    if (!_initialized)
        return CONFIG_LOGI_DEFAULT_FILL_DWELL_TIME_S;
    return _fillDwellTime;
}

uint32_t DeviceSettings::getLteTimeout() const
{
    if (!_initialized)
        return CONFIG_LOGI_DEFAULT_LTE_TIMEOUT_S;
    return _lteTimeout;
}

uint8_t DeviceSettings::getFillAlarmDelta() const
{
    if (!_initialized)
        return CONFIG_LOGI_DEFAULT_FILL_PERCENT_DELTA;
    return _fillAlarmDelta;
}

uint32_t DeviceSettings::getPostDwellTime() const
{
    if (!_initialized)
        return CONFIG_LOGI_DEFAULT_POST_DWELL_TIME_S;
    return _postDwellTime;
}

bool DeviceSettings::getEventPosts() const
{
    if (!_initialized)
        return false;
    return _eventPosts;
}

bool DeviceSettings::getMqttScheduledPost(char *buffer, size_t bufferSize) const
{
    if (!_initialized || buffer == nullptr || bufferSize == 0)
        return false;

    strncpy(buffer, _mqttScheduledPost, bufferSize);
    buffer[bufferSize - 1] = '\0';
    return true;
}

bool DeviceSettings::isDeviceIdValid() const
{
    if (!_initialized)
        return false;
    return (_deviceIdValidFlag == DEVICE_ID_VALID_FLAG_VALUE);
}

// --- Setters ---

bool DeviceSettings::setDeviceId(const char *id)
{
    if (!_initialized)
    {
        ESP_LOGE(TAG, "setDeviceId failed: Not initialized.");
        return false;
    }
    if (!id)
    {
        ESP_LOGE(TAG, "setDeviceId failed: Input ID is null.");
        return false;
    }

    ESP_LOGI(TAG, "Setting Device ID to: %s", id);
    strncpy(_deviceId, id, DEVICE_ID_BUFFER_SIZE - 1);
    _deviceId[DEVICE_ID_BUFFER_SIZE - 1] = '\0'; // Ensure null termination

    // Set the valid flag
    _deviceIdValidFlag = DEVICE_ID_VALID_FLAG_VALUE;

    // --- Save both the ID (as blob) and the flag (as blob) ---
    bool success = true;
    success &= _settingsService.SetValue(KEY_DEVICE_ID, reinterpret_cast<const uint8_t *>(_deviceId), strlen(_deviceId) + 1);
    success &= _settingsService.SetValue(KEY_DEVICE_ID_VALID, reinterpret_cast<const uint8_t *>(&_deviceIdValidFlag), sizeof(_deviceIdValidFlag));
    // --- End Save ---

    if (!success)
    {
        ESP_LOGE(TAG, "Failed to save Device ID or Valid Flag to NVS!");
    }
    return success;
}

bool DeviceSettings::setSoftwareResetStatus(SoftwareResetStatus status)
{
    if (!_initialized)
    {
        ESP_LOGE(TAG, "setSoftwareResetStatus failed: Not initialized.");
        return false;
    }

    const char *statusStr = getSoftwareResetStatusString(status);
    ESP_LOGI(TAG, "Setting Software Reset Status to: %s", statusStr);

    strncpy(_softwareResetStatusStr, statusStr, SW_RESET_STATUS_BUFFER_SIZE - 1);
    _softwareResetStatusStr[SW_RESET_STATUS_BUFFER_SIZE - 1] = '\0';

    // --- Save as blob ---
    bool success = _settingsService.SetValue(KEY_SW_RESET_STATUS, reinterpret_cast<const uint8_t *>(_softwareResetStatusStr), strlen(_softwareResetStatusStr) + 1);
    // --- End Save ---

    if (!success)
    {
        ESP_LOGE(TAG, "Failed to save Software Reset Status to NVS!");
    }
    return success;
}

bool DeviceSettings::setWeeklySchedules(const WeeklySchedule schedules[MAX_WEEKLY_SCHEDULES])
{
    if (!_initialized)
    {
        ESP_LOGE(TAG, "setWeeklySchedules failed: Not initialized.");
        return false;
    }
    if (!schedules)
    {
        ESP_LOGE(TAG, "setWeeklySchedules failed: Input schedules array is null.");
        return false;
    }

    ESP_LOGI(TAG, "Setting Weekly Schedules.");
    memcpy(_weeklySchedules, schedules, sizeof(_weeklySchedules));

    // --- Save as blob ---
    bool success = _settingsService.SetValue(KEY_WEEKLY_SCHED, reinterpret_cast<const uint8_t *>(_weeklySchedules), sizeof(_weeklySchedules));
    // --- End Save ---

    if (!success)
    {
        ESP_LOGE(TAG, "Failed to save Weekly Schedules to NVS!");
    }
    return success;
}

bool DeviceSettings::setFillDwellTime(uint32_t seconds)
{
    if (!_initialized)
        return false;
    ESP_LOGI(TAG, "Setting Fill Dwell Time to: %lu seconds", seconds);
    _fillDwellTime = seconds;
    // --- Save as blob ---
    bool success = _settingsService.SetValue(KEY_FILL_DWELL_T, reinterpret_cast<const uint8_t *>(&_fillDwellTime), sizeof(_fillDwellTime));
    // --- End Save ---
    if (!success)
        ESP_LOGE(TAG, "Failed to save Fill Dwell Time!");
    return success;
}

bool DeviceSettings::setLteTimeout(uint32_t seconds)
{
    if (!_initialized)
        return false;
    ESP_LOGI(TAG, "Setting LTE Timeout to: %lu seconds", seconds);
    _lteTimeout = seconds;
    // --- Save as blob ---
    bool success = _settingsService.SetValue(KEY_LTE_TIMEOUT, reinterpret_cast<const uint8_t *>(&_lteTimeout), sizeof(_lteTimeout));
    // --- End Save ---
    if (!success)
        ESP_LOGE(TAG, "Failed to save LTE Timeout!");
    return success;
}

bool DeviceSettings::setFillAlarmDelta(uint8_t delta)
{
    if (!_initialized)
        return false;
    if (delta > 100)
        delta = 100;
    ESP_LOGI(TAG, "Setting Fill Alarm Delta to: %u %%", delta);
    _fillAlarmDelta = delta;
    // --- Save as blob ---
    bool success = _settingsService.SetValue(KEY_FILL_ALARM_D, reinterpret_cast<const uint8_t *>(&_fillAlarmDelta), sizeof(_fillAlarmDelta));
    // --- End Save ---
    if (!success)
        ESP_LOGE(TAG, "Failed to save Fill Alarm Delta!");
    return success;
}

bool DeviceSettings::setPostDwellTime(uint32_t seconds)
{
    if (!_initialized)
        return false;
    ESP_LOGI(TAG, "Setting Post Dwell Time to: %lu seconds", seconds);
    _postDwellTime = seconds;
    // --- Save as blob ---
    bool success = _settingsService.SetValue(KEY_POST_DWELL_T, reinterpret_cast<const uint8_t *>(&_postDwellTime), sizeof(_postDwellTime));
    // --- End Save ---
    if (!success)
        ESP_LOGE(TAG, "Failed to save Post Dwell Time!");
    return success;
}

uint32_t DeviceSettings::getBleAdvTime() const
{
    if (!_initialized) {
        ESP_LOGW(TAG, "Not initialized, returning default ble_adv_time");
        return CONFIG_LOGI_DEFAULT_BLE_ADV_TIME_MS;
    }
    return _bleAdvTime;
}

bool DeviceSettings::setBleAdvTime(uint32_t milliseconds)
{
    if (!_initialized)
        return false;
    if (milliseconds < CONFIG_LOGI_MIN_BLE_ADV_TIME_MS) {
        ESP_LOGW(TAG, "ble_adv_time %lu ms below min %d ms — rejected", milliseconds, CONFIG_LOGI_MIN_BLE_ADV_TIME_MS);
        return false;
    }
    ESP_LOGI(TAG, "Setting BLE ADV Time to: %lu ms", milliseconds);
    _bleAdvTime = milliseconds;
    bool success = _settingsService.SetValue(KEY_BLE_ADV_T, reinterpret_cast<const uint8_t *>(&_bleAdvTime), sizeof(_bleAdvTime));
    if (!success)
        ESP_LOGE(TAG, "Failed to save BLE ADV Time!");
    return success;
}

bool DeviceSettings::setEventPosts(bool enabled)
{
    if (!_initialized)
        return false;
    ESP_LOGI(TAG, "Setting event_posts to: %s", enabled ? "true" : "false");
    _eventPosts = enabled;
    bool success = _settingsService.SetValue(KEY_EVENT_POSTS, _eventPosts);
    if (!success)
        ESP_LOGE(TAG, "Failed to save event_posts!");
    return success;
}

bool DeviceSettings::setMqttScheduledPost(const char *schedule)
{
    if (!_initialized)
        return false;
    if (schedule == nullptr)
        schedule = "";

    ESP_LOGI(TAG, "Setting mqtt_scheduled_post to: %s", schedule);
    strncpy(_mqttScheduledPost, schedule, MQTT_SCHEDULED_POST_BUFFER_SIZE - 1);
    _mqttScheduledPost[MQTT_SCHEDULED_POST_BUFFER_SIZE - 1] = '\0';

    bool success = _settingsService.SetValue(KEY_MQTT_SCHED_POST,
                                             reinterpret_cast<const uint8_t *>(_mqttScheduledPost),
                                             strlen(_mqttScheduledPost) + 1);
    if (!success)
        ESP_LOGE(TAG, "Failed to save mqtt_scheduled_post!");
    return success;
}

// --- Private String/Enum Conversion ---

// Mapping from C file
static const char *SoftwareResetStatusStrings[] = {
    [DeviceSettings::SOFTWARE_RESET_STATUS_NONE] = "NONE",
    [DeviceSettings::SOFTWARE_RESET_STATUS_REQUESTED] = "REQUESTED",
    [DeviceSettings::SOFTWARE_RESET_STATUS_COMPLETED] = "COMPLETED",
};
// Calculate count based on enum range if possible, or define explicitly
static const int SoftwareResetStatus_Count = 3; // Assuming 0, 1, 2 are the only valid values

DeviceSettings::SoftwareResetStatus DeviceSettings::getSoftwareResetStatusFromString() const
{
    for (int i = 0; i < SoftwareResetStatus_Count; ++i)
    {
        // Make sure index is valid for the strings array
        if (i < (sizeof(SoftwareResetStatusStrings) / sizeof(SoftwareResetStatusStrings[0])) &&
            SoftwareResetStatusStrings[i] != nullptr &&
            strcmp(SoftwareResetStatusStrings[i], _softwareResetStatusStr) == 0)
        {
            return static_cast<SoftwareResetStatus>(i);
        }
    }
    ESP_LOGW(TAG, "Unrecognized SoftwareResetStatus string: '%s'. Returning NONE.", _softwareResetStatusStr);
    return SOFTWARE_RESET_STATUS_NONE; // Default if string not found
}

const char *DeviceSettings::getSoftwareResetStatusString(SoftwareResetStatus status)
{
    // Check if status is within the valid range of the enum/array
    if (status >= SOFTWARE_RESET_STATUS_NONE && status <= SOFTWARE_RESET_STATUS_COMPLETED &&
        status < (sizeof(SoftwareResetStatusStrings) / sizeof(SoftwareResetStatusStrings[0])) &&
        SoftwareResetStatusStrings[status] != nullptr)
    {
        return SoftwareResetStatusStrings[status];
    }
    ESP_LOGW(TAG, "Invalid SoftwareResetStatus enum value: %d. Returning 'NONE'.", status);
    return SoftwareResetStatusStrings[SOFTWARE_RESET_STATUS_NONE]; // Default for invalid enum
}
