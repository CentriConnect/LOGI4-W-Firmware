/*
 * DateTime.h
 *
 *  Created on: Jan 20, 2021
 *      Author: tom.zagotta
 */

#ifndef DATETIME_H_
#define DATETIME_H_

#include "Date.h"
#include "Time.h"

// Size of buffer required to hold binary representation
#define DATETIME_BINARY_SIZE (DATE_BINARY_SIZE+TIME_BINARY_SIZE)

/// Size of buffer required to hold ISO 8601 formatted string
// Note that DATE_ISO_STRING_SIZE allocates a character for the final NUL
// character, and this is replaced with a 'T' separator in datetime
#define DATETIME_ISO_STRING_SIZE (DATE_ISO_STRING_SIZE+TIME_ISO_STRING_SIZE)

// Date and Time structure
typedef struct
{
    Date date;
    Time time;
} DateTime;

void DateTime_Clear(DateTime* value);

bool DateTime_Initialize(DateTime* value, year_t year, month_t month, day_t day, hour_t hour, minute_t minute, second_t second, millisecond_t millisecond);

bool DateTime_FromUnix(DateTime* value, int64_t unixTime);

void DateTime_AddMilliseconds(DateTime* value, millisecond_t millisecondsValue);

bool DateTime_IsValid(const DateTime* value);

bool DateTime_IsEmpty(const DateTime* value);

bool DateTime_FromByteArray(DateTime* value, const byte* buffer, size_t bufferSize);

bool DateTime_ToByteArray(const DateTime* value, byte* buffer, size_t bufferSize);

bool DateTime_ToIsoString(const DateTime* value, char* buffer, size_t bufferSize);

#endif /* DATETIME_H_ */
