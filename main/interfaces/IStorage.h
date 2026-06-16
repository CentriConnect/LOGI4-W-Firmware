#ifndef ISTORAGE_H
#define ISTORAGE_H

#include <stddef.h> // For size_t
#include <stdbool.h> // For bool type

/// <summary>
/// Interface for Non-Volatile Storage operations.
/// Defines the contract for saving and loading data persistently.
/// </summary>
class IStorage {
public:
    /// <summary>
    /// Virtual destructor is crucial for interfaces.
    /// </summary>
    virtual ~IStorage() = default;

    /// <summary>
    /// Initializes the storage system. Must be called before other operations.
    /// </summary>
    /// <returns>true if initialization was successful, false otherwise.</returns>
    virtual bool Init() = 0;

    /// <summary>
    /// Saves a binary blob (raw data) associated with a key.
    /// </summary>
    /// <param name="key">The null-terminated C-string identifier for the data.</param>
    /// <param name="data">Pointer to the data buffer to save.</param>
    /// <param name="length">The number of bytes to save from the buffer.</param>
    /// <returns>true if saving was successful, false otherwise.</returns>
    virtual bool SaveBlob(const char* key, const void* data, size_t length) = 0;

    /// <summary>
    /// Loads a binary blob associated with a key.
    /// </summary>
    /// <param name="key">The null-terminated C-string identifier for the data.</param>
    /// <param name="buffer">Pointer to the buffer where loaded data will be stored.</param>
    /// <param name="bufferSize">The maximum number of bytes the buffer can hold.</param>
    /// <returns>The actual number of bytes loaded into the buffer, or 0 if the key
    /// was not found or an error occurred. Returns bufferSize if the stored data
    /// exceeded bufferSize (indicating truncation might have occurred depending on implementation).</returns>
    virtual size_t LoadBlob(const char* key, void* buffer, size_t bufferSize) = 0;

    /// <summary>
    /// Saves a null-terminated C-string value associated with a key.
    /// </summary>
    /// <param name="key">The null-terminated C-string identifier for the string.</param>
    /// <param name="value">The null-terminated C-string value to save.</param>
    /// <returns>true if saving was successful, false otherwise.</returns>
    virtual bool SaveString(const char* key, const char* value) = 0;

    /// <summary>
    /// Loads a null-terminated C-string value associated with a key.
    /// </summary>
    /// <param name="key">The null-terminated C-string identifier for the string.</param>
    /// <param name="buffer">Pointer to the character buffer where the loaded string will be stored.</param>
    /// <param name="bufferSize">The size of the buffer (including space for the null terminator).</param>
    /// <returns>true if loading was successful (key was found and string fit), false otherwise.
    /// The buffer will be null-terminated on success. It might be empty or null-terminated
    /// on failure depending on implementation.</returns>
    virtual bool LoadString(const char* key, char* buffer, size_t bufferSize) = 0;

    /// <summary>
    /// Commits any pending changes to the persistent storage.
    /// Depending on the implementation, changes might be buffered until committed.
    /// </summary>
    /// <returns>true if commit was successful, false otherwise.</returns>
    virtual bool Commit() = 0;
};

#endif // ISTORAGE_H