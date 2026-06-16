#include "NvsSettingsService.h" // Adjust path if needed
#include <cstring>              // For memcpy
#include "esp_log.h"
#include <vector> // Include vector for string loading buffer
#include "sdkconfig.h"

// Define static const member
const char *NvsSettingsService::TAG = "NvsSettingsSvc";

// --- Constructor ---
NvsSettingsService::NvsSettingsService(IStorage &storage) : _storage(storage),
                                                            _initialized(false)
{
    ESP_LOGD(TAG, "NvsSettingsService created.");
}

// --- Initialization ---
bool NvsSettingsService::Initialize()
{
    if (_initialized)
    {
        ESP_LOGD(TAG, "Already initialized.");
        return true;
    }
    ESP_LOGI(TAG, "Initializing...");
    _initialized = _storage.Init(); // Rely on underlying storage init
    if (!_initialized)
    {
        ESP_LOGE(TAG, "Underlying IStorage initialization failed!");
    }
    else
    {
        ESP_LOGI(TAG, "Initialization successful.");
    }
    return _initialized;
}

bool NvsSettingsService::IsInitialized() const
{
    return _initialized;
}

// --- Helper Templates (Internal Implementation Detail) ---
template <typename T>
bool NvsSettingsService::SaveBasicTypeAsBlob(const char *key, T value)
{
    if (!_initialized)
    {
        ESP_LOGE(TAG, "SaveBasicTypeAsBlob failed for key '%s': Service not initialized.", key);
        return false;
    }
    // Use the specific blob setter from the interface
    return SetValue(key, reinterpret_cast<const uint8_t *>(&value), sizeof(T));
}

template <typename T>
bool NvsSettingsService::LoadBasicTypeFromBlob(const char *key, T &value)
{
    if (!_initialized)
    {
        ESP_LOGE(TAG, "LoadBasicTypeFromBlob failed for key '%s': Service not initialized.", key);
        return false;
    }

    T temp_value{};
    // Use the specific blob getter from the interface
    size_t loaded_size = GetValue(key, reinterpret_cast<uint8_t *>(&temp_value), sizeof(T));

    if (loaded_size == sizeof(T))
    {
        value = temp_value;
        // ESP_LOGD(TAG, "LoadBasicTypeFromBlob: key='%s', success=true, size=%d", key, loaded_size);
        return true;
    }
    else
    {
        if (loaded_size == 0)
        {
            // ESP_LOGD(TAG, "LoadBasicTypeFromBlob: Key '%s' not found or load error.", key);
        }
        else
        {
            ESP_LOGW(TAG, "LoadBasicTypeFromBlob: Size mismatch for key '%s'. Expected %d, Got %d.", key, sizeof(T), loaded_size);
        }
        return false;
    }
}

// --- Setters Implementation ---
bool NvsSettingsService::SetValue(const char *key, int32_t value)
{
    return SaveBasicTypeAsBlob(key, value);
}

bool NvsSettingsService::SetValue(const char *key, uint32_t value)
{
    return SaveBasicTypeAsBlob(key, value);
}

bool NvsSettingsService::SetValue(const char *key, bool value)
{
    uint8_t val_u8 = value ? 1 : 0;
    return SaveBasicTypeAsBlob(key, val_u8); // Save bool as uint8_t blob
}

bool NvsSettingsService::SetValue(const char *key, float value)
{
    return SaveBasicTypeAsBlob(key, value);
}

bool NvsSettingsService::SetValue(const char *key, double value)
{
    return SaveBasicTypeAsBlob(key, value);
}

bool NvsSettingsService::SetValue(const char *key, const char *value)
{
    if (!_initialized)
    {
        ESP_LOGE(TAG, "SetValue(string) failed for key '%s': Service not initialized.", key);
        return false;
    }
    bool success = _storage.SaveString(key, value); // Use underlying SaveString
    if (!success)
    {
        ESP_LOGE(TAG, "IStorage::SaveString failed for key '%s'", key);
    }
    return success;
}

bool NvsSettingsService::SetValue(const char *key, const uint8_t *blob_data, size_t length)
{
    if (!_initialized)
    {
        ESP_LOGE(TAG, "SetValue(blob) failed for key '%s': Service not initialized.", key);
        return false;
    }
    bool success = _storage.SaveBlob(key, blob_data, length); // Pass through to underlying SaveBlob
    if (!success)
    {
        ESP_LOGE(TAG, "IStorage::SaveBlob failed for key '%s'", key);
    }
    return success;
}

// --- Getters Implementation ---
bool NvsSettingsService::GetValue(const char *key, int32_t &value)
{
    return LoadBasicTypeFromBlob(key, value);
}

bool NvsSettingsService::GetValue(const char *key, uint32_t &value)
{
    return LoadBasicTypeFromBlob(key, value);
}

bool NvsSettingsService::GetValue(const char *key, bool &value)
{
    uint8_t val_u8 = 0;
    if (LoadBasicTypeFromBlob(key, val_u8)) // Load bool as uint8_t blob
    {
        value = (val_u8 != 0);
        return true;
    }
    return false;
}

bool NvsSettingsService::GetValue(const char *key, float &value)
{
    return LoadBasicTypeFromBlob(key, value);
}

bool NvsSettingsService::GetValue(const char *key, double &value)
{
    return LoadBasicTypeFromBlob(key, value);
}

bool NvsSettingsService::GetValue(const char *key, char *buffer, size_t buffer_size)
{
    if (!_initialized)
    {
        ESP_LOGE(TAG, "GetValue(string) failed for key '%s': Service not initialized.", key);
        if (buffer && buffer_size > 0)
            buffer[0] = '\0';
        return false;
    }
    // Use underlying LoadString
    return _storage.LoadString(key, buffer, buffer_size);
}

size_t NvsSettingsService::GetValue(const char *key, uint8_t *blob_buffer, size_t buffer_size)
{
    if (!_initialized)
    {
        ESP_LOGE(TAG, "GetValue(blob) failed for key '%s': Service not initialized.", key);
        return 0;
    }
    // Pass through to underlying LoadBlob
    return _storage.LoadBlob(key, blob_buffer, buffer_size);
}

// --- Commit Implementation ---
bool NvsSettingsService::Commit()
{
    if (!_initialized)
    {
        ESP_LOGE(TAG, "Commit failed: Service not initialized.");
        return false;
    }
    bool success = _storage.Commit(); // Pass through to underlying Commit
    if (!success)
    {
        ESP_LOGE(TAG, "IStorage::Commit failed.");
    }
    else
    {
        // ESP_LOGD(TAG, "Commit successful.");
    }
    return success;
}
