#ifndef DEVICE_SETTINGS_H
#define DEVICE_SETTINGS_H

#include "interfaces/ISettingsService.h"       // Dependency
#include "CLibFiles/DateTime/WeeklySchedule.h" // Include C struct definition
#include "sdkconfig.h"                         // For CONFIG_LOGI_DEFAULT_* values
#include <stdint.h>
#include <stddef.h>

// Forward declaration if NvsSettingsService is used directly (though interface is preferred)
// class NvsSettingsService;

// Define constants within the class scope
class DeviceSettings
{
public:
    // --- Constants ---
    static const size_t MAX_WEEKLY_SCHEDULES = 8;
    // Note: DEVICE_ID_SIZE includes the null terminator from sizeof() in C
    static const size_t DEVICE_ID_BUFFER_SIZE = 37; // UUID string "0000..." length + 1 for null
    static const size_t SW_RESET_STATUS_BUFFER_SIZE = 20;
    static const size_t MQTT_SCHEDULED_POST_BUFFER_SIZE = 16;
    static const uint16_t DEVICE_ID_VALID_FLAG_VALUE = 0xBAFD;
// Use the Kconfig value for the defaults applied flag
// Ensure this is defined in your sdkconfig.h or replace with literal value
#ifndef CONFIG_LOGI_DEFAULT_IS_SET_FLAG_VALUE
#warning "CONFIG_LOGI_DEFAULT_IS_SET_FLAG_VALUE not defined in sdkconfig. Assuming 0xBEEF"
#define CONFIG_LOGI_DEFAULT_IS_SET_FLAG_VALUE 0xBEEF
#endif
    static const uint16_t DEFAULTS_APPLIED_FLAG_VALUE = CONFIG_LOGI_DEFAULT_IS_SET_FLAG_VALUE;

    // --- Enum Definition ---
    // Mirroring the C enum for SoftwareResetStatus
    typedef enum
    {
        SOFTWARE_RESET_STATUS_NONE = 0,
        SOFTWARE_RESET_STATUS_REQUESTED,
        SOFTWARE_RESET_STATUS_COMPLETED,
    } SoftwareResetStatus;

    // --- Public Methods ---

    /**
     * @brief Constructor. Requires an initialized ISettingsService instance.
     * @param settingsService Reference to the settings service implementation (e.g., NvsSettingsService).
     */
    explicit DeviceSettings(ISettingsService &settingsService);

    /**
     * @brief Virtual destructor.
     */
    virtual ~DeviceSettings() = default;

    // Delete copy constructor and assignment operator
    DeviceSettings(const DeviceSettings &) = delete;
    DeviceSettings &operator=(const DeviceSettings &) = delete;

    /**
     * @brief Initializes the DeviceSettings instance.
     * This method loads settings from the underlying ISettingsService.
     * If settings are not found, it applies default values and saves them back.
     * Must be called after the ISettingsService itself is initialized.
     * @return true if initialization was successful, false otherwise.
     */
    bool Initialize();

    /**
     * @brief Checks if the DeviceSettings instance has been successfully initialized.
     * @return true if initialized, false otherwise.
     */
    bool IsInitialized() const;

    /**
     * @brief Applies the default values for all settings and saves them.
     * This is typically called internally during Initialize if defaults are missing.
     * @return true if defaults were applied and saved successfully, false otherwise.
     */
    bool ApplyDefaults();

    /**
     * @brief Commits any pending changes to the underlying storage.
     * @return true if commit was successful, false otherwise.
     */
    bool Commit();

    // --- Getters ---
    /**
     * @brief Gets the current Device ID.
     * @param buffer Character buffer to copy the Device ID into.
     * @param bufferSize The size of the provided buffer.
     * @return true if the ID was successfully copied, false otherwise (e.g., buffer too small).
     */
    bool getDeviceId(char *buffer, size_t bufferSize) const;

    /**
     * @brief Gets the current Software Reset Status.
     * @return The current SoftwareResetStatus enum value.
     */
    SoftwareResetStatus getSoftwareResetStatus() const;

    /**
     * @brief Gets a copy of the weekly schedules.
     * @param schedules Array to copy the schedules into (must be size MAX_WEEKLY_SCHEDULES).
     * @return true (currently always returns true, consider adding error check if needed).
     */
    bool getWeeklySchedules(WeeklySchedule schedules[MAX_WEEKLY_SCHEDULES]) const;

    /**
     * @brief Gets the Fill Dwell Time.
     * @return The Fill Dwell Time in seconds.
     */
    uint32_t getFillDwellTime() const;

    /**
     * @brief Gets the LTE Timeout.
     * @return The LTE Timeout in seconds.
     */
    uint32_t getLteTimeout() const;

    /**
     * @brief Gets the Fill Alarm Delta percentage.
     * @return The Fill Alarm Delta (0-100).
     */
    uint8_t getFillAlarmDelta() const;

    /**
     * @brief Gets the Post Dwell Time.
     * @return The Post Dwell Time in seconds.
     */
    uint32_t getPostDwellTime() const;

    /**
     * @brief Gets the BLE ADV interval (ms) used during the 48h provisioning window.
     * REQ-SHADOW: REV B field 'ble_adv_time', min 1000 ms, default 8000 ms.
     */
    uint32_t getBleAdvTime() const;
    bool getEventPosts() const;
    bool getMqttScheduledPost(char *buffer, size_t bufferSize) const;

    /**
     * @brief Checks if the stored Device ID is considered valid based on the flag.
     * @return true if the Device ID valid flag is set, false otherwise.
     */
    bool isDeviceIdValid() const;

    // --- Setters ---
    /**
     * @brief Sets the Device ID.
     * @param id Null-terminated C-string representing the Device ID (max DEVICE_ID_BUFFER_SIZE - 1 chars).
     * @return true if the ID was set and saved successfully, false otherwise.
     */
    bool setDeviceId(const char *id);

