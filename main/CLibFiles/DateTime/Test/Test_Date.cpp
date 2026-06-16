#include "Test_Date.h"

#include <string.h>
#include "Date.h"
#include "Test.h"

void Test_Date()
{
	Date d = { 2021, 1, 28 };
	TEST(d.Year == 2021);
	TEST(d.Month == 1);
	TEST(d.Day == 28);
	TEST(Date_IsValid(&d));
	TEST(!Date_IsEmpty(&d));
	TEST(Date_GetDayOfWeek(&d) == Date_Thursday);
	TEST((1 << Date_GetDayOfWeek(&d)) & DATE_WEEKDAYS);
	TEST(((1 << Date_GetDayOfWeek(&d)) & DATE_WEEKEND) == 0);

	Date_Clear(&d);
	TEST(d.Year == 0);
	TEST(d.Month == 0);
	TEST(d.Day == 0);
	TEST(!Date_IsValid(&d));
	TEST(Date_IsEmpty(&d));

	TEST(Date_Initialize(&d, 2020, 8, 20));
	TEST(d.Year == 2020);
	TEST(d.Month == 8);
	TEST(d.Day == 20);
	TEST(Date_IsValid(&d));
	TEST(!Date_IsEmpty(&d));
	TEST(Date_GetDayOfWeek(&d) == Date_Thursday);
	TEST((1 << Date_GetDayOfWeek(&d)) & DATE_WEEKDAYS);
	TEST(((1 << Date_GetDayOfWeek(&d)) & DATE_WEEKEND) == 0);

	TEST(!Date_Initialize(&d, 2020, 8, 32));
	TEST(!Date_Initialize(&d, 2020, 8, 0));
	TEST(Date_Initialize(&d, 2020, 8, 1));
	TEST(d.Year == 2020);
	TEST(d.Month == 8);
	TEST(d.Day == 1);
	TEST(Date_IsValid(&d));
	TEST(!Date_IsEmpty(&d));
	TEST(Date_GetDayOfWeek(&d) == Date_Saturday);
	TEST(((1 << Date_GetDayOfWeek(&d)) & DATE_WEEKDAYS) == 0);
	TEST((1 << Date_GetDayOfWeek(&d)) & DATE_WEEKEND);

	byte array[DATE_BINARY_SIZE] = { 0 };
	TEST(Date_ToByteArray(&d, array, sizeof(array)));
	TEST((array[0] | (year_t)array[1] << 8) == 2020);
	TEST(array[2] == 8);
	TEST(array[3] == 1);

	array[0] = (byte)2021;
	array[1] = (byte)(2021 >> 8);
	array[2] = 1;
	array[3] = 28;
	TEST(Date_FromByteArray(&d, array, sizeof(array)));
	TEST(d.Year == 2021);
	TEST(d.Month == 1);
	TEST(d.Day == 28);
	TEST(Date_IsValid(&d));
	TEST(!Date_IsEmpty(&d));
	TEST(Date_GetDayOfWeek(&d) == Date_Thursday);
	TEST((1 << Date_GetDayOfWeek(&d)) & DATE_WEEKDAYS);
	TEST(((1 << Date_GetDayOfWeek(&d)) & DATE_WEEKEND) == 0);

	byte short_array[DATE_BINARY_SIZE - 1];
	TEST(!Date_ToByteArray(&d, short_array, sizeof(short_array)));
	TEST(!Date_FromByteArray(&d, short_array, sizeof(short_array)));

	byte long_array[DATE_BINARY_SIZE + 1] = { 0 };
	long_array[DATE_BINARY_SIZE] = 42;
	TEST(Date_ToByteArray(&d, long_array, sizeof(long_array)));
	TEST((long_array[0] | (year_t)long_array[1] << 8) == 2021);
	TEST(long_array[2] == 1);
	TEST(long_array[3] == 28);
	TEST(long_array[4] == 42);
	long_array[0] = (byte)2020;
	long_array[1] = (byte)(2020 >> 8);
	long_array[2] = 8;
	long_array[3] = 20;
	TEST(Date_FromByteArray(&d, long_array, sizeof(long_array)));
	TEST(d.Year == 2020);
	TEST(d.Month == 8);
	TEST(d.Day == 20);

	char string[DATE_ISO_STRING_SIZE];
	TEST(Date_ToIsoString(&d, string, sizeof(string)));
	TEST(strcmp(string, "2020-08-20") == 0);

	char short_string[DATE_ISO_STRING_SIZE - 1];
	TEST(!Date_ToIsoString(&d, short_string, sizeof(short_string)));

	char long_string[DATE_ISO_STRING_SIZE + 1];
	long_string[DATE_ISO_STRING_SIZE] = 42;
	TEST(Date_ToIsoString(&d, long_string, sizeof(string)));
	TEST(strcmp(long_string, "2020-08-20") == 0);
	TEST(long_string[DATE_ISO_STRING_SIZE] == 42);

	Date_Clear(&d);
	TEST(Date_ToIsoString(&d, string, sizeof(string)));
	TEST(strcmp(string, "0000-00-00") == 0);

	TEST(!Date_IsLeapYear(2021));
	TEST(Date_IsLeapYear(2020));
	TEST(!Date_IsLeapYear(2019));
	TEST(!Date_IsLeapYear(2018));
	TEST(!Date_IsLeapYear(2017));
	TEST(Date_IsLeapYear(2016));
	TEST(!Date_IsLeapYear(2015));
	TEST(!Date_IsLeapYear(2014));
	TEST(!Date_IsLeapYear(2013));
	TEST(Date_IsLeapYear(2012));
	TEST(!Date_IsLeapYear(2011));
	TEST(!Date_IsLeapYear(2010));
	TEST(!Date_IsLeapYear(2009));
	TEST(Date_IsLeapYear(2008));
	TEST(!Date_IsLeapYear(2007));
	TEST(!Date_IsLeapYear(2006));
	TEST(!Date_IsLeapYear(2005));
	TEST(Date_IsLeapYear(2004));
	TEST(!Date_IsLeapYear(2003));
	TEST(!Date_IsLeapYear(2002));
	TEST(!Date_IsLeapYear(2001));
	TEST(Date_IsLeapYear(2000));
	TEST(!Date_IsLeapYear(1999));

	TEST(Date_DaysInMonth(2021, 0) == 0);
	TEST(Date_DaysInMonth(2021, 1) == 31);
	TEST(Date_DaysInMonth(2021, 2) == 28);
	TEST(Date_DaysInMonth(2021, 3) == 31);
	TEST(Date_DaysInMonth(2021, 4) == 30);
	TEST(Date_DaysInMonth(2021, 5) == 31);
	TEST(Date_DaysInMonth(2021, 6) == 30);
	TEST(Date_DaysInMonth(2021, 7) == 31);
	TEST(Date_DaysInMonth(2021, 8) == 31);
	TEST(Date_DaysInMonth(2021, 9) == 30);
	TEST(Date_DaysInMonth(2021, 10) == 31);
	TEST(Date_DaysInMonth(2021, 11) == 30);
	TEST(Date_DaysInMonth(2021, 12) == 31);
	TEST(Date_DaysInMonth(2021, 13) == 0);

	TEST(Date_DaysInMonth(2020, 2) == 29);
	TEST(Date_DaysInMonth(2019, 2) == 28);
	TEST(Date_DaysInMonth(2018, 2) == 28);
	TEST(Date_DaysInMonth(2017, 2) == 28);
	TEST(Date_DaysInMonth(2016, 2) == 29);
	TEST(Date_DaysInMonth(2015, 2) == 28);

	Date_Initialize(&d, 2021, 1, 28);
	TEST(Date_GetDayOfWeek(&d) == Date_Thursday);
	d.Day = 27;
	TEST(Date_GetDayOfWeek(&d) == Date_Wednesday);
	d.Day = 26;
	TEST(Date_GetDayOfWeek(&d) == Date_Tuesday);
	d.Day = 25;
	TEST(Date_GetDayOfWeek(&d) == Date_Monday);
	d.Day = 24;
	TEST(Date_GetDayOfWeek(&d) == Date_Sunday);
	d.Day = 23;
	TEST(Date_GetDayOfWeek(&d) == Date_Saturday);
	d.Day = 22;
	TEST(Date_GetDayOfWeek(&d) == Date_Friday);
	d.Day = 21;
	TEST(Date_GetDayOfWeek(&d) == Date_Thursday);
	Date_Initialize(&d, 2021, 1, 28);
	TEST(Date_GetDayOfWeek(&d) == Date_Thursday);
	d.Day = 29;
	TEST(Date_GetDayOfWeek(&d) == Date_Friday);
	d.Day = 30;
	TEST(Date_GetDayOfWeek(&d) == Date_Saturday);
	d.Day = 31;
	TEST(Date_GetDayOfWeek(&d) == Date_Sunday);
	d.Month = 2;
	d.Day = 1;
	TEST(Date_GetDayOfWeek(&d) == Date_Monday);
	d.Day = 2;
	TEST(Date_GetDayOfWeek(&d) == Date_Tuesday);

	Date_Initialize(nullptr, 2021, 1, 28);
	Date_Clear(nullptr);
	TEST(!Date_IsValid(nullptr));
	TEST(!Date_IsEmpty(nullptr));
	TEST(Date_GetDayOfWeek(nullptr) == 0);
	TEST(!Date_ToByteArray(&d, nullptr, DATE_BINARY_SIZE));
	TEST(!Date_FromByteArray(&d, nullptr, DATE_BINARY_SIZE));
	TEST(!Date_ToIsoString(nullptr, string, DATE_ISO_STRING_SIZE));
	TEST(!Date_ToIsoString(&d, nullptr, DATE_ISO_STRING_SIZE));
}
