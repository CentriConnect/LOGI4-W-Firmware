/*
 * Date.h
 *
 *  Created on: Jan 21, 2021
 *      Author: tom.zagotta
 */

#ifndef DATE_H_
#define DATE_H_

#include "System.h"

// Basic data types for dates
typedef uint16_t year_t; // 2000+
typedef uint8_t month_t; // 1-12
typedef uint8_t day_t; // 1-31
typedef enum
{
    Date_Sunday,
    Date_Monday,
    Date_Tuesday,
    Date_Wednesday,
    Date_Thursday,
    Date_Friday,
    Date_Saturday
} day_of_week_t; // 0-6

#define DATE_WEEKDAYS ((1 << Date_Monday)|(1<< Date_Tuesday)|(1 << Date_Wednesday)|(1 << Date_Thursday)|(1 << Date_Friday))
#define DATE_WEEKEND ((1 << Date_Sunday)|(1 << Date_Saturday))
#define DATE_DAYS_ALL (1 << Date_Sunday)|(1 << Date_Monday)|(1<< Date_Tuesday)|(1 << Date_Wednesday)|(1 << Date_Thursday)|(1 << Date_Friday)|(1 << Date_Saturday)

// Valid year range
#define DATE_MIN_YEAR 2000
#define DATE_MAX_YEAR 3000

// Binary buffer size
#define DATE_BINARY_SIZE (sizeof(year_t)+sizeof(month_t)+sizeof(day_t))

// ISO 8601 formatted string buffer size (includes NUL terminator)
#define DATE_ISO_STRING_SIZE sizeof("0000-00-00")

// Date structure
typedef struct
{
    year_t Year;
    month_t Month;
    day_t Day;
} Date;

day_t Date_DaysInMonth(const year_t year, const month_t month);

void Date_Clear(Date* value);

bool Date_Initialize(Date* date, year_t year, month_t month, day_t day);

bool Date_IsValid(const Date* value);

bool Date_IsEmpty(const Date* value);

bool Date_IsLeapYear(const year_t year);

bool Date_FromByteArray(Date* value, const byte* buffer, size_t bufferSize);

bool Date_ToByteArray(const Date* value, byte* buffer, size_t bufferSize);

bool Date_ToIsoString(const Date* value, char* buffer, size_t bufferSize);

day_of_week_t Date_GetDayOfWeek(const Date* value);

#endif /* DATE_H_ */
