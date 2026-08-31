/*=============================================================================
 * File        : keypad.h
 * Project     : Bluetooth-Based Secure Locker with Access Logging
 * Description : Public API for the 4x4 matrix keypad driver.
 *===========================================================================*/
#ifndef KEYPAD_H
#define KEYPAD_H

#include "types.h"

void keypad_init(void);  /* Configure row (output) and column (input) GPIO lines */
u8   keypad_scan(void);  /* Scan once; returns the pressed key's ASCII code, or 0 if none */

/* Wait for a key press, giving up after 'timeout_ms' milliseconds.
 *
 *   returns            -> the pressed key's ASCII code, or 0 if the time limit
 *                         expired with no key pressed
 *   timeout_ms == 0    -> do not wait at all: scan once and return 0 if no key
 *                         is down (see the WARNING below)
 *   waited_ms != NULL  -> receives how long this call actually waited, so a
 *                         caller can enforce ONE budget across several
 *                         keystrokes
 *
 * The elapsed time is measured against the Timer1 millisecond time base
 * (millis(), see delay.h), NOT by adding up the polling sleeps, so it stays
 * accurate however long the loop spends scanning the matrix, waiting for a key
 * to be released, or redrawing an on-screen countdown.
 *
 * *** WARNING - THIS BEHAVIOUR CHANGED ON PURPOSE ***
 * timeout_ms == 0 used to mean "WAIT FOREVER", and there was a separate
 * keypad_getkey() wrapper that passed 0 to get exactly that. Both are gone.
 *
 * The reason: several callers compute a DECAYING budget ("remaining -=
 * waited") and pass it in. The moment such a budget reached exactly 0, the
 * call silently turned into an infinite wait - reintroducing the very hang the
 * timeout existed to prevent. That happened once already during development
 * and was only caught by review. Every caller then had to remember to write
 * "if (remaining == 0) return 0;" first, which is a trap, not an API.
 *
 * Now 0 means "no time left, do not wait", so an exhausted budget fails SAFE
 * instead of hanging. There is no longer any way to ask this driver to block
 * forever, which is also why keypad_getkey() no longer exists: every keypad
 * wait in the project is now bounded by construction.
 * ------------------------------------------------------------------------ */
u8   keypad_getkey_timeout(u32 timeout_ms, u32 *waited_ms);

#endif
