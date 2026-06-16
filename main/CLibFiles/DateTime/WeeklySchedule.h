/*
 * WeeklySchedule.h
 *
 *  Created on: Jan 21, 2021
 *      Author: tom.zagotta
 */

#ifndef WEEKLYSCHEDULE_H_
#define WEEKLYSCHEDULE_H_

#include "DateTime.h"

// Set of days of weeks, individual bits are set for each day, with
// the bit mask for each day being (1 << day_of_week), bit 7 unused.
typedef uint8_t day_of_week_set_t;

// Weekly schedule
typedef struct
{
    day_of_week_set_t DaysOfWeek;
    Time StartTime;
} WeeklySchedule;

bool WeeklySchedule_IsMatch(const WeeklySchedule* schedule, const DateTime* dateTime);

#endif /* WEEKLYSCHEDULE_H_ */