    /**
     * @brief Sets the Software Reset Status.
     * @param status The SoftwareResetStatus enum value to set.
     * @return true if the status was set and saved successfully, false otherwise.
     */
    bool setSoftwareResetStatus(SoftwareResetStatus status);

    /**
     * @brief Sets the weekly schedules.
     * @param schedules Array containing the schedules to set (must be size MAX_WEEKLY_SCHEDULES).
     * @return true if the schedules were set and saved successfully, false otherwise.
     */
    bool setWeeklySchedules(const WeeklySchedule schedules[MAX_WEEKLY_SCHEDULES]);

    /**
     * @brief Sets the Fill Dwell Time.
     * @param seconds The Fill Dwell Time in seconds.
     * @return true if the time was set and saved successfully, false otherwise.
     */
    bool setFillDwellTime(uint32_t seconds);

    /**
     * @brief Sets the LTE Timeout.
     * @param seconds The LTE Timeout in seconds.
     * @return true if the timeout was set and saved successfully, false otherwise.
     */
    bool setLteTimeout(uint32_t seconds);

    /**
     * @brief Sets the Fill Alarm Delta percentage.
     * @param delta The Fill Alarm Delta (0-100). Value will be clamped.
     * @return true if the delta was set and saved successfully, false otherwise.
     */
    bool setFillAlarmDelta(uint8_t delta);

    /**
     * @brief Sets the Post Dwell Time.
     * @param seconds The Post Dwell Time in seconds.
     * @return true if the time was set and saved successfully, false otherwise.
     */
    bool setPostDwellTime(uint32_t seconds);

    /**
     * @brief Sets the BLE ADV interval (ms). REQ-SHADOW.
     * Values below CONFIG_LOGI_MIN_BLE_ADV_TIME_MS are rejected.
     */
    bool setBleAdvTime(uint32_t milliseconds);
    bool setEventPosts(bool enabled);
    bool setMqttScheduledPost(const char *schedule);

private:
    // --- Private Members ---
    ISettingsService &_settingsService; // Reference to the injected service
    bool _initialized;

    // Member variables to hold current settings
    char _deviceId[DEVICE_ID_BUFFER_SIZE];
    char _softwareResetStatusStr[SW_RESET_STATUS_BUFFER_SIZE];
    WeeklySchedule _weeklySchedules[MAX_WEEKLY_SCHEDULES];
    uint32_t _fillDwellTime;
    uint32_t _lteTimeout;
    uint8_t _fillAlarmDelta;
    uint32_t _postDwellTime;
    uint32_t _bleAdvTime;       // REQ-SHADOW: BLE adv interval (ms) for prov mode
    bool _eventPosts;
    char _mqttScheduledPost[MQTT_SCHEDULED_POST_BUFFER_SIZE];
    uint16_t _deviceIdValidFlag;
    uint16_t _defaultsAppliedFlag;

    // NVS Keys (using const char* for compatibility with C APIs)
    static const char *KEY_DEVICE_ID;
    static const char *KEY_SW_RESET_STATUS;
    static const char *KEY_WEEKLY_SCHED;
    static const char *KEY_FILL_DWELL_T;
    static const char *KEY_LTE_TIMEOUT;
    static const char *KEY_FILL_ALARM_D;
    static const char *KEY_POST_DWELL_T;
    static const char *KEY_BLE_ADV_T;       // REQ-SHADOW
    static const char *KEY_EVENT_POSTS;
    static const char *KEY_MQTT_SCHED_POST;
    static const char *KEY_DEVICE_ID_VALID;
    static const char *KEY_DEFAULTS_SET;

    // Logger Tag
    static const char *TAG;

    // --- Private Helper Methods ---

    /**
     * @brief Loads a setting of a basic type (uint32_t, uint16_t, uint8_t) from NVS.
     * Uses the BLOB GetValue/SetValue methods.
     * If not found, applies the default value and saves it back.
     * @tparam T The data type (uint32_t, uint16_t, uint8_t).
     * @param key The NVS key.
     * @param valueRef Reference to the member variable to store the value.
     * @param defaultValue The default value to apply if not found.
     * @return true if loaded or default applied successfully, false on error.
     */
    template <typename T>
    bool loadOrDefaultBasicTypeSetting(const char *key, T &valueRef, T defaultValue);

    /**
     * @brief Loads the Device ID string (as blob) from NVS.
     * If not found, applies the default ID and saves it back.
     * @return true if loaded or default applied successfully, false on error.
     */
    bool loadOrDefaultDeviceId();

    /**
     * @brief Loads the Software Reset Status string (as blob) from NVS.
     * If not found, applies the default status ("NONE") and saves it back.
     * @return true if loaded or default applied successfully, false on error.
     */
    bool loadOrDefaultSwResetStatus();

    /**
     * @brief Loads the Weekly Schedules blob from NVS.
     * If not found, applies the default schedule and saves it back.
     * @return true if loaded or default applied successfully, false on error.
     */
    bool loadOrDefaultWeeklySchedules();
    bool loadOrDefaultMqttScheduledPost();

    /**
     * @brief Converts the internal _softwareResetStatusStr to the enum value.
     * @return The corresponding SoftwareResetStatus enum.
     */
    SoftwareResetStatus getSoftwareResetStatusFromString() const;

    /**
     * @brief Converts the SoftwareResetStatus enum to its string representation.
     * @param status The enum value.
     * @return Pointer to the corresponding static string. Returns "NONE" for invalid input.
     */
    static const char *getSoftwareResetStatusString(SoftwareResetStatus status);
};

#endif // DEVICE_SETTINGS_H
