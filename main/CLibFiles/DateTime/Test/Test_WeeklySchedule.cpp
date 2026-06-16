#include "Test_WeeklySchedule.h"

#include "Test.h"
#include "WeeklySchedule.h"

void Test_WeeklySchedule()
{
	WeeklySchedule ws = { (1 << Date_Sunday), {8, 0, 0, 0} };
	DateTime dt = { {2021, 1, 31}, {7, 59, 59, 0} };
	TEST(!WeeklySchedule_IsMatch(&ws, &dt));
	DateTime_AddMilliseconds(&dt, 500);
	TEST(!WeeklySchedule_IsMatch(&ws, &dt));
	DateTime_AddMilliseconds(&dt, 500);
	TEST(WeeklySchedule_IsMatch(&ws, &dt));
	DateTime_AddMilliseconds(&dt, 500);
	TEST(WeeklySchedule_IsMatch(&ws, &dt));
	DateTime_AddMilliseconds(&dt, 500);
	TEST(!WeeklySchedule_IsMatch(&ws, &dt));

	DateTime_Initialize(&dt, 2021, 1, 30, 8, 0, 0, 0);
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Sat
	dt.Date.Day = 31;
	TEST(WeeklySchedule_IsMatch(&ws, &dt)); // Sun
	dt.Date.Month = 2;
	dt.Date.Day = 1;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Mon
	dt.Date.Day = 2;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Tue
	dt.Date.Day = 3;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Wed
	dt.Date.Day = 4;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Thu
	dt.Date.Day = 5;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Fri
	dt.Date.Day = 6;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Sat
	dt.Date.Day = 7;
	TEST(WeeklySchedule_IsMatch(&ws, &dt)); // Sun

	ws.DaysOfWeek = (1 << Date_Monday);
	DateTime_Initialize(&dt, 2021, 1, 30, 8, 0, 0, 0);
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Sat
	dt.Date.Day = 31;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Sun
	dt.Date.Month = 2;
	dt.Date.Day = 1;
	TEST(WeeklySchedule_IsMatch(&ws, &dt));// Mon
	dt.Date.Day = 2;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Tue
	dt.Date.Day = 3;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Wed
	dt.Date.Day = 4;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Thu
	dt.Date.Day = 5;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Fri
	dt.Date.Day = 6;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Sat
	dt.Date.Day = 7;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Sun

	ws.DaysOfWeek = (1 << Date_Saturday);
	DateTime_Initialize(&dt, 2021, 1, 30, 8, 0, 0, 0);
	TEST(WeeklySchedule_IsMatch(&ws, &dt)); // Sat
	dt.Date.Day = 31;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Sun
	dt.Date.Month = 2;
	dt.Date.Day = 1;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt));// Mon
	dt.Date.Day = 2;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Tue
	dt.Date.Day = 3;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Wed
	dt.Date.Day = 4;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Thu
	dt.Date.Day = 5;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Fri
	dt.Date.Day = 6;
	TEST(WeeklySchedule_IsMatch(&ws, &dt)); // Sat
	dt.Date.Day = 7;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Sun

	ws.DaysOfWeek = DATE_WEEKDAYS;
	DateTime_Initialize(&dt, 2021, 1, 30, 8, 0, 0, 0);
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Sat
	dt.Date.Day = 31;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Sun
	dt.Date.Month = 2;
	dt.Date.Day = 1;
	TEST(WeeklySchedule_IsMatch(&ws, &dt));// Mon
	dt.Date.Day = 2;
	TEST(WeeklySchedule_IsMatch(&ws, &dt)); // Tue
	dt.Date.Day = 3;
	TEST(WeeklySchedule_IsMatch(&ws, &dt)); // Wed
	dt.Date.Day = 4;
	TEST(WeeklySchedule_IsMatch(&ws, &dt)); // Thu
	dt.Date.Day = 5;
	TEST(WeeklySchedule_IsMatch(&ws, &dt)); // Fri
	dt.Date.Day = 6;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Sat
	dt.Date.Day = 7;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Sun

	ws.DaysOfWeek = DATE_WEEKEND;
	DateTime_Initialize(&dt, 2021, 1, 30, 8, 0, 0, 0);
	TEST(WeeklySchedule_IsMatch(&ws, &dt)); // Sat
	dt.Date.Day = 31;
	TEST(WeeklySchedule_IsMatch(&ws, &dt)); // Sun
	dt.Date.Month = 2;
	dt.Date.Day = 1;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt));// Mon
	dt.Date.Day = 2;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Tue
	dt.Date.Day = 3;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Wed
	dt.Date.Day = 4;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Thu
	dt.Date.Day = 5;
	TEST(!WeeklySchedule_IsMatch(&ws, &dt)); // Fri
	dt.Date.Day = 6;
	TEST(WeeklySchedule_IsMatch(&ws, &dt)); // Sat
	dt.Date.Day = 7;
	TEST(WeeklySchedule_IsMatch(&ws, &dt)); // Sun

	ws.DaysOfWeek = DATE_DAYS_ALL;
	DateTime_Initialize(&dt, 2021, 1, 30, 8, 0, 0, 0);
	TEST(WeeklySchedule_IsMatch(&ws, &dt)); // Sat
	dt.Date.Day = 31;
	TEST(WeeklySchedule_IsMatch(&ws, &dt)); // Sun
	dt.Date.Month = 2;
	dt.Date.Day = 1;
	TEST(WeeklySchedule_IsMatch(&ws, &dt));// Mon
	dt.Date.Day = 2;
	TEST(WeeklySchedule_IsMatch(&ws, &dt)); // Tue
	dt.Date.Day = 3;
	TEST(WeeklySchedule_IsMatch(&ws, &dt)); // Wed
	dt.Date.Day = 4;
	TEST(WeeklySchedule_IsMatch(&ws, &dt)); // Thu
	dt.Date.Day = 5;
	TEST(WeeklySchedule_IsMatch(&ws, &dt)); // Fri
	dt.Date.Day = 6;
	TEST(WeeklySchedule_IsMatch(&ws, &dt)); // Sat
	dt.Date.Day = 7;
	TEST(WeeklySchedule_IsMatch(&ws, &dt)); // Sun
}
