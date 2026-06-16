/*
 * Version.h
 *
 *  Created on: Jan 21, 2021
 *      Author: tom.zagotta
 */

#ifndef VERSION_H_
#define VERSION_H_

#include "System.h"

/// Structure that holds a version number
typedef struct
{
    uint8_t Major;
    uint8_t Minor;
    uint8_t Revision;
} Version;

/// Binary buffer size
#define VERSION_BINARY_SIZE (3*sizeof(byte))

/// Maximum formatted string buffer size (includes NUL terminator)
#define VERSION_MAX_STRING_SIZE sizeof("000.000.000")

/// Initialize a version.
void Version_Initialize(Version* value, const uint8_t major, const uint8_t minor, const uint8_t revision);

/// Clear the fields within a version.
void Version_Clear(Version* value);

/// Convert a byte array to a version struct
bool Version_FromByteArray(Version* value, const byte* buffer, size_t bufferSize);

/// Convert a version struct to a byte array
bool Version_ToByteArray(const Version* value, byte* buffer, size_t bufferSize);

/// Format the version into a string, e.g., "1.2" or "1.2.3" with NUL terminator
bool Version_ToString(const Version* value, char* buffer, size_t bufferSize);

#endif /* VERSION_H_ */
