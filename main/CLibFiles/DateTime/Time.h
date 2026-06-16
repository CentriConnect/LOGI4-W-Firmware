/*
 * Time.h
 *
 *  Created on: Jan 21, 2021
 *      Author: tom.zagotta
 */

#ifndef TIME_H_
#define TIME_H_

#include "System.h"

// Basic data types for time
typedef uint8_t hour_t; // 0-23
typedef uint8_t minute_t; // 0-59
typedef uint8_t second_t; // 0-59
typedef uint16_t millisecond_t; // 0-999

// Binary buffer size
#define TIME_BINARY_SIZE (sizeof(hour_t)+sizeof(minute_t)+sizeof(second_t)+sizeof(millisecond_t))

// ISO 8601 formatted string buffer size (includes NUL terminator)
#define TIME_ISO_STRING_SIZE sizeof("00:00:00.000")

// Time structure
typedef struct
{
    hour_t Hour;
    minute_t Minute;
    second_t Second;
    millisecond_t Millisecond;
} Time;

void Time_Clear(Time* value);

bool Time_Initialize(Time* value, hour_t hour, minute_t minute, second_t second, millisecond_t millisecond);

bool Time_IsValid(const Time* value);

bool Time_FromByteArray(Time* value, const byte* buffer, size_t bufferSize);

bool Time_ToByteArray(const Time* value, byte* buffer, size_t bufferSize);

bool Time_ToIsoString(const Time* value, char* buffer, size_t bufferSize);

#endif /* TIME_H_ */
