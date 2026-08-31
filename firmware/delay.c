/*=============================================================================
 * File        : delay.c
 * Project     : Bluetooth-Based Secure Locker with Access Logging
 * Description : Time services for the whole project:
 *                 - blocking delays (delay_ms / delay_us) built on Timer0
 *                 - a free-running millisecond time base (millis()) built on
 *                   Timer1, which every timeout in the project measures
 *                   against
 *
 * PCLK is assumed to be 15 MHz (see the PLL configuration in
 * projectmain.c). Timer0 is configured in timer mode with a prescaler
 * (T0PR) chosen so that T0TC increments once per millisecond or once per
 * microsecond, letting the delay simply be "wait until T0TC reaches the
 * requested count".
 *
 * T0CTCR = 0x00 -> Timer0 counts PCLK cycles (timer mode, not counter mode)
 * T0TCR  = 0x02 -> Reset the timer counter and prescale counter
 * T0TCR  = 0x01 -> Enable (start) the timer counter
 *
 * TIMER USAGE MAP FOR THE WHOLE PROJECT (keep this accurate):
 *   Timer0 - reset and restarted by every delay_ms()/delay_us() call. Must
 *            NOT be relied on to keep counting across calls.
 *   Timer1 - started once by timebase_init() and then left alone forever;
 *            millis() just reads T1TC. Nothing else in the project touches
 *            Timer1, so the two uses cannot interfere.
 *   RTC    - wall-clock date/time (rtc.c), independent of both timers.
 *===========================================================================*/
#include <lpc214x.h>
#include "delay.h"

/* Busy-wait for approximately 'ms' milliseconds.
 * Prescaler = 15000 -> with PCLK = 15 MHz, T0TC increments once every
 * 15000 / 15,000,000 s = 1 ms, so waiting for T0TC == ms gives a
 * millisecond-accurate delay. */
void delay_ms(u32 ms)
{
    T0CTCR = 0x00;         /* Plain timer mode                          */
    T0PR   = 15000 - 1;    /* Prescaler: 1 timer tick every 1 ms         */
    T0TC   = 0x00;         /* Reset the tick counter                     */
    T0TCR  = 0x02;         /* Reset the timer (counter + prescaler)      */
    T0TCR  = 0x01;         /* Start the timer                            */
    while (T0TC < ms);     /* Busy-wait until the requested time elapses */
    T0TCR  = 0x00;         /* Stop the timer                             */
}

/* Busy-wait for approximately 'us' microseconds.
 * Prescaler = 15 -> with PCLK = 15 MHz, T0TC increments once every
 * 15 / 15,000,000 s = 1 us. */
void delay_us(u32 us)
{
    T0CTCR = 0x00;
    T0PR   = 15 - 1;       /* Prescaler: 1 timer tick every 1 us */
    T0TC   = 0x00;
    T0TCR  = 0x02;
    T0TCR  = 0x01;
    while (T0TC < us);
    T0TCR  = 0x00;
}

/*=============================================================================
 * FREE-RUNNING MILLISECOND TIME BASE (Timer1)
 *===========================================================================*/

/* Start Timer1 as a free-running millisecond counter and then leave it
 * running for the lifetime of the program.
 *
 * Prescaler = 15000 -> with PCLK = 15 MHz the prescale counter overflows
 * every 15000 / 15,000,000 s = 1 ms, so T1TC counts milliseconds exactly (the
 * same arithmetic delay_ms() uses on Timer0).
 *
 * No match register, no interrupt: the counter is only ever READ, which makes
 * this the cheapest possible time base and means it cannot add latency or
 * jitter anywhere. T1TC is 32-bit, so it wraps after 2^32 ms = 49.7 days;
 * elapsed_since() below handles the wrap.
 *
 * MUST be called after the PLL/VPBDIV configuration has settled PCLK at
 * 15 MHz, otherwise the prescaler is calibrated for the wrong clock. */
void timebase_init(void)
{
    T1CTCR = 0x00;         /* Plain timer mode: count PCLK cycles         */
    T1PR   = 15000 - 1;    /* Prescaler: T1TC increments once every 1 ms   */
    T1TC   = 0x00;         /* Start counting from zero                     */
    T1TCR  = 0x02;         /* Reset the counter and the prescale counter   */
    T1TCR  = 0x01;         /* Start the timer - and never stop it again    */
}

/* Milliseconds since timebase_init(). See the wrap-around warning in delay.h:
 * always compare with elapsed_since(), never with '<' or '>' directly. */
u32 millis(void)
{
    return (u32)T1TC;
}

/* Milliseconds elapsed from 'start_ms' (a previously captured millis() value)
 * until now.
 *
 * The subtraction is done in unsigned 32-bit arithmetic, which is what makes
 * this correct even when the counter wraps between the two readings: if
 * start_ms = 0xFFFFFF00 and millis() has since wrapped to 0x00000010, then
 * (0x00000010 - 0xFFFFFF00) evaluates to 0x110 = 272 ms, which is the true
 * interval. A plain "now - start" written with signed types, or a "now <
 * start" test, would get this wrong once every 49.7 days. */
u32 elapsed_since(u32 start_ms)
{
    return (u32)(millis() - start_ms);
}
