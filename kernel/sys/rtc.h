/* =============================================================================
 *  AGOS - CMOS real-time clock (sys/rtc.h, sys/rtc.c)
 * ========================================================================== */
#ifndef AGOS_RTC_H
#define AGOS_RTC_H

#include <stdbool.h>
#include <stdint.h>

struct rtc_time {
    uint16_t year;                      /* e.g. 2026                           */
    uint8_t  mon;                       /* 1..12                               */
    uint8_t  mday;                      /* 1..31                               */
    uint8_t  hour, min, sec;
    uint8_t  wday;                      /* 0 = Sunday .. 6 = Saturday          */
};

/* Read the wall clock (as the BIOS stored it: usually local time). Returns
 * false if the chip looks dead (invalid fields); *t is then zeroed-ish. */
bool rtc_read(struct rtc_time *t);

/* Day of week for a Gregorian date, 0 = Sunday. */
unsigned rtc_day_of_week(unsigned year, unsigned mon, unsigned mday);

#endif /* AGOS_RTC_H */
