/*=============================================================================
 * File        : keypad.c
 * Project     : Bluetooth-Based Secure Locker with Access Logging
 * Description : Driver for a standard 4x4 matrix keypad used to enter the
 *               Level-2 password and to operate the admin menu.
 *
 * Wiring (all on GPIO Port 1):
 *   Rows (driven as outputs) -> P1.16, P1.17, P1.18, P1.19
 *   Cols (read as inputs, pulled up externally) -> P1.20, P1.21, P1.22, P1.23
 *
 * Scanning method: rows are held HIGH, then one row at a time is pulled
 * LOW; if a key in that row is pressed, its column line will read LOW.
 *==============================/=============================================*/
#include <lpc214x.h>
#include "keypad.h"
#include "defines.h"
#include "delay.h"

#define ROW_MASK   (0x0FUL << 16)   /* P1.16-P1.19 : the 4 row lines (outputs) */
#define COL_MASK   (0x0FUL << 20)   /* P1.20-P1.23 : the 4 column lines (inputs) */

/* Standard 4x4 keypad character layout, row-major order. */
static const u8 key_map[4][4] =
{
    {'1','2','3','A'},
    {'4','5','6','B'},
    {'7','8','9','C'},
    {'*','0','#','D'}
};

/* Configure the row pins as outputs (idle HIGH) and the column pins as
 * inputs, ready for scanning.
 *
 * PINSEL2: only bits 3:2 matter here - bit 2 selects P1.26-31 as GPIO (0) or
 * as the JTAG/Debug port (1), and bit 3 selects P1.16-25 as GPIO (0) or as the
 * Trace port (1). Both must be 0 for the keypad rows/columns (P1.16-P1.23) to
 * work as GPIO.
 *
 * This is a READ-MODIFY-WRITE that clears only those two bits. It used to be a
 * blind "PINSEL2 = 0x00000000", which also wrote 0 over the register's
 * reserved bits - harmless on this part, but the kind of blind write that
 * quietly breaks when a driver is reused on a different chip. Note that
 * clearing bit 2 does disable the JTAG/Debug pin group, which is unavoidable
 * because the keypad and the debug port share those pins; that is worth
 * knowing if you ever try to debug this board over JTAG. */
void keypad_init(void)
{
    PINSEL2 &= ~0x0000000CUL;   /* Clear GPIO/DEBUG (bit 2) + GPIO/TRACE (bit 3) */
    IO1DIR |=  ROW_MASK;    /* Rows  = outputs */
    IO1DIR &= ~COL_MASK;    /* Columns = inputs */
    IO1SET = ROW_MASK;      /* Idle state: all rows HIGH (no key pressed yet) */
}

/* Perform one full scan of the keypad matrix.
 * Returns the ASCII code of the first pressed key found, or 0 if no key
 * is currently pressed. Includes simple debounce + key-release wait so a
 * single press is never read as multiple repeated keys.
 *
 * The wait-for-release loop is BOUNDED (KEY_RELEASE_MAX_MS). It used to be
 * an unconditional `while (!(IO1PIN & ...));`, which meant a key that was
 * held down - or a shorted/stuck key, or a wet keypad - blocked here
 * indefinitely. That silently defeated every timeout built on top of this
 * function (the Level-2 3-minute limit and the admin menu's 15-second idle
 * exit), because no amount of elapsed time could be noticed while the scan
 * itself was stuck. After the bound expires the key is reported anyway;
 * worst case a key held down for longer than the bound repeats, which the
 * callers' own input handling copes with. */
u8 keypad_scan(void)
{
    u8  r, c;
    u32 guard;

    for (r = 0; r < 4; r++)
    {
        IO1SET = ROW_MASK;                 /* All rows HIGH ... */
        IO1CLR = (1UL << (16 + r));         /* ... except the row being scanned (LOW) */
        delay_us(50);                       /* Let the line settle before reading    */

        for (c = 0; c < 4; c++)
        {
            if (!(IO1PIN & (1UL << (20 + c))))   /* Column read LOW -> key pressed */
            {
                /* Wait for release, but never for longer than
                 * KEY_RELEASE_MAX_MS, so a held/stuck key cannot stall
                 * the caller's timeout. */
                guard = KEY_RELEASE_MAX_MS;
                while ((!(IO1PIN & (1UL << (20 + c)))) && (guard > 0UL))
                {
                    delay_ms(1);
                    guard--;
                }

                delay_ms(20);                            /* Debounce delay after release   */
                IO1SET = ROW_MASK;                       /* Restore rows to idle HIGH       */
                return key_map[r][c];                    /* Return the corresponding character */
            }
        }
    }

    IO1SET = ROW_MASK;   /* No key found in this pass; restore idle state */
    return 0;
}

/* Wait for a key press, giving up after 'timeout_ms' milliseconds.
 *
 * Returns the key's ASCII code, or 0 if the limit expired first. See the full
 * contract - including why timeout_ms == 0 now means "do not wait" rather than
 * "wait forever" - in keypad.h.
 *
 * If 'waited_ms' is not NULL it receives how long this call actually spent
 * waiting. Callers enforcing a budget across SEVERAL keystrokes subtract that
 * from their remaining allowance.
 *
 * TIMING: the deadline is measured against the free-running Timer1
 * millisecond counter (millis()), not by adding up the KEY_POLL_MS sleeps.
 * The old accumulate-the-sleeps approach ignored every microsecond spent
 * outside a sleep - the matrix scan itself, the bounded wait for a key to be
 * released, an LCD redraw - so a nominal 180 s budget really lasted ~185 s.
 * Reading a hardware counter instead makes the limit exact, which matters now
 * that the remaining time is shown to the user as a live countdown. */
u8 keypad_getkey_timeout(u32 timeout_ms, u32 *waited_ms)
{
    u32 start = millis();
    u8  key;

    for (;;)
    {
        key = keypad_scan();

        if (key != 0)
        {
            if (waited_ms) *waited_ms = elapsed_since(start);
            return key;
        }

        if (elapsed_since(start) >= timeout_ms)
        {
            if (waited_ms) *waited_ms = elapsed_since(start);
            return 0;                    /* Timed out with no key pressed */
        }

        delay_ms(KEY_POLL_MS);
    }
}
