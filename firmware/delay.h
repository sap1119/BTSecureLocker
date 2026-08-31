/*=============================================================================
 * File        : delay.h
 * Project     : Bluetooth-Based Secure Locker with Access Logging
 * Description : Public API for simple blocking millisecond/microsecond
 *               delays (Timer0), plus a free-running millisecond time base
 *               (Timer1) used by every timeout in the project.
 *===========================================================================*/
#ifndef DELAY_H
#define DELAY_H

#include "types.h"

void delay_ms(u32 ms);   /* Block for approximately 'ms' milliseconds */
void delay_us(u32 us);   /* Block for approximately 'us' microseconds */

/*-------------- Free-running millisecond time base (Timer1) ----------------
 * WHY THIS EXISTS: before this was added, every timeout in the project was
 * measured by ADDING UP the delay_ms() sleeps a polling loop had performed
 * ("elapsed += KEY_POLL_MS"). That silently ignores all the time spent doing
 * the actual work between sleeps - scanning the keypad matrix, writing to the
 * LCD (every lcd_data() costs ~2 ms), sounding the buzzer, printing a log
 * line - so a nominal 180 s budget really expired at ~185 s and a countdown
 * shown on the LCD would visibly lag a stopwatch.
 *
 * millis() reads a hardware counter instead, so a timeout measures REAL
 * elapsed time no matter what the loop did in between.
 *
 *   timebase_init()      - start Timer1 as a free-running 1 ms counter. Must
 *                          be called once, AFTER the PLL/VPBDIV setup has
 *                          settled PCLK at 15 MHz (see
 *                          SystemInit_SecureLocker() in projectmain.c).
 *   millis()             - milliseconds since timebase_init(). Wraps back to
 *                          0 after ~49.7 days; never compare two values with
 *                          '<' or '>', always use elapsed_since().
 *   elapsed_since(start) - milliseconds from 'start' until now, computed with
 *                          unsigned arithmetic so it stays correct across the
 *                          49.7-day wrap.
 *
 * Timer1 is used ONLY for this, is never stopped or reset after init, and
 * raises no interrupt - so it cannot disturb Timer0, which delay_ms()/
 * delay_us() keep resetting on every call.
 * ------------------------------------------------------------------------ */
void timebase_init(void);
u32  millis(void);
u32  elapsed_since(u32 start_ms);

#endif
