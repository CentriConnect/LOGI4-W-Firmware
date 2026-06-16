#ifndef ESP_NVS_STORAGE_H
#define ESP_NVS_STORAGE_H

#include "interfaces/IStorage.h" // Include the interface definition
#include "nvs_flash.h"         // Include IDF NVS headers
#include "nvs.h"

/// <summary>
/// Concrete implementation of the IStorage interface using ESP-IDF's NVS (Non-Volatile Storage).
/// </summary>
class EspNvsStorage : public IStorage {
    nvs_handle_t _handle;     // Handle obtained from nvs_open
    const char* _namespace; // NVS namespace used by this instance
    bool _initialized;      // Flag indicating if init() was successful

    // Private tag for logging
    static const char* TAG;

public:
    /// <summary>
    /// Constructor for EspNvsStorage.
    /// </summary>
    /// <param name="storage_namespace">The NVS namespace to use. Defaults to "app_config".</param>
    EspNvsStorage(const char* storage_namespace = "app_config");

    /// <summary>
    /// Destructor. Closes the NVS handle.
    /// </summary>
    virtual ~EspNvsStorage();

    // --- Delete copy constructor and assignment operator ---
    // Prevents accidental copying which could misuse the NVS handle.
    EspNvsStorage(const EspNvsStorage&) = delete;
    EspNvsStorage& operator=(const EspNvsStorage&) = delete;

    /// <summary>
    /// Initializes the NVS storage. Must be called before other operations.
    /// Handles potential NVS partition errors (e.g., by erasing if needed).
    /// </summary>
    /// <returns>true if initialization was successful, false otherwise.</returns>
    bool Init() override;

    /// <summary>
    /// Saves a binary blob (raw data) associated with a key to NVS.
    /// </summary>
    /// <param name="key">The null-terminated C-string identifier for the data.</param>
    /// <param name="data">Pointer to the data buffer to save.</param>
    /// <param name="length">The number of bytes to save from the buffer.</param>
    /// <returns>true if saving was successful, false otherwise.</returns>
    bool SaveBlob(const char* key, const void* data, size_t length) override;

    /// <summary>
    /// Loads a binary blob associated with a key from NVS.
    /// If the buffer is NULL or bufferSize is 0, it returns the required size without reading.
    /// </summary>
    /// <param name="key">The null-terminated C-string identifier for the data.</param>
    /// <param name="buffer">Pointer to the buffer where loaded data will be stored.</param>
    /// <param name="bufferSize">The maximum number of bytes the buffer can hold.</param>
    /// <returns>The actual number of bytes loaded into the buffer. Returns 0 if the key
    /// was not found or an error occurred. Returns the required size if buffer is NULL or bufferSize is 0.
    /// Note: If the returned size equals bufferSize, the data might have been truncated if the
    /// actual stored size was larger.</returns>
    size_t LoadBlob(const char* key, void* buffer, size_t bufferSize) override;

    /// <summary>
    /// Saves a null-terminated C-string value associated with a key to NVS.
    /// </summary>
    /// <param name="key">The null-terminated C-string identifier for the string.</param>
    /// <param name="value">The null-terminated C-string value to save.</param>
    /// <returns>true if saving was successful, false otherwise.</returns>
    bool SaveString(const char* key, const char* value) override;

    /// <summary>
    /// Loads a null-terminated C-string value associated with a key from NVS.
    /// Ensures the loaded string fits the buffer and is null-terminated.
    /// </summary>
    /// <param name="key">The null-terminated C-string identifier for the string.</param>
    /// <param name="buffer">Pointer to the character buffer where the loaded string will be stored.</param>
    /// <param name="bufferSize">The size of the buffer (including space for the null terminator).</param>
    /// <returns>true if loading was successful (key was found and string fit), false otherwise.
    /// The buffer will be null-terminated on success. buffer[0] will be '\0' if the key
    /// is not found or if the string doesn't fit.</returns>
    bool LoadString(const char* key, char* buffer, size_t bufferSize) override;

    /// <summary>
    /// Commits any pending changes to the NVS partition.
    /// </summary>
    /// <returns>true if commit was successful, false otherwise.</returns>
    bool Commit() override;
};

#endif // ESP_NVS_STORAGE_H