/**
 * @file NvsSettingsService.h
 * @brief Concrete implementation of ISettingsService using ESP-IDF NVS via IStorage.
 */
#ifndef NVS_SETTINGS_SERVICE_H
#define NVS_SETTINGS_SERVICE_H

#include "interfaces/ISettingsService.h" // Include the interface
#include "interfaces/IStorage.h"         // Include the dependency interface
// #include <string> // No longer needed
#include "esp_log.h"
#include <vector> // Include vector for string loading buffer

/**
 * @brief Implements the ISettingsService interface using an IStorage instance (typically EspNvsStorage).
 *
 * Handles storing/retrieving basic data types using the underlying IStorage Save/Load Blob/String methods.
 */
class NvsSettingsService : public ISettingsService
{
public:
    /// <summary>
    /// Constructor.
    /// </summary>
    /// <param name="storage">A reference to an initialized IStorage implementation (e.g., EspNvsStorage).</param>
    NvsSettingsService(IStorage &storage);

    /// <summary>
    /// Virtual destructor.
    /// </summary>
    virtual ~NvsSettingsService() = default;

    // --- ISettingsService Implementation ---

    /// <summary>
    /// Initializes the settings service by initializing the underlying storage.
    /// </summary>
    /// <returns>true on success, false otherwise.</returns>
    bool Initialize() override;

    /// <summary>
    /// Checks if the service is initialized.
    /// </summary>
    /// <returns>true if initialized, false otherwise.</returns>
    bool IsInitialized() const override;

    /// <summary>
    /// Saves an int32_t value associated with a key.
    /// </summary>
    bool SetValue(const char *key, int32_t value) override;

    /// <summary>
    /// Saves a uint32_t value associated with a key.
    /// </summary>
    bool SetValue(const char *key, uint32_t value) override;

    /// <summary>
    /// Saves a boolean value associated with a key.
    /// </summary>
    bool SetValue(const char *key, bool value) override;

    /// <summary>
    /// Saves a float value associated with a key.
    /// </summary>
    bool SetValue(const char *key, float value) override;

    /// <summary>
    /// Saves a double value associated with a key.
    /// </summary>
    bool SetValue(const char *key, double value) override;

    /// <summary>
    /// Saves a C-string value associated with a key.
    /// </summary>
    bool SetValue(const char *key, const char *value) override;

    /// <summary>
    /// Saves a raw binary blob associated with a key.
    /// </summary>
    bool SetValue(const char *key, const uint8_t *blob_data, size_t length) override;

    /// <summary>
    /// Gets an int32_t value associated with a key.
    /// </summary>
    bool GetValue(const char *key, int32_t &value) override;

    /// <summary>
    /// Gets a uint32_t value associated with a key.
    /// </summary>
    bool GetValue(const char *key, uint32_t &value) override;

    /// <summary>
    /// Gets a boolean value associated with a key.
    /// </summary>
    bool GetValue(const char *key, bool &value) override;

    /// <summary>
    /// Gets a float value associated with a key.
    /// </summary>
    bool GetValue(const char *key, float &value) override;

    /// <summary>
    /// Gets a double value associated with a key.
    /// </summary>
    bool GetValue(const char *key, double &value) override;

    /// <summary>
    /// Gets a C-string value associated with a key.
    /// </summary>
    bool GetValue(const char *key, char *buffer, size_t buffer_size) override;

    /// <summary>
    /// Gets a raw binary blob associated with a key.
    /// </summary>
    size_t GetValue(const char *key, uint8_t *blob_buffer, size_t buffer_size) override;

    /// <summary>
    /// Commits changes using the underlying storage layer.
    /// </summary>
    /// <returns>true on success, false otherwise.</returns>
    bool Commit() override;

private:
    IStorage &_storage;     ///< Reference to the underlying storage implementation.
    bool _initialized;      ///< Tracks initialization status.
    static const char *TAG; ///< Logger tag.

    /// <summary>
    /// Helper template to save basic types as blobs via IStorage.
    /// </summary>
    template <typename T>
    bool SaveBasicTypeAsBlob(const char *key, T value);

    /// <summary>
    /// Helper template to load basic types from blobs via IStorage.
    /// </summary>
    template <typename T>
    bool LoadBasicTypeFromBlob(const char *key, T &value);
};

#endif // NVS_SETTINGS_SERVICE_H
