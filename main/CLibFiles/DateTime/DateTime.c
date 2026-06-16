/*
 * DateTime.c
 *
 *  Created on: Jan 21, 2021
 *      Author: tom.zagotta
 */

#include "DateTime.h"
#include <time.h>
#include <stdio.h>

void DateTime_Clear(DateTime* value)
{
    if (value != NULL)
    {
        Date_Clear(&value->date);
        Time_Clear(&value->time);
    }
}

bool DateTime_Initialize(DateTime* value, year_t year, month_t month, day_t day, hour_t hour, minute_t minute, second_t second, millisecond_t millisecond)
{
    return
        Date_Initialize(&value->date, year, month, day) &&
        Time_Initialize(&value->time, hour, minute, second, millisecond);
}

bool DateTime_FromUnix(DateTime* value, int64_t unixTime)
{
    time_t epoch = unixTime;
    struct tm timestamp = *localtime(&epoch);  
    return DateTime_Initialize(value, 1900 + timestamp.tm_year, 1 + timestamp.tm_mon, timestamp.tm_mday, timestamp.tm_hour, timestamp.tm_min, timestamp.tm_sec, 0);
}

void DateTime_AddMilliseconds(DateTime* value, millisecond_t millisecondsValue)
{
    if ((value != NULL) && DateTime_IsValid(value))
    {
    const millisecond_t millisecond = value->time.Millisecond + millisecondsValue;
        value->time.Millisecond = millisecond % 1000;

        const second_t second = millisecond / 1000 + value->time.Second;
        value->time.Second = second % 60;

        const minute_t minute = second / 60 + value->time.Minute;
        value->time.Minute = minute % 60;

        const hour_t hour = minute / 60 + value->time.Hour;
        value->time.Hour = hour % 24;

        const day_t day = hour / 24 + value->date.Day;
        const day_t daysInMonth = Date_DaysInMonth(value->date.Year, value->date.Month);
        value->date.Day = ((day - 1) % daysInMonth) + 1;

        const month_t month = (day - 1) / daysInMonth + value->date.Month;
        value->date.Month = ((month - 1) % 12) + 1;

        value->date.Year += (month - 1) / 12;
    }
}

bool DateTime_IsEmpty(const DateTime* value)
{
    return (value != NULL) && Date_IsEmpty(&value->date);
}

bool DateTime_IsValid(const DateTime* value)
{
    return (value != NULL) && Date_IsValid(&value->date) && Time_IsValid(&value->time);
}

bool DateTime_FromByteArray(DateTime* value, const byte* buffer, size_t bufferSize)
{
    return
        (value != NULL) &&
        (buffer != NULL) &&
        (bufferSize >= DATETIME_BINARY_SIZE) &&
        Date_FromByteArray(&value->date, buffer, bufferSize) &&
        Time_FromByteArray(&value->time, buffer+DATE_BINARY_SIZE, bufferSize-DATE_BINARY_SIZE);
}

bool DateTime_ToByteArray(const DateTime* value, byte* buffer, size_t bufferSize)
{
    return
        (value != NULL) &&
        (buffer != NULL) &&
        (bufferSize >= DATETIME_BINARY_SIZE) &&
        Date_ToByteArray(&value->date, buffer, bufferSize) &&
        Time_ToByteArray(&value->time, buffer+DATE_BINARY_SIZE, bufferSize-DATE_BINARY_SIZE);
}

bool DateTime_ToIsoString(const DateTime* value, char* buffer, size_t bufferSize)
{
    if ((value == NULL) || (buffer == NULL) || (bufferSize < DATETIME_ISO_STRING_SIZE))
        return false;

    if (!Date_ToIsoString(&value->date, buffer, bufferSize))
        return false;
    buffer[DATE_ISO_STRING_SIZE-sizeof(char)] = 'T';

    return Time_ToIsoString(&value->time, buffer+DATE_ISO_STRING_SIZE, bufferSize-DATE_ISO_STRING_SIZE);
}
