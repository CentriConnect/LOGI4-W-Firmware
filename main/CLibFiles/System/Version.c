/*
 * Version.c
 *
 *  Created on: Jan 21, 2021
 *      Author: tom.zagotta
 */

#include "Version.h"

#include <stdio.h>

void Version_Initialize(Version* value, const uint8_t major, const uint8_t minor, const uint8_t revision)
{
	if (value != NULL)
	{
        value->Major = major;
        value->Minor = minor;
        value->Revision = revision;
	}
}

void Version_Clear(Version* value)
{
	if (value != NULL)
	{
        value->Major = 0;
        value->Minor = 0;
        value->Revision = 0;
	}
}

bool Version_FromByteArray(Version* value, const byte* buffer, size_t bufferSize)
{
    if ((value == NULL) || (buffer == NULL) || (bufferSize < VERSION_BINARY_SIZE))
        return false;

    value->Major = (uint8_t)buffer[0];
    value->Minor = (uint8_t)buffer[1];
    value->Revision = (uint8_t)buffer[2];
    return true;
}

bool Version_ToByteArray(const Version* value, byte* buffer, size_t bufferSize)
{
    if ((value == NULL) || (buffer == NULL) || (bufferSize < VERSION_BINARY_SIZE))
        return false;

    buffer[0] = (byte)value->Major;
    buffer[1] = (byte)value->Minor;
    buffer[2] = (byte)value->Revision;
    return true;
}

bool Version_ToString(const Version* value, char* buffer, size_t bufferSize)
{
    if ((value == NULL) || (buffer == NULL) || (bufferSize < VERSION_MAX_STRING_SIZE))
        return false;

    sprintf(buffer, (value->Revision == 0) ? "%u.%u" : "%u.%u.%u", value->Major, value->Minor, value->Revision);
    return true;
}
