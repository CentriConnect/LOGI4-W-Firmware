#include "Test_DateTime.h"

#include <string.h>
#include "DateTime.h"
#include "Test.h"

void Test_DateTime()
{
	DateTime dt = { { 2021, 1, 28}, {16, 27, 13, 344} };
	TEST(dt.Date.Year == 2021);
	TEST(dt.Date.Month == 1);
	TEST(dt.Date.Day == 28);
	TEST(dt.Time.Hour == 16);
	TEST(dt.Time.Minute == 27);
	TEST(dt.Time.Second == 13);
	TEST(dt.Time.Millisecond == 344);
	TEST(DateTime_IsValid(&dt));
	TEST(!DateTime_IsEmpty(&dt));

	DateTime_Clear(&dt);
	TEST(dt.Date.Year == 0);
	TEST(dt.Date.Month == 0);
	TEST(dt.Date.Day == 0);
	TEST(dt.Time.Hour == 0);
	TEST(dt.Time.Minute == 0);
	TEST(dt.Time.Second == 0);
	TEST(dt.Time.Millisecond == 0);
	TEST(!DateTime_IsValid(&dt));
	TEST(DateTime_IsEmpty(&dt));

	TEST(DateTime_Initialize(&dt, 2018, 7, 5, 12, 15, 52, 876));
	TEST(dt.Date.Year == 2018);
	TEST(dt.Date.Month == 7);
	TEST(dt.Date.Day == 5);
	TEST(dt.Time.Hour == 12);
	TEST(dt.Time.Minute == 15);
	TEST(dt.Time.Second == 52);
	TEST(dt.Time.Millisecond == 876);
	TEST(DateTime_IsValid(&dt));
	TEST(!DateTime_IsEmpty(&dt));

	byte array[DATETIME_BINARY_SIZE] = { 0 };
	TEST(DateTime_ToByteArray(&dt, array, sizeof(array)));
	TEST((array[0] | ((year_t)array[1] << 8)) == 2018);
	TEST(array[2] == 7);
	TEST(array[3] == 5);
	TEST(array[4] == 12);
	TEST(array[5] == 15);
	TEST(array[6] == 52);
	TEST((array[7] | ((millisecond_t)array[8] << 8)) == 876);

	array[0] = (byte)2014;
	array[1] = (byte)(2014 >> 8);
	array[2] = 9;
	array[3] = 15;
	array[4] = 20;
	array[5] = 52;
	array[6] = 13;
	array[7] = (byte)519;
	array[8] = (byte)(519 >> 8);
	TEST(DateTime_FromByteArray(&dt, array, sizeof(array)));
	TEST(dt.Date.Year == 2014);
	TEST(dt.Date.Month == 9);
	TEST(dt.Date.Day == 15);
	TEST(dt.Time.Hour == 20);
	TEST(dt.Time.Minute == 52);
	TEST(dt.Time.Second == 13);
	TEST(dt.Time.Millisecond == 519);
	TEST(DateTime_IsValid(&dt));
	TEST(!DateTime_IsEmpty(&dt));

	byte short_array[DATETIME_BINARY_SIZE - 1];
	TEST(!DateTime_ToByteArray(&dt, short_array, sizeof(short_array)));
	TEST(!DateTime_FromByteArray(&dt, short_array, sizeof(short_array)));

	byte long_array[DATETIME_BINARY_SIZE + 1] = { 0 };
	long_array[DATETIME_BINARY_SIZE] = 42;
	TEST(DateTime_ToByteArray(&dt, long_array, sizeof(long_array)));
	TEST((long_array[0] | ((year_t)long_array[1] << 8)) == 2014);
	TEST(long_array[2] == 9);
	TEST(long_array[3] == 15);
	TEST(long_array[4] == 20);
	TEST(long_array[5] == 52);
	TEST(long_array[6] == 13);
	TEST((long_array[7] | ((millisecond_t)long_array[8] << 8)) == 519);
	TEST(long_array[DATETIME_BINARY_SIZE] == 42);

	long_array[0] = (byte)2016;
	long_array[1] = (byte)(2016 >> 8);
	long_array[2] = 2;
	long_array[3] = 9;
	long_array[4] = 23;
	long_array[5] = 6;
	long_array[6] = 54;
	long_array[7] = (byte)432;
	long_array[8] = (byte)(432 >> 8);
	TEST(DateTime_FromByteArray(&dt, long_array, sizeof(long_array)));
	TEST(long_array[DATETIME_BINARY_SIZE] == 42);
	TEST(dt.Date.Year == 2016);
	TEST(dt.Date.Month == 2);
	TEST(dt.Date.Day == 9);
	TEST(dt.Time.Hour == 23);
	TEST(dt.Time.Minute == 6);
	TEST(dt.Time.Second == 54);
	TEST(dt.Time.Millisecond == 432);
	TEST(DateTime_IsValid(&dt));
	TEST(!DateTime_IsEmpty(&dt));

	char string[DATETIME_ISO_STRING_SIZE];
	TEST(DateTime_ToIsoString(&dt, string, sizeof(string)));
	TEST(strcmp(string, "2016-02-09T23:06:54.432") == 0);

	char short_string[DATETIME_ISO_STRING_SIZE - 1];
	TEST(!DateTime_ToIsoString(&dt, short_string, sizeof(short_string)));

	char long_string[DATETIME_ISO_STRING_SIZE + 1];
	long_string[DATETIME_ISO_STRING_SIZE] = 42;
	TEST(DateTime_ToIsoString(&dt, long_string, sizeof(long_string)));
	TEST(strcmp(long_string, "2016-02-09T23:06:54.432") == 0);
	TEST(long_string[DATETIME_ISO_STRING_SIZE] == 42);

	DateTime_Clear(&dt);
	TEST(DateTime_ToIsoString(&dt, string, sizeof(string)));
	TEST(strcmp(string, "0000-00-00T00:00:00.000") == 0);

	DateTime_Initialize(&dt, 2016, 2, 9, 23, 6, 54, 432);

	DateTime_AddMilliseconds(&dt, 50);
	TEST(dt.Date.Year == 2016);
	TEST(dt.Date.Month == 2);
	TEST(dt.Date.Day == 9);
	TEST(dt.Time.Hour == 23);
	TEST(dt.Time.Minute == 6);
	TEST(dt.Time.Second == 54);
	TEST(dt.Time.Millisecond == 482);

	DateTime_AddMilliseconds(&dt, 50);
	TEST(dt.Date.Year == 2016);
	TEST(dt.Date.Month == 2);
	TEST(dt.Date.Day == 9);
	TEST(dt.Time.Hour == 23);
	TEST(dt.Time.Minute == 6);
	TEST(dt.Time.Second == 54);
	TEST(dt.Time.Millisecond == 532);

	DateTime_AddMilliseconds(&dt, 500);
	TEST(dt.Date.Year == 2016);
	TEST(dt.Date.Month == 2);
	TEST(dt.Date.Day == 9);
	TEST(dt.Time.Hour == 23);
	TEST(dt.Time.Minute == 6);
	TEST(dt.Time.Second == 55);
	TEST(dt.Time.Millisecond == 32);

	DateTime_AddMilliseconds(&dt, 500);
	TEST(dt.Date.Year == 2016);
	TEST(dt.Date.Month == 2);
	TEST(dt.Date.Day == 9);
	TEST(dt.Time.Hour == 23);
	TEST(dt.Time.Minute == 6);
	TEST(dt.Time.Second == 55);
	TEST(dt.Time.Millisecond == 532);

	DateTime_AddMilliseconds(&dt, 1000);
	TEST(dt.Date.Year == 2016);
	TEST(dt.Date.Month == 2);
	TEST(dt.Date.Day == 9);
	TEST(dt.Time.Hour == 23);
	TEST(dt.Time.Minute == 6);
	TEST(dt.Time.Second == 56);
	TEST(dt.Time.Millisecond == 532);

	DateTime_AddMilliseconds(&dt, 468);
	TEST(dt.Date.Year == 2016);
	TEST(dt.Date.Month == 2);
	TEST(dt.Date.Day == 9);
	TEST(dt.Time.Hour == 23);
	TEST(dt.Time.Minute == 6);
	TEST(dt.Time.Second == 57);
	TEST(dt.Time.Millisecond == 0);

	for (int i = 0; i < 9; i++)
	{
		DateTime_AddMilliseconds(&dt, 100);
		TEST(dt.Date.Year == 2016);
		TEST(dt.Date.Month == 2);
		TEST(dt.Date.Day == 9);
		TEST(dt.Time.Hour == 23);
		TEST(dt.Time.Minute == 6);
		TEST(dt.Time.Second == 57);
		TEST(dt.Time.Millisecond == (i + 1) * 100);
	}

	DateTime_AddMilliseconds(&dt, 100);
	TEST(dt.Date.Year == 2016);
	TEST(dt.Date.Month == 2);
	TEST(dt.Date.Day == 9);
	TEST(dt.Time.Hour == 23);
	TEST(dt.Time.Minute == 6);
	TEST(dt.Time.Second == 58);
	TEST(dt.Time.Millisecond == 0);

	DateTime_AddMilliseconds(&dt, 1999);
	TEST(dt.Date.Year == 2016);
	TEST(dt.Date.Month == 2);
	TEST(dt.Date.Day == 9);
	TEST(dt.Time.Hour == 23);
	TEST(dt.Time.Minute == 6);
	TEST(dt.Time.Second == 59);
	TEST(dt.Time.Millisecond == 999);

	DateTime_AddMilliseconds(&dt, 1);
	TEST(dt.Date.Year == 2016);
	TEST(dt.Date.Month == 2);
	TEST(dt.Date.Day == 9);
	TEST(dt.Time.Hour == 23);
	TEST(dt.Time.Minute == 7);
	TEST(dt.Time.Second == 0);
	TEST(dt.Time.Millisecond == 0);

	for (int i = 0; i < 59; i++)
	{
		DateTime_AddMilliseconds(&dt, 1000);
		TEST(dt.Date.Year == 2016);
		TEST(dt.Date.Month == 2);
		TEST(dt.Date.Day == 9);
		TEST(dt.Time.Hour == 23);
		TEST(dt.Time.Minute == 7);
		TEST(dt.Time.Second == i + 1);
		TEST(dt.Time.Millisecond == 0);
	}

	DateTime_AddMilliseconds(&dt, 1000);
	TEST(dt.Date.Year == 2016);
	TEST(dt.Date.Month == 2);
	TEST(dt.Date.Day == 9);
	TEST(dt.Time.Hour == 23);
	TEST(dt.Time.Minute == 8);
	TEST(dt.Time.Second == 0);
	TEST(dt.Time.Millisecond == 0);

	dt.Time.Hour = 22;
	dt.Time.Minute = 59;
	dt.Time.Second = 59;
	DateTime_AddMilliseconds(&dt, 1000);
	TEST(dt.Date.Year == 2016);
	TEST(dt.Date.Month == 2);
	TEST(dt.Date.Day == 9);
	TEST(dt.Time.Hour == 23);
	TEST(dt.Time.Minute == 0);
	TEST(dt.Time.Second == 0);
	TEST(dt.Time.Millisecond == 0);

	dt.Time.Hour = 23;
	dt.Time.Minute = 59;
	dt.Time.Second = 59;
	DateTime_AddMilliseconds(&dt, 1000);
	TEST(dt.Date.Year == 2016);
	TEST(dt.Date.Month == 2);
	TEST(dt.Date.Day == 10);
	TEST(dt.Time.Hour == 0);
	TEST(dt.Time.Minute == 0);
	TEST(dt.Time.Second == 0);
	TEST(dt.Time.Millisecond == 0);

	dt.Date.Day = 28;
	dt.Time.Hour = 23;
	dt.Time.Minute = 59;
	dt.Time.Second = 59;
	DateTime_AddMilliseconds(&dt, 1000);
	TEST(dt.Date.Year == 2016);
	TEST(dt.Date.Month == 2);
	TEST(dt.Date.Day == 29);
	TEST(dt.Time.Hour == 0);
	TEST(dt.Time.Minute == 0);
	TEST(dt.Time.Second == 0);
	TEST(dt.Time.Millisecond == 0);

	dt.Date.Day = 29;
	dt.Time.Hour = 23;
	dt.Time.Minute = 59;
	dt.Time.Second = 59;
	DateTime_AddMilliseconds(&dt, 1000);
	TEST(dt.Date.Year == 2016);
	TEST(dt.Date.Month == 3);
	TEST(dt.Date.Day == 1);
	TEST(dt.Time.Hour == 0);
	TEST(dt.Time.Minute == 0);
	TEST(dt.Time.Second == 0);
	TEST(dt.Time.Millisecond == 0);

	dt.Date.Year = 2017;
	dt.Date.Month = 2;
	dt.Date.Day = 28;
	dt.Time.Hour = 23;
	dt.Time.Minute = 59;
	dt.Time.Second = 59;
	DateTime_AddMilliseconds(&dt, 1000);
	TEST(dt.Date.Year == 2017);
	TEST(dt.Date.Month == 3);
	TEST(dt.Date.Day == 1);
	TEST(dt.Time.Hour == 0);
	TEST(dt.Time.Minute == 0);
	TEST(dt.Time.Second == 0);
	TEST(dt.Time.Millisecond == 0);

	dt.Date.Year = 2017;
	dt.Date.Month = 12;
	dt.Date.Day = 31;
	dt.Time.Hour = 23;
	dt.Time.Minute = 59;
	dt.Time.Second = 59;
	DateTime_AddMilliseconds(&dt, 1000);
	TEST(dt.Date.Year == 2018);
	TEST(dt.Date.Month == 1);
	TEST(dt.Date.Day == 1);
	TEST(dt.Time.Hour == 0);
	TEST(dt.Time.Minute == 0);
	TEST(dt.Time.Second == 0);
	TEST(dt.Time.Millisecond == 0);

	DateTime_Clear(nullptr);
	TEST(!DateTime_Initialize(nullptr, 2018, 7, 5, 12, 15, 52, 876));
	TEST(!DateTime_IsValid(nullptr));
	TEST(!DateTime_IsEmpty(nullptr));
}
