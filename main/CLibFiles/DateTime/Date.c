/*
 * Date.c
 *
 *  Created on: Jan 21, 2021
 *      Author: tom.zagotta
 */

#include "Date.h"

#include <stdio.h>

void Date_Clear(Date* value)
{
    if (value != NULL)
    {
        value->Year = 0;
        value->Month = 0;
        value->Day = 0;
    }
}

bool Date_Initialize(Date* value, year_t year, month_t month, day_t day)
{
    if ((value == NULL) ||
        (year < DATE_MIN_YEAR) || (year > DATE_MAX_YEAR) ||
        (month < 1) || (month > 12) ||
        (day < 1) || (day > Date_DaysInMonth(year, month)))
        return false;

    value->Year = year;
    value->Month = month;
    value->Day = day;
    return true;
}

bool Date_IsLeapYear(const year_t year)
{
    if ((year % 4) != 0)
        return false;
    else if ((year % 100) != 0)
        return true;
    else if ((year % 400) != 0)
        return false;
    else
        return true;
}

day_t Date_DaysInMonth(const year_t year, const month_t month)
{
	// Invalid month
    if ((month < 1) || (month > 12))
        return 0;

	// February - leap year or not
    else if (month == 2)
        return Date_IsLeapYear(year) ? 29 : 28;

	// Odd months before August, and even months after July, have 31 days
    else if (((month <= 7) && ((month % 2) == 1)) || ((month >= 8) && ((month % 2) == 0)))
        return 31;
    else
		return 30;
}

bool Date_IsValid(const Date* value)
{
    return
		(value != NULL) &&
        (value->Year >= DATE_MIN_YEAR) && (value->Year <= DATE_MAX_YEAR) &&
        (value->Month >= 1) && (value->Month <= 12) &&
        (value->Day >= 1) && (value->Day <= Date_DaysInMonth(value->Year, value->Month));
}

bool Date_IsEmpty(const Date* value)
{
    return (value != NULL) && (value->Year == 0) && (value->Month == 0) && (value->Day == 0);
}

bool Date_FromByteArray(Date* value, const byte* buffer, size_t bufferSize)
{
    // Check the parameter values
    if ((value == NULL) || (buffer == NULL) || (bufferSize < DATE_BINARY_SIZE))
        return false;

    // Copy the data from the buffer into the date (year is little endian)
    value->Year = (year_t)((uint16_t)(buffer[1]) << 8 | buffer[0]);
    value->Month = (month_t)buffer[2];
    value->Day = (day_t)buffer[3];

    // Return whether the new date is valid
    return Date_IsValid(value);
}

bool Date_ToByteArray(const Date* value, byte* buffer, size_t bufferSize)
{
    // Check the parameter values
    if ((value == NULL) || (buffer == NULL) || (bufferSize < DATE_BINARY_SIZE))
        return false;

    // Copy the date value into the buffer (year is little endian)
    buffer[0] = (byte)(value->Year);
    buffer[1] = (byte)(value->Year >> 8);
    buffer[2] = (byte)(value->Month);
    buffer[3] = (byte)(value->Day);
    return true;
}

bool Date_ToIsoString(const Date* value, char* buffer, size_t bufferSize)
{
    if ((value == NULL) || (buffer == NULL) || (bufferSize < DATE_ISO_STRING_SIZE))
        return false;

    sprintf(buffer, "%04u-%02u-%02u", value->Year, value->Month, value->Day);
    return true;
}

day_of_week_t Date_GetDayOfWeek(const Date* value)
{
    if (value == NULL)
        return (day_of_week_t)0;
	
    uint16_t year = (uint16_t)value->Year;
    const uint16_t month = (uint16_t)value->Month;
    uint16_t day = (uint16_t)value->Day;

    day += month < 3 ? year-- : year-2;
    return (day_of_week_t)((23*month/9 + day + 4 + year/4 - year/100 + year/400) % 7);
}
