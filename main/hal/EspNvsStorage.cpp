#include "EspNvsStorage.h"
#include "esp_log.h"
#include <string.h> // For strlen, strcmp if needed, though IDF functions handle null termination

// Define the static const char* TAG member
const char* EspNvsStorage::TAG = "EspNvsStorage";

EspNvsStorage::EspNvsStorage(const char* storage_namespace) :
    _handle(0),
    _namespace(storage_namespace),
    _initialized(false)
{}

EspNvsStorage::~EspNvsStorage() {
    if (_handle != 0) {
        nvs_close(_handle);
        _handle = 0; // Mark handle as closed
        _initialized = false;
         ESP_LOGI(TAG, "NVS handle closed for namespace '%s'", _namespace);
    }
}

bool EspNvsStorage::Init() {
    if (_initialized) {
        ESP_LOGI(TAG, "Already initialized.");
        return true;
    }

    ESP_LOGI(TAG, "Initializing NVS flash...");
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated or needs format, erase and retry
        ESP_LOGW(TAG, "NVS init failed (0x%X), erasing partition and retrying...", err);
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
         ESP_LOGE(TAG, "Failed to initialize NVS flash (0x%X)", err);
         return false;
    }
     ESP_LOGI(TAG, "NVS flash initialized successfully.");

    ESP_LOGI(TAG, "Opening NVS namespace '%s'...", _namespace);
    err = nvs_open(_namespace, NVS_READWRITE, &_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS namespace '%s' (0x%X)", _namespace, err);
        return false;
    }

    ESP_LOGI(TAG, "NVS namespace '%s' opened successfully.", _namespace);
    _initialized = true;
    return true;
}

bool EspNvsStorage::SaveBlob(const char* key, const void* data, size_t length) {
    if (!_initialized) {
        ESP_LOGE(TAG, "Save failed: NVS not initialized.");
        return false;
    }
    if (key == nullptr || data == nullptr) {
         ESP_LOGE(TAG, "Save failed: key or data pointer is null.");
         return false;
    }

    esp_err_t err = nvs_set_blob(_handle, key, data, length);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set blob for key '%s' (0x%X)", key, err);
        return false;
    }
    // ESP_LOGD(TAG, "Blob saved for key '%s' (length %d)", key, length); // Use Debug level
    return true;
}

size_t EspNvsStorage::LoadBlob(const char* key, void* buffer, size_t bufferSize) {
     if (!_initialized) {
        ESP_LOGE(TAG, "Load failed: NVS not initialized.");
        return 0;
    }
     if (key == nullptr) {
        ESP_LOGE(TAG, "Load failed: key pointer is null.");
        return 0;
    }

    size_t required_len = 0;
    esp_err_t err = nvs_get_blob(_handle, key, NULL, &required_len); // First call to get the size

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // ESP_LOGD(TAG, "Blob key '%s' not found.", key);
        return 0; // Key doesn't exist
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get blob size for key '%s' (0x%X)", key, err);
        return 0; // Other error
    }

    if (buffer == NULL || bufferSize == 0) {
        return required_len; // Caller only wanted the size
    }

    if (required_len == 0) {
        // ESP_LOGD(TAG, "Blob key '%s' exists but has zero length.", key);
        return 0; // Stored blob is empty
    }

    if (required_len > bufferSize) {
         ESP_LOGW(TAG, "Buffer too small for blob key '%s'. Required: %d, Available: %d. Will load truncated data.", key, required_len, bufferSize);
         // We will load only bufferSize bytes
         required_len = bufferSize; // Adjust length to read to fit buffer
         err = nvs_get_blob(_handle, key, buffer, &required_len);
         if (err != ESP_OK) {
             ESP_LOGE(TAG, "Failed to get truncated blob for key '%s' (0x%X)", key, err);
             return 0;
         }
         // Return bufferSize to indicate buffer was filled, caller knows it might be truncated
         return bufferSize;

    } else {
        // Buffer is large enough
        err = nvs_get_blob(_handle, key, buffer, &required_len);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to get blob for key '%s' (0x%X)", key, err);
            return 0;
        }
        // ESP_LOGD(TAG, "Blob loaded for key '%s' (length %d)", key, required_len);
        return required_len; // Return actual bytes read
    }
}


bool EspNvsStorage::SaveString(const char* key, const char* value) {
    if (!_initialized) {
        ESP_LOGE(TAG, "Save failed: NVS not initialized.");
        return false;
    }
     if (key == nullptr || value == nullptr) {
        ESP_LOGE(TAG, "Save failed: key or value pointer is null.");
        return false;
    }

    esp_err_t err = nvs_set_str(_handle, key, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set string for key '%s' (0x%X)", key, err);
        return false;
    }
    // ESP_LOGD(TAG, "String saved for key '%s'", key);
    return true;
}

bool EspNvsStorage::LoadString(const char* key, char* buffer, size_t bufferSize) {
    if (!_initialized) {
        ESP_LOGE(TAG, "Load failed: NVS not initialized.");
        if (buffer && bufferSize > 0) buffer[0] = '\0'; // Null-terminate on error
        return false;
    }
    if (key == nullptr || buffer == nullptr || bufferSize == 0) {
         ESP_LOGE(TAG, "Load failed: Invalid arguments (key=%p, buffer=%p, bufferSize=%d).", key, buffer, bufferSize);
         if (buffer && bufferSize > 0) buffer[0] = '\0'; // Null-terminate on error
         return false;
    }

    size_t required_len = 0;
    esp_err_t err = nvs_get_str(_handle, key, NULL, &required_len); // First call to get the size

    if (err == ESP_ERR_NVS_NOT_FOUND) {
        // ESP_LOGD(TAG, "String key '%s' not found.", key);
        buffer[0] = '\0'; // Ensure buffer is empty string representation
        return false; // Key doesn't exist
    } else if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get string size for key '%s' (0x%X)", key, err);
        buffer[0] = '\0';
        return false; // Other error
    }

    if (required_len > bufferSize) {
         ESP_LOGE(TAG, "Buffer too small for string key '%s'. Required: %d (incl. null), Available: %d.", key, required_len, bufferSize);
         buffer[0] = '\0'; // Ensure buffer is empty string representation
         return false; // String doesn't fit
    }

    // Buffer is large enough, load the string
    err = nvs_get_str(_handle, key, buffer, &required_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get string for key '%s' (0x%X)", key, err);
        buffer[0] = '\0';
        return false;
    }

    // nvs_get_str should null-terminate, but belt-and-suspenders
    // buffer[required_len - 1] = '\0'; // Not needed if required_len includes null term

    // ESP_LOGD(TAG, "String loaded for key '%s'", key);
    return true;
}

bool EspNvsStorage::Commit() {
    if (!_initialized) {
        ESP_LOGE(TAG, "Commit failed: NVS not initialized.");
        return false;
    }
    esp_err_t err = nvs_commit(_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS changes (0x%X)", err);
        return false;
    }
     ESP_LOGI(TAG, "NVS changes committed.");
    return true;
}