/*=============================================================================
 * File        : rtc.h
 * Project     : Bluetooth-Based Secure Locker with Access Logging
 * Description : Public API for the LPC2148's on-chip Real-Time Clock (RTC),
 *               used to timestamp every access-log entry, to drive the idle
 *               clock display, and to trigger the admin alarm.
 *===========================================================================*/
#ifndef RTC_H
#define RTC_H

#include "types.h"

/*-------------- Consistent date/time snapshot -----------------------------
 * One coherent reading of the whole calendar, filled in by rtc_get().
 *
 * WHY A SNAPSHOT TYPE EXISTS: reading the individual RTC registers one after
 * another (HOUR, then MIN, then SEC, ...) can TEAR across a rollover. Read
 * HOUR at 12:59:59.999 and MIN/SEC a few microseconds later, after the clock
 * has ticked, and the result is "12:00:00" - an hour wrong, once an hour, at
 * random. The same tear between the time and date fields can report the
 * previous day's date with today's time at midnight.
 *
 * rtc_get() avoids this by reading the RTC's CONSOLIDATED registers
 * (CTIME0/CTIME1), which latch several fields into one 32-bit word, and by
 * re-reading until two consecutive reads agree. Every part of the project
 * that displays or compares the time uses it.
 * ------------------------------------------------------------------------ */
typedef struct
{
    u8  hour;    /* 0 .. 23                       */
    u8  min;     /* 0 .. 59                       */
    u8  sec;     /* 0 .. 59                       */
    u8  dom;     /* Day of month,  1 .. 31        */
    u8  month;   /* 1 .. 12                       */
    u8  dow;     /* Day of week,   0=SUN .. 6=SAT */
    u16 year;    /* Full 4-digit year             */
} rtc_time;

void rtc_init(void);                                   /* Reset and start the on-chip RTC, then apply the boot RTC policy
                                                          (restore from a battery-backed external RTC if fitted, else a
                                                          default only when the RTC is unset)                             */
u8   rtc_battery_backed(void);                         /* 1 = an external RTC was found at boot and is keeping the time    */
void rtc_set_time(u8 hh, u8 mm, u8 ss);                 /* Set the current time (24-hour format)               */
void rtc_set_date(u8 dd, u8 mon, u16 yy);               /* Set the current date                                 */
void rtc_set_dow(u8 dow);                               /* Set the day of week (0=SUN .. 6=SAT)                 */
void rtc_get(rtc_time *t);                              /* Read one tear-free snapshot of the whole calendar    */
void rtc_get_stamp(char *buf);                          /* Format "DD/MM/YYYY HH:MM:SS" into buf (20 bytes min) */

#endif
