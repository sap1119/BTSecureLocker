/*=============================================================================
 * File        : security.h
 * Project     : Bluetooth-Based Secure Locker with Access Logging
 * Description : Public API for tamper detection, the UART/RTC-timestamped
 *               access-logging helper, and first-boot password provisioning.
 *===========================================================================*/
#ifndef SECURITY_H
#define SECURITY_H

#include "types.h"

void security_init(void);              /* Configure the tamper-switch input pin           */
u8   tamper_detected(void);             /* Returns 1 if the tamper switch is currently open */
void check_tamper_and_alert(void);      /* Poll the tamper switch; on a fresh event show it on the LCD + buzzer */
void ensure_default_passwords(void);    /* Load factory-default passwords on first boot     */
void log_event(const char *msg);        /* Print a timestamped event line over UART0         */
void log_event2(const char *msg, const char *arg); /* log_event() + one string argument, so callers
                                                    * can timestamp lines that include a value, e.g.
                                                    * log_event2("Admin set clock time to ", "12:07:30").
                                                    * There is deliberately no printf in this codebase. */

/* Poll the tamper switch and return 1 on a FRESH tamper event, logging it but
 * touching neither the LCD nor the buzzer.
 *
 * This is what lets tamper detection stay active during phases that own the
 * display and must not have it wiped from under them - above all the Level-2
 * keypad entry, which can last 3 minutes with Level-1 already satisfied, and
 * the boot self-test's retry loops. Those callers report the event their own
 * way (a brief message, then redraw their screen) instead of losing it.
 * check_tamper_and_alert() above is simply this plus the standard LCD + buzzer
 * alert, for callers that have nothing more important on screen. */
u8   tamper_poll(void);

/*-------------- Strict password comparison --------------------------------
 * password_match() is the ONLY approved way to check an entered password
 * against a stored one. It replaces the plain strcmp() calls that used to
 * be scattered through projectmain.c and menu.c, and enforces two things
 * strcmp() alone did not make explicit:
 *
 *   1. LENGTH. The entered string must be EXACTLY expected_len characters -
 *      no shorter (a truncated entry), and no longer (so "1234" followed by
 *      anything else can never be accepted as "1234").
 *   2. EVERY CHARACTER. All expected_len characters are compared, one by
 *      one, and the result is accumulated instead of returning early on the
 *      first difference. Comparing at bit level with XOR means the function
 *      takes the same time whether the mismatch is in the first character
 *      or the last, so an attacker cannot learn how much of a guess was
 *      correct by measuring the response time.
 *
 * Returns 1 only on an exact, full-length match; 0 otherwise.
 * ------------------------------------------------------------------------ */
u8 password_match(const char *entered, const char *stored, u8 expected_len);

#endif
