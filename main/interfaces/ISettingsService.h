/**
 * @file ISettingsService.h
 * @brief Interface for a generic, portable key-value settings storage service.
 */
#ifndef ISETTINGS_SERVICE_H
#define ISETTINGS_SERVICE_H

#include "interfaces/IStorage.h" // Depends on the basic storage interface
// #include <string> // REMOVED std::string dependency
#include <cstdint>  // For specific integer types
#include <stddef.h> // For size_t

/**
 * @brief Provides a portable key-value interface on top of the raw IStorage HAL.
 *
 * This interface abstracts the underlying storage mechanism (like NVS or filesystem)
 * allowing application code (like DeviceSettings) to work with settings in a
 * type-safe manner without depending on the specific storage implementation.
 * Uses const char* for keys to avoid std::string.
 */
class ISettingsService
{
public:
    /// <summary>
    /// Virtual destructor.
    /// </summary>
    virtual ~ISettingsService() = default;

    /// <summary>
    /// Initializes the settings service and the underlying storage.
    /// Must be called before other operations.
    /// </summary>
    /// <returns>true if initialization was successful, false otherwise.</returns>
    virtual bool Initialize() = 0;

    /// <summary>
    /// Checks if the service and underlying storage are initialized.
    /// </summary>
    /// <returns>true if initialized, false otherwise.</returns>
    virtual bool IsInitialized() const = 0;

    // --- Setters ---

    /// <summary>
    /// Saves an int32_t value associated with a key.
    /// </summary>
    /// <param name="key">The setting key (null-terminated C-string).</param>
    /// <param name="value">The value to save.</param>
    /// <returns>true on success, false otherwise.</returns>
    virtual bool SetValue(const char *key, int32_t value) = 0;

    /// <summary>
    /// Saves a uint32_t value associated with a key.
    /// </summary>
    /// <param name="key">The setting key (null-terminated C-string).</param>
    /// <param name="value">The value to save.</param>
    /// <returns>true on success, false otherwise.</returns>
    virtual bool SetValue(const char *key, uint32_t value) = 0;

    /// <summary>
    /// Saves a boolean value associated with a key.
    /// (Internally stored as uint8_t).
    /// </summary>
    /// <param name="key">The setting key (null-terminated C-string).</param>
    /// <param name="value">The value to save.</param>
    /// <returns>true on success, false otherwise.</returns>
    virtual bool SetValue(const char *key, bool value) = 0;

    /// <summary>
    /// Saves a float value associated with a key.
    /// </summary>
    /// <param name="key">The setting key (null-terminated C-string).</param>
    /// <param name="value">The value to save.</param>
    /// <returns>true on success, false otherwise.</returns>
    virtual bool SetValue(const char *key, float value) = 0;

    /// <summary>
    /// Saves a double value associated with a key.
    /// </summary>
    /// <param name="key">The setting key (null-terminated C-string).</param>
    /// <param name="value">The value to save.</param>
    /// <returns>true on success, false otherwise.</returns>
    virtual bool SetValue(const char *key, double value) = 0;

    /// <summary>
    /// Saves a C-string value associated with a key.
    /// </summary>
    /// <param name="key">The setting key (null-terminated C-string).</param>
    /// <param name="value">The null-terminated C-string to save.</param>
    /// <returns>true on success, false otherwise.</returns>
    virtual bool SetValue(const char *key, const char *value) = 0;

    /// <summary>
    /// Saves a raw binary blob associated with a key.
    /// </summary>
    /// <param name="key">The setting key (null-terminated C-string).</param>
    /// <param name="blob_data">Pointer to the data buffer to save.</param>
    /// <param name="length">The number of bytes to save.</param>
    /// <returns>true on success, false otherwise.</returns>
    virtual bool SetValue(const char *key, const uint8_t *blob_data, size_t length) = 0;

    // --- Getters ---

    /// <summary>
    /// Gets an int32_t value associated with a key.
    /// </summary>
    /// <param name="key">The setting key (null-terminated C-string).</param>
    /// <param name="value">Reference to store the retrieved value. Unchanged on failure.</param>
    /// <returns>true if the key was found and the value was retrieved successfully, false otherwise.</returns>
    virtual bool GetValue(const char *key, int32_t &value) = 0;

    /// <summary>
    /// Gets a uint32_t value associated with a key.
    /// </summary>
    /// <param name="key">The setting key (null-terminated C-string).</param>
    /// <param name="value">Reference to store the retrieved value. Unchanged on failure.</param>
    /// <returns>true if the key was found and the value was retrieved successfully, false otherwise.</returns>
    virtual bool GetValue(const char *key, uint32_t &value) = 0;

    /// <summary>
    /// Gets a boolean value associated with a key.
    /// </summary>
    /// <param name="key">The setting key (null-terminated C-string).</param>
    /// <param name="value">Reference to store the retrieved value. Unchanged on failure.</param>
    /// <returns>true if the key was found and the value was retrieved successfully, false otherwise.</returns>
    virtual bool GetValue(const char *key, bool &value) = 0;

    /// <summary>
    /// Gets a float value associated with a key.
    /// </summary>
    /// <param name="key">The setting key (null-terminated C-string).</param>
    /// <param name="value">Reference to store the retrieved value. Unchanged on failure.</param>
    /// <returns>true if the key was found and the value was retrieved successfully, false otherwise.</returns>
    virtual bool GetValue(const char *key, float &value) = 0;

    /// <summary>
    /// Gets a double value associated with a key.
    /// </summary>
    /// <param name="key">The setting key (null-terminated C-string).</param>
    /// <param name="value">Reference to store the retrieved value. Unchanged on failure.</param>
    /// <returns>true if the key was found and the value was retrieved successfully, false otherwise.</returns>
    virtual bool GetValue(const char *key, double &value) = 0;

    /// <summary>
    /// Gets a C-string value associated with a key.
    /// </summary>
    /// <param name="key">The setting key (null-terminated C-string).</param>
    /// <param name="buffer">Buffer to store the retrieved null-terminated string.</param>
    /// <param name="buffer_size">Size of the provided buffer.</param>
    /// <returns>true if the key was found and the string fit in the buffer, false otherwise.</returns>
    virtual bool GetValue(const char *key, char *buffer, size_t buffer_size) = 0;

    /// <summary>
    /// Gets a raw binary blob associated with a key.
    /// If the provided buffer is sufficient, data is loaded into it.
    /// If blob_buffer is nullptr or buffer_size is 0, it only returns the required size.
    /// </summary>
    /// <param name="key">The setting key (null-terminated C-string).</param>
    /// <param name="blob_buffer">Pointer to the buffer to store the loaded data (can be nullptr to query size).</param>
    /// <param name="buffer_size">The size of the provided buffer.</param>
    /// <returns>size_t The actual size of the stored blob.
    /// Returns 0 if the key is not found or an error occurs during size query/load.
    /// If blob_buffer is non-nullptr and buffer_size is less than the required size,
    /// it should return 0 and not load data.
    /// </returns>
    virtual size_t GetValue(const char *key, uint8_t *blob_buffer, size_t buffer_size) = 0;

    /// <summary>
    /// Commits any pending changes to the persistent storage via IStorage.
    /// The necessity of calling Commit depends on the underlying IStorage implementation.
    /// </summary>
    /// <returns>true if commit was successful or not needed, false on failure.</returns>
    virtual bool Commit() = 0;

    // Optional: Add methods like Exists(key), Delete(key) if needed
};

#endif // ISETTINGS_SERVICE_H
