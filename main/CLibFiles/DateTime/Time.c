/*
 * Time.c
 *
 *  Created on: Jan 21, 2021
 *      Author: tom.zagotta
 */

#include "Time.h"

#include <stdio.h>

void Time_Clear(Time* value)
{
    if (value != NULL)
    {
        value->Hour = 0;
        value->Minute = 0;
        value->Second = 0;
        value->Millisecond = 0;
    }
}

bool Time_Initialize(Time* value, hour_t hour, minute_t minute, second_t second, millisecond_t millisecond)
{
    if ((value == NULL) || (hour >= 24) || (minute >= 60) || (second >= 60) || (millisecond >= 1000))
        return false;

    value->Hour = hour;
    value->Minute = minute;
    value->Second = second;
    value->Millisecond = millisecond;
    return true;
}

bool Time_IsValid(const Time* value)
{
    return
        (value != NULL) &&
        (value->Hour < 24) &&
        (value->Minute < 60) &&
        (value->Second < 60) &&
        (value->Millisecond < 1000);
}

bool Time_FromByteArray(Time* value, const byte* buffer, size_t bufferSize)
{
    // Check the parameter values
    if ((value == NULL) || (buffer == NULL) || (bufferSize < TIME_BINARY_SIZE))
        return false;

    // Copy the data from the buffer into the date (millisecond is little endian)
    value->Hour = (hour_t)buffer[0];
    value->Minute = (minute_t)buffer[1];
    value->Second = (second_t)buffer[2];
    value->Millisecond = (millisecond_t)((uint16_t)(buffer[4]) << 8 | buffer[3]);

    // Return whether the new date is valid
    return Time_IsValid(value);
}

bool Time_ToByteArray(const Time* value, byte* buffer, size_t bufferSize)
{
    // Check the parameter values
    if ((value == NULL) || (buffer == NULL) || (bufferSize < TIME_BINARY_SIZE))
        return false;

    // Copy the date value into the buffer (millisecond is little endian)
    buffer[0] = (byte)(value->Hour);
    buffer[1] = (byte)(value->Minute);
    buffer[2] = (byte)(value->Second);
    buffer[3] = (byte)(value->Millisecond);
    buffer[4] = (byte)(value->Millisecond >> 8);
    return true;
}

bool Time_ToIsoString(const Time* value, char* buffer, size_t bufferSize)
{
    if ((value == NULL) || (buffer == NULL) || (bufferSize < TIME_ISO_STRING_SIZE))
        return false;

    sprintf(buffer, "%02u:%02u:%02u.%03u", value->Hour, value->Minute, value->Second, value->Millisecond);
    return true;
}
