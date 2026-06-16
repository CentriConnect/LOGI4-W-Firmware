/*
 * WeeklySchedule.c
 *
 *  Created on: Jan 21, 2021
 *      Author: tom.zagotta
 */

#include "WeeklySchedule.h"

// Matches with a resolution of seconds, milliseconds are ignored.
bool WeeklySchedule_IsMatch(const WeeklySchedule* schedule, const DateTime* dateTime)
{
    return
        (schedule != NULL) &&
        (dateTime != NULL) &&
        ((schedule->DaysOfWeek & (1 << (uint8_t)Date_GetDayOfWeek(&dateTime->date))) != 0) &&
        (schedule->StartTime.Hour == dateTime->time.Hour) &&
        (schedule->StartTime.Minute == dateTime->time.Minute) &&
        (schedule->StartTime.Second == dateTime->time.Second);
}
