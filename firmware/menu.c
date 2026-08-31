/*=============================================================================
 * File        : menu.c
 * Project     : Bluetooth-Based Secure Locker with Access Logging
 * Description : Admin configuration menu, entered by pressing an external
 *               push-button wired to trigger EINT2 on P0.7.
 *
 *               MERGE NOTE: the menu feature set below was brought in
 *               from the separate "EnviroTime" project (CLK Setting,
 *               Alarm, and the ATM-style auto-timeout), and rewritten
 *               here to call SecureLocker's own drivers
 *               (lcd_*, keypad_*, buzzer_*, rtc registers, eeprom_*)
 *               instead of EnviroTime's driver API. No other
 *               SecureLocker source file was modified to make this work,
 *               except a single check_alarm() call added to the main
 *               loop in projectmain.c (needed for the alarm to actually
 *               ring - see the note there).
 *
 * MENU STRUCTURE (after the admin button / EINT2 is pressed):
 *   Line1: "1=CLK  2=Alarm"      Line2: "3=Pwd  4=Set"
 *     1 = CLK Setting -> 1=Time (HH:MM:SS) 2=Date (DD/MM/YYYY) 3=Day (0=SUN..6=SAT)
 *     2 = Alarm        -> 1=Set HH:MM:SS   2=Toggle ON/OFF     3 or 'B'=Reset
 *     3 = Password     -> choose L1 (Bluetooth) or L2 (keypad), then
 *                          old/new/confirm, same as the original menu
 *     4 = Set          -> save and return to normal operation
 *   'D' at any point   -> cancel / go back one level
 *   '*'                -> backspace while entering a value
 *   '#'                -> confirm the value being entered
 *
 *   If no key is pressed for MENU_TIMEOUT_MS (15 s) while the top-level
 *   menu is showing, the menu automatically exits back to normal
 *   operation (ATM-style idle timeout).
 *
 *   EVERY SUB-SCREEN BOUNDS ITS OWN WAIT TOO, because that 15-second timeout
 *   covers ONLY the top-level screen. The password fields use
 *   ADMIN_PWD_TIMEOUT_MS (1 min each, with a live countdown in the corner) and
 *   the CLK/Alarm navigation and value-entry screens use
 *   MENU_INPUT_TIMEOUT_MS (1 min). A screen that times out unwinds the WHOLE
 *   menu immediately rather than dropping back one level, so an abandoned admin
 *   session always releases the LCD and returns the locker to normal operation.
 *   These prompts previously used the unconditionally blocking keypad_getkey()
 *   and would sit on an admin screen forever.
 *
 * Wiring:
 *   Admin push-button -> P0.7 (configured as EINT2, falling edge trigger)
 *===========================================================================*/
#include <lpc214x.h>
#include <string.h>
#include "delay.h"
#include "menu.h"
#include "defines.h"
#include "lcd.h"
#include "keypad.h"
#include "rtc.h"
#include "eeprom.h"
#include "buzzer.h"
#include "security.h"

/* Flag raised by the EINT2 ISR; polled by the main loop in
 * projectmain.c to decide when to invoke admin_menu(). */
volatile unsigned char admin_flag = 0;

/*------------------------------------------------------------
 * Alarm state (from EnviroTime). Defined here, declared extern
 * in menu.h so other modules (e.g. projectmain.c) could read it
 * if ever needed.
 *------------------------------------------------------------*/
s32 alarm_hour      = 0;    /* Alarm trigger hour (0-23)      */
s32 alarm_min       = 0;    /* Alarm trigger minute (0-59)    */
s32 alarm_sec       = 0;    /* Alarm trigger second (0-59)    */
u8  alarm_enabled   = 0U;   /* 0 = alarm OFF, 1 = alarm ON    */
u8  alarm_triggered = 0U;   /* 1 = alarm is currently ringing */

/* Time-of-day (seconds since midnight) observed by the previous check_alarm()
 * call, used to detect that the alarm time has been CROSSED rather than
 * requiring the main loop to look at the exact matching second. 0xFFFFFFFF
 * means "no baseline yet". See check_alarm() for the full explanation. */
static u32 alarm_last_secs = 0xFFFFFFFFUL;

/*------------------------------------------------------------
 * Return codes used by the admin menu's input helpers.
 *
 * A timeout has to be distinguishable from a deliberate 'D' cancel: a cancel
 * means "the admin is still here, go back one level", while a timeout means
 * "nobody is here any more", and the whole menu should unwind and release the
 * LCD back to normal operation instead of sitting on a settings screen.
 *------------------------------------------------------------*/
#define MENU_CANCEL     (-1)   /* 'D' pressed - go back one level      */
#define MENU_TIMEDOUT   (-2)   /* No key in time - abandon the session  */

/* Audit-log tag for which stored password is being edited: "L1)" or "L2)".
 * Appended inside parentheses by the password-change log lines, so the log
 * names which of the two passwords the admin actually changed. */
#define PWD_LEVEL_TAG(sel)  ((sel) == '1' ? "L1)" : "L2)")

/*------------------------------------------------------------
 * Private helper prototypes
 *
 * The sub-menu functions return u8: 1 = the admin left normally, 0 = the
 * screen timed out. A 0 is propagated all the way up so one abandoned screen
 * unwinds the entire menu at once, instead of each level having to time out in
 * turn.
 *------------------------------------------------------------*/
static u8   edit_passwords(void);
static u8   read_menu_password(char *buf);
static u8   menu_wait_key(u32 timeout_ms);
static void menu_timeout_notice(const char *log_msg);
static s32  input_value(const char *prompt, s32 minVal, s32 maxVal);
static void lcd_2digit(u8 val);
static u8   edit_time(void);
static u8   edit_date(void);
static u8   edit_day(void);
static u8   clk_setting(void);
static u8   set_alarm_time(void);
static u32  alarm_secs_of_day(const rtc_time *t);
static void alarm_rearm(void);

/* External interrupt 2 (EINT2) handler, fired when the admin push-button
 * is pressed. Just raises a flag and clears the pending interrupt; the
 * actual menu logic runs later from the main loop, not from inside the
 * ISR, to keep the ISR itself short. */
__irq void EINT2_ISR(void)
{
    admin_flag = 1;
    EXTINT = (1UL << ADMIN_EINT_BIT);   /* Clear the EINT2 pending flag */
    VICVectAddr = 0;                     /* Acknowledge to the VIC (end-of-interrupt) */
}

/* Configure P0.7 for its EINT2 alternate function and arm the interrupt:
 * falling-edge triggered (EXTMODE = edge, EXTPOLAR = falling), routed
 * through VIC channel 16 to the EINT2_ISR handler above. */
void admin_int_init(void)
{
    PINSEL0 &= ~(3UL << 14);
    PINSEL0 |=  (3UL << 14);        /* P0.7 -> EINT2 alternate function */

    EXTMODE  |= (1UL << ADMIN_EINT_BIT);   /* Edge-sensitive (not level) */
    EXTPOLAR &= ~(1UL << ADMIN_EINT_BIT);  /* Falling edge (button press pulls the line low) */
    EXTINT    = (1UL << ADMIN_EINT_BIT);   /* Clear any stale pending flag */

    VICIntSelect &= ~(1UL << ADMIN_VIC_SRC);  /* EINT2 (VIC source 16) -> IRQ, not FIQ */
    VICVectAddr2  = (u32)EINT2_ISR;           /* Register the ISR in vectored slot 2   */
    VICVectCntl2  = 0x20 | ADMIN_VIC_SRC;     /* Enable slot 2, assign it to source 16 */
    VICIntEnable  = (1UL << ADMIN_VIC_SRC);   /* Unmask the EINT2 interrupt source     */
}

/* Print a byte as two zero-padded decimal digits (e.g. 7 -> "07"),
 * used for the alarm time display. */
static void lcd_2digit(u8 val)
{
    lcd_data((val / 10) + '0');
    lcd_data((val % 10) + '0');
}

/* Write the three time-of-day fields into 'buf' as "HH:MM:SS" (8 characters
 * plus a NUL terminator). Used to put the exact value the admin just wrote
 * into the audit log, e.g. log_event2("Admin set clock time to ", buf). */
static void fmt_hms(char *buf, u8 h, u8 m, u8 s)
{
    buf[0] = (char)((h / 10) + '0');
    buf[1] = (char)((h % 10) + '0');
    buf[2] = ':';
    buf[3] = (char)((m / 10) + '0');
    buf[4] = (char)((m % 10) + '0');
    buf[5] = ':';
    buf[6] = (char)((s / 10) + '0');
    buf[7] = (char)((s % 10) + '0');
    buf[8] = '\0';
}

/* Write the date fields into 'buf' as "DD/MM/YYYY" (10 characters plus a NUL
 * terminator), for the same audit-log purpose as fmt_hms(). The year digits
 * are expanded the same way rtc_get_stamp() does it. */
static void fmt_dmy(char *buf, u8 d, u8 m, u16 y)
{
    buf[0] = (char)((d / 10) + '0');
    buf[1] = (char)((d % 10) + '0');
    buf[2] = '/';
    buf[3] = (char)((m / 10) + '0');
    buf[4] = (char)((m % 10) + '0');
    buf[5] = '/';
    buf[6] = (char)((y / 1000) + '0');
    buf[7] = (char)(((y / 100) % 10) + '0');
    buf[8] = (char)(((y / 10) % 10) + '0');
    buf[9] = (char)((y % 10) + '0');
    buf[10] = '\0';
}

/*============================================================
 * menu_wait_key  (PRIVATE)
 * Wait for a keypress on an admin screen, giving up after 'timeout_ms'.
 * Returns the key, or 0 if the time ran out.
 *
 * THE BUG THIS FIXES: every navigation prompt in this file used to call the
 * unconditionally blocking keypad_getkey(). The 15-second idle timeout in
 * admin_menu() covers ONLY the top-level menu screen, so the moment the admin
 * pressed '1' (CLK) or '2' (Alarm), control moved into a sub-screen where no
 * timeout applied at all and the locker would sit on an admin settings screen
 * forever if the admin walked away - unable to accept a Bluetooth password,
 * with the tamper switch not being polled and the LCD stranded. Five prompts
 * had this defect: input_value(), edit_time(), edit_date(), clk_setting() and
 * set_alarm_time(). keypad_getkey() no longer exists precisely so that this
 * class of bug cannot come back.
 *
 * DELIBERATELY NO ON-SCREEN COUNTDOWN HERE. The Level-2 entry, the three admin
 * password fields and the lockout screen all show one, because their prompts
 * are short enough to leave the top-right corner free. These CLK/Alarm screens
 * use both display lines in full (e.g. "1=Set 2=On/Off" over
 * "Alm:12:30:00 ON"), and truncating the labels to make room would make the
 * menu harder to read for no safety gain - the timeout itself is what matters
 * here, and it is reported clearly when it fires.
 *============================================================*/
static u8 menu_wait_key(u32 timeout_ms)
{
    return keypad_getkey_timeout(timeout_ms, 0);
}

/* Report an abandoned admin screen on the LCD and in the audit log. */
static void menu_timeout_notice(const char *log_msg)
{
    lcd_clear();
    lcd_string("MENU TIMEOUT");
    lcd_gotoxy(1, 0);
    lcd_string("RETURNING...");
    log_event(log_msg);
    delay_ms(1200);
}

/*============================================================
 * input_value  (PRIVATE)  -- adapted from EnviroTime's InputValue()
 * Shows 'prompt' on LCD line 1, collects digit key presses on
 * line 2, validates against [minVal, maxVal], and returns the
 * value once confirmed with '#'.
 *
 *   '0'-'9' -> append digit      '*' -> backspace
 *   '#'     -> confirm entry     'D' -> cancel (returns MENU_CANCEL)
 *
 * Returns MENU_TIMEDOUT if no key is pressed for MENU_INPUT_TIMEOUT_MS, so the
 * caller can unwind the whole menu instead of treating an abandoned screen the
 * same as a deliberate cancel.
 *============================================================*/
static s32 input_value(const char *prompt, s32 minVal, s32 maxVal)
{
    s32 value    = 0;
    u8  hasInput = 0U;
    char key;

    lcd_clear();
    lcd_gotoxy(0, 0);
    lcd_string(prompt);
    lcd_gotoxy(1, 0);
    lcd_data('>');

    while (1)
    {
        key = (char)menu_wait_key(MENU_INPUT_TIMEOUT_MS);

        if (key == 0)
            return MENU_TIMEDOUT;   /* Nobody is here any more */

        if (key == '#')
        {
            if (hasInput == 0U)
                continue;   /* Nothing entered yet - ignore confirm */

            if ((value >= minVal) && (value <= maxVal))
                return value;

            /* Out of range: show an error, then let the admin retry */
            lcd_clear();
            lcd_gotoxy(0, 0);
            lcd_string("Out of Range!");
            lcd_gotoxy(1, 0);
            lcd_string("Min:");
            lcd_int(minVal);
            lcd_string(" Max:");
            lcd_int(maxVal);
            delay_ms(1500);

            value    = 0;
            hasInput = 0U;
            lcd_clear();
            lcd_gotoxy(0, 0);
            lcd_string(prompt);
            lcd_gotoxy(1, 0);
            lcd_data('>');
        }
        else if (key == '*')
        {
            /* Backspace: drop the last digit and redraw line 2 */
            value /= 10;
            lcd_gotoxy(1, 0);
            lcd_string("     ");
            lcd_gotoxy(1, 0);
            lcd_data('>');
            if (value > 0)
                lcd_int(value);
            else
                hasInput = 0U;
        }
        else if (key == 'D')
        {
            return MENU_CANCEL;   /* Cancel */
        }
        else if ((key >= '0') && (key <= '9'))
        {
            /* Accept at most 4 digits (the widest value any caller asks for is
             * a 4-digit year). The guard used to be "value < 9999", which let a
             * 4-digit value grow to 5 digits - ">99989" is 6 characters and
             * overflowed the 5-character field cleared on line 2, leaving a
             * stray digit on screen. */
            if (value <= 999)
            {
                value    = (value * 10) + (key - '0');
                hasInput = 1U;
                lcd_gotoxy(1, 0);
                lcd_data('>');
                lcd_int(value);
            }
        }
        /* Any other key is ignored */
    }
}

/* CLK Setting sub-menu: edit RTC hour/minute/second.
 * Returns 1 on a normal exit, 0 if the screen timed out.
 *
 * The chosen field is applied through rtc_set_time(), which pauses the RTC
 * (CCR CLKEN) around the update. The old code assigned straight to the HOUR /
 * MIN / SEC registers with the clock still running, which can race with a
 * rollover happening in that same instant. */
static u8 edit_time(void)
{
    s32 val;
    char key;
    rtc_time now;
    char tbuf[9];   /* "HH:MM:SS" for the audit log */

    while (1)
    {
        lcd_clear();
        lcd_string("1H 2M 3S");
        lcd_gotoxy(1, 0);
        lcd_string("D=Back");

        key = (char)menu_wait_key(MENU_INPUT_TIMEOUT_MS);

        if (key == 0)
        {
            menu_timeout_notice("Admin menu abandoned at the Set Time screen");
            return 0;
        }

        switch (key)
        {
            case '1':
                val = input_value("Set Hour(0-23)", 0, 23);
                if (val == MENU_TIMEDOUT) return 0;
                if (val >= 0)
                {
                    rtc_get(&now);
                    rtc_set_time((u8)val, now.min, now.sec);
                    fmt_hms(tbuf, (u8)val, now.min, now.sec);
                    log_event2("Admin set clock time to ", tbuf);
                }
                break;
            case '2':
                val = input_value("Set Min(0-59)", 0, 59);
                if (val == MENU_TIMEDOUT) return 0;
                if (val >= 0)
                {
                    rtc_get(&now);
                    rtc_set_time(now.hour, (u8)val, now.sec);
                    fmt_hms(tbuf, now.hour, (u8)val, now.sec);
                    log_event2("Admin set clock time to ", tbuf);
                }
                break;
            case '3':
                val = input_value("Set Sec(0-59)", 0, 59);
                if (val == MENU_TIMEDOUT) return 0;
                if (val >= 0)
                {
                    rtc_get(&now);
                    rtc_set_time(now.hour, now.min, (u8)val);
                    fmt_hms(tbuf, now.hour, now.min, (u8)val);
                    log_event2("Admin set clock time to ", tbuf);
                }
                break;
            case 'D':
                return 1;
            default:
                break;
        }
    }
}

/* CLK Setting sub-menu: edit RTC day-of-month/month/year.
 * Returns 1 on a normal exit, 0 if the screen timed out. Writes go through
 * rtc_set_date(), which pauses the RTC around the update. */
static u8 edit_date(void)
{
    s32 val;
    char key;
    rtc_time now;
    char dbuf[11];   /* "DD/MM/YYYY" for the audit log */

    while (1)
    {
        lcd_clear();
        lcd_string("1D 2M 3Y");
        lcd_gotoxy(1, 0);
        lcd_string("D=Back");

        key = (char)menu_wait_key(MENU_INPUT_TIMEOUT_MS);

        if (key == 0)
        {
            menu_timeout_notice("Admin menu abandoned at the Set Date screen");
            return 0;
        }

        switch (key)
        {
            case '1':
                val = input_value("Set Date(1-31)", 1, 31);
                if (val == MENU_TIMEDOUT) return 0;
                if (val >= 0)
                {
                    rtc_get(&now);
                    rtc_set_date((u8)val, now.month, now.year);
                    fmt_dmy(dbuf, (u8)val, now.month, now.year);
                    log_event2("Admin set clock date to ", dbuf);
                }
                break;
            case '2':
                val = input_value("Set Month(1-12)", 1, 12);
                if (val == MENU_TIMEDOUT) return 0;
                if (val >= 0)
                {
                    rtc_get(&now);
                    rtc_set_date(now.dom, (u8)val, now.year);
                    fmt_dmy(dbuf, now.dom, (u8)val, now.year);
                    log_event2("Admin set clock date to ", dbuf);
                }
                break;
            case '3':
                val = input_value("Set Year(2000-)", 2000, 4095);
                if (val == MENU_TIMEDOUT) return 0;
                if (val >= 0)
                {
                    rtc_get(&now);
                    rtc_set_date(now.dom, now.month, (u16)val);
                    fmt_dmy(dbuf, now.dom, now.month, (u16)val);
                    log_event2("Admin set clock date to ", dbuf);
                }
                break;
            case 'D':
                return 1;
            default:
                break;
        }
    }
}

/* CLK Setting sub-menu: edit the RTC day-of-week (0=SUN .. 6=SAT).
 * Returns 1 on a normal exit, 0 if the input screen timed out. */
static u8 edit_day(void)
{
    s32 val;
    char d[2];   /* "0".."6" as a NUL-terminated string for the audit log */

    lcd_clear();
    lcd_string("0=SUN 6=SAT");
    delay_ms(1000);

    val = input_value("Set Day(0-6)", 0, 6);

    if (val == MENU_TIMEDOUT)
        return 0;

    if (val >= 0)
    {
        rtc_set_dow((u8)val);

        d[0] = (char)(val + '0');
        d[1] = '\0';
        log_event2("Admin set day of week to ", d);

        lcd_clear();
        lcd_string("Day Updated!");
        delay_ms(1000);
    }

    return 1;
}

/* Unified "CLK Setting" sub-menu consolidating Time/Date/Day.
 * Returns 1 on a normal exit, 0 if any screen below it timed out - which is
 * propagated so the whole admin menu unwinds at once. */
static u8 clk_setting(void)
{
    char key;

    while (1)
    {
        lcd_clear();
        lcd_string("1=Time 2=Date");
        lcd_gotoxy(1, 0);
        lcd_string("3=Day  D=Back");

        key = (char)menu_wait_key(MENU_INPUT_TIMEOUT_MS);

        if (key == 0)
        {
            menu_timeout_notice("Admin menu abandoned at the CLK Setting screen");
            return 0;
        }

        switch (key)
        {
            case '1':
                log_event("Admin menu: CLK setting - Time selected");
                if (!edit_time()) return 0;
                break;
            case '2':
                log_event("Admin menu: CLK setting - Date selected");
                if (!edit_date()) return 0;
                break;
            case '3':
                log_event("Admin menu: CLK setting - Day selected");
                if (!edit_day())  return 0;
                break;
            case 'D': return 1;
            default:  break;
        }
    }
}

/* Alarm sub-menu: set the alarm time, toggle it on/off, or reset it.
 * Returns 1 on a normal exit, 0 if a screen timed out. */
static u8 set_alarm_time(void)
{
    s32 val;
    char key;
    char tbuf[9];   /* "HH:MM:SS" for the audit log */

    while (1)
    {
        lcd_clear();
        lcd_string("1=Set 2=On/Off");
        lcd_gotoxy(1, 0);
        lcd_string("Alm:");
        lcd_2digit((u8)alarm_hour);
        lcd_data(':');
        lcd_2digit((u8)alarm_min);
        lcd_data(':');
        lcd_2digit((u8)alarm_sec);
        lcd_data(' ');
        lcd_string(alarm_enabled ? "ON" : "OFF");

        key = (char)menu_wait_key(MENU_INPUT_TIMEOUT_MS);

        if (key == 0)
        {
            menu_timeout_notice("Admin menu abandoned at the Alarm screen");
            return 0;
        }

        switch (key)
        {
            case '1':
                val = input_value("Alarm Hr(0-23)", 0, 23);
                if (val == MENU_TIMEDOUT) return 0;
                if (val >= 0)
                {
                    alarm_hour = val;
                    val = input_value("Alarm Min(0-59)", 0, 59);
                    if (val == MENU_TIMEDOUT) return 0;
                    if (val >= 0)
                    {
                        alarm_min = val;
                        val = input_value("Alarm Sec(0-59)", 0, 59);
                        if (val == MENU_TIMEDOUT) return 0;
                        if (val >= 0)
                        {
                            alarm_sec     = val;
                            alarm_enabled = 1U;
                            alarm_rearm();      /* Fresh baseline: see check_alarm() */

                            fmt_hms(tbuf, (u8)alarm_hour, (u8)alarm_min, (u8)alarm_sec);
                            log_event2("Admin set alarm time to ", tbuf);

                            lcd_clear();
                            lcd_string("Alarm Set:");
                            lcd_gotoxy(1, 0);
                            lcd_2digit((u8)alarm_hour);
                            lcd_data(':');
                            lcd_2digit((u8)alarm_min);
                            lcd_data(':');
                            lcd_2digit((u8)alarm_sec);
                            delay_ms(1500);
                        }
                    }
                }
                break;

            case '2':
                alarm_enabled = (alarm_enabled == 0U) ? 1U : 0U;
                alarm_rearm();
                buzzer_off();

                log_event(alarm_enabled ? "Admin enabled the alarm"
                                        : "Admin disabled the alarm");

                lcd_clear();
                lcd_string("Alarm:");
                lcd_string(alarm_enabled ? "ENABLED" : "DISABLED");
                delay_ms(1000);
                break;

            case '3':
            case 'B':
                alarm_hour    = 0;
                alarm_min     = 0;
                alarm_sec     = 0;
                alarm_enabled = 0U;
                alarm_rearm();
                buzzer_off();

                log_event("Admin reset the alarm (time cleared, alarm disabled)");

                lcd_clear();
                lcd_string("Alarm Reset!");
                lcd_gotoxy(1, 0);
                lcd_string("Alarm OFF");
                delay_ms(1200);
                break;

            case 'D':
                return 1;

            default:
                break;
        }
    }
}

/*============================================================
 * read_menu_password
 * Read one PWD_LEN-digit password field for the password-change screen,
 * masked with '*' on the LCD.
 *
 * THE BUG THIS FIXES: these fields used to be read with a bare
 *   for (i = 0; i < PWD_LEN; i++) { oldp[i] = keypad_getkey(); lcd_data('*'); }
 * which accepted ANY key as a password character - so 'A'-'D', '*' and '#'
 * were all silently stored as if they were digits, with no way to correct a
 * mistake. Since passwords themselves are always digits, a single stray
 * keypress made the entry unmatchable, and the user's only clue was "WRONG
 * OLD". That is almost certainly the cause of the long run of "Password
 * change failed: wrong old password" entries in the captured log (1.TXT):
 * eleven consecutive failures, then one success.
 *
 * Now it behaves like the Level-2 prompt in projectmain.c:
 *   0-9 -> accept the digit
 *   '*' -> backspace
 *   '#' -> clear the whole field
 *   A-D -> ignored (do not consume a digit position)
 *
 * Returns 1 when PWD_LEN digits have been entered, or 0 if the field timed
 * out (ADMIN_PWD_TIMEOUT_MS for the whole field), so the menu can never sit
 * waiting forever on a half-typed password either.
 *
 * The seconds remaining are shown live in the top-right corner while the admin
 * types, the same way the Level-2 prompt does it - the prompts here ("OLD
 * PWD:", "NEW PWD:", "CONFIRM:") are short, so the corner is free.
 *============================================================*/
static u8 read_menu_password(char *buf)
{
    u32 start = millis();
    u32 shown = 0xFFFFFFFFUL;   /* Last countdown value drawn; forces a first draw */
    u32 left;
    u32 secs;
    u8  i = 0;
    u8  k;

    /* Fixed part of the countdown field, drawn once. */
    lcd_gotoxy(0, LCD_COUNTDOWN_UNIT);
    lcd_data('s');
    lcd_gotoxy(1, 0);

    while (i < PWD_LEN)
    {
        /* --- redraw the countdown only when the whole second changes ------
         * Rounded UP, so the display reaches 0 exactly when the field really
         * expires rather than a second early. */
        left = elapsed_since(start);
        left = (left >= ADMIN_PWD_TIMEOUT_MS) ? 0UL : (ADMIN_PWD_TIMEOUT_MS - left);
        secs = (left + 999UL) / 1000UL;

        if (secs != shown)
        {
            shown = secs;
            lcd_gotoxy(0, LCD_COUNTDOWN_COL);
            lcd_uint_pad(secs, 3);
            lcd_gotoxy(1, i);        /* Put the cursor back where the mask goes */
        }

        if (left == 0UL)
            return 0;                /* Field timed out */

        /* Poll the matrix directly rather than calling keypad_getkey_timeout()
         * with the whole remaining budget: that would not return until a key
         * was pressed or the entire minute had elapsed, so the countdown on
         * screen would freeze. Scanning here keeps the loop turning every
         * KEY_POLL_MS, which is what lets the display refresh every second.
         * keypad_scan() still does its own debounce and wait-for-release. */
        k = keypad_scan();

        if (k == 0)
        {
            delay_ms(KEY_POLL_MS);
            continue;                /* Nothing yet - loop to refresh the countdown */
        }

        if (k >= '0' && k <= '9')
        {
            buf[i++] = (char)k;
            lcd_data('*');
        }
        else if (k == '*')               /* Backspace */
        {
            if (i > 0)
            {
                i--;
                buf[i] = '\0';
                lcd_gotoxy(1, i);
                lcd_data(' ');
                lcd_gotoxy(1, i);
            }
        }
        else if (k == '#')               /* Clear the whole field */
        {
            while (i > 0)
            {
                i--;
                buf[i] = '\0';
                lcd_gotoxy(1, i);
                lcd_data(' ');
            }
            lcd_gotoxy(1, 0);
        }
        /* A-D: ignored */
    }

    buf[PWD_LEN] = '\0';
    return 1;
}

/* Password sub-menu: choose which stored password to change (L1 =
 * Bluetooth, L2 = keypad), verify the current password, then read and
 * confirm a new one before committing it to EEPROM.
 *
 * All three fields are now read through read_menu_password() (digit
 * filtering, editing keys, a per-field timeout and a live countdown), and all
 * comparisons go through password_match() so the same strict length + full
 * character-by-character rule applies here as at the two login levels.
 *
 * Returns 1 on a normal exit, 0 if a screen timed out (propagated so the whole
 * admin menu unwinds immediately instead of waiting out another idle period on
 * the top-level screen). */
static u8 edit_passwords(void)
{
    char oldp[PWD_LEN + 1];
    char newp[PWD_LEN + 1];
    char conf[PWD_LEN + 1];
    char cur[PWD_LEN + 1];
    unsigned int addr;
    char sel;

    lcd_clear();
    lcd_string("1:L1 2:L2");

    log_event("Password change opened from the admin menu");

    /* Bounded, for the same reason as the three password fields below: the
     * 15-second idle timeout in admin_menu() only covers the TOP-LEVEL menu
     * screen. Once '3' has been pressed we are inside this function, so
     * without a limit here the locker would sit on an admin screen forever
     * if the admin walked away after selecting "3=Pwd". */
    sel = (char)menu_wait_key(ADMIN_PWD_TIMEOUT_MS);

    if (sel == 0)
    {
        menu_timeout_notice("Password change abandoned: no L1/L2 selection made");
        return 0;
    }

    if (sel == '1')      addr = EEPROM_L1_ADDR;
    else if (sel == '2') addr = EEPROM_L2_ADDR;
    else                 return 1;

    log_event2("Password change: ", (sel == '1')
                ? "Level-1 (Bluetooth) password selected"
                : "Level-2 (keypad) password selected");

    eeprom_read_str(addr, cur, PWD_LEN);

    lcd_clear();
    lcd_string("OLD PWD:");
    lcd_gotoxy(1,0);
    if (!read_menu_password(oldp))
    {
        menu_timeout_notice("Password change abandoned: old-password entry timed out");
        return 0;
    }

    if (!password_match(oldp, cur, PWD_LEN))
    {
        lcd_clear();
        lcd_string("WRONG OLD");
        log_event2("Password change failed: wrong old password (", PWD_LEVEL_TAG(sel));
        buzzer_alert(3);
        delay_ms(1000);
        return 1;
    }

    lcd_clear();
    lcd_string("NEW PWD:");
    lcd_gotoxy(1,0);
    if (!read_menu_password(newp))
    {
        menu_timeout_notice("Password change abandoned: new-password entry timed out");
        return 0;
    }

    lcd_clear();
    lcd_string("CONFIRM:");
    lcd_gotoxy(1,0);
    if (!read_menu_password(conf))
    {
        menu_timeout_notice("Password change abandoned: confirmation entry timed out");
        return 0;
    }

    if (password_match(newp, conf, PWD_LEN))
    {
        char readback[PWD_LEN + 1];

        /* Write the new password to the AT24C256 EEPROM. EEPROM is
         * non-volatile, so this survives a power cycle/reset - but as
         * a safety check, immediately read it back and compare before
         * declaring success, in case of a bus glitch during the write. */
        eeprom_write_str(addr, newp, PWD_LEN);
        eeprom_read_str(addr, readback, PWD_LEN);

        if (password_match(readback, newp, PWD_LEN))
        {
            lcd_clear();
            lcd_string("PWD UPDATED");
            log_event2("Password updated and verified in EEPROM (", PWD_LEVEL_TAG(sel));
        }
        else
        {
            /* Read-back didn't match what was written - retry once
             * before giving up, since this should be rare. */
            eeprom_write_str(addr, newp, PWD_LEN);
            eeprom_read_str(addr, readback, PWD_LEN);

            if (password_match(readback, newp, PWD_LEN))
            {
                lcd_clear();
                lcd_string("PWD UPDATED");
                log_event2("Password updated and verified in EEPROM after retry (", PWD_LEVEL_TAG(sel));
            }
            else
            {
                lcd_clear();
                lcd_string("EEPROM WRITE FAIL");
                log_event2("Password update FAILED: EEPROM read-back mismatch (", PWD_LEVEL_TAG(sel));
                buzzer_alert(3);
            }
        }
    }
    else
    {
        lcd_clear();
        lcd_string("MISMATCH");
        log_event2("Password update failed: mismatch (", PWD_LEVEL_TAG(sel));
        buzzer_alert(3);
    }

    delay_ms(1000);
    return 1;
}

/* Top-level admin menu, entered from the main loop whenever admin_flag
 * is set (i.e. the admin push-button was pressed). Presents the
 * 4-option menu merged in from EnviroTime and dispatches to the
 * relevant sub-menu. If no key is pressed for MENU_TIMEOUT_MS (15 s) while
 * this top-level menu is showing, it exits automatically (ATM-style idle
 * timeout).
 *
 * That idle timeout covers ONLY this screen - every sub-menu bounds its own
 * waits (see menu_wait_key()). A sub-menu that times out returns 0, and this
 * function then leaves the menu altogether rather than parking on the
 * top-level screen for another 15 seconds. */
void admin_menu(void)
{
    char key;
    u8   stay      = 1U;
    u8   timed_out = 0U;
    u32  idleCount = 0U;

    admin_flag = 0;

    /* The admin push-button (EINT2) opened the menu. Record the entry so the
     * audit log captures the whole menu session - this is the "interrupt
     * button pressed, menu loaded" event the faculty asked to be tracked. */
    log_event("Admin menu opened (admin button pressed)");

    while (stay == 1U)
    {
        lcd_clear();
        lcd_string("1=CLK  2=Alarm");
        lcd_gotoxy(1,0);
        lcd_string("3=Pwd  4=Set");

        /* Idle-wait for a key press, using the non-blocking keypad_scan()
         * so the 15-second auto-timeout can be measured. */
        idleCount = 0U;
        key = 0;
        while (key == 0)
        {
            key = (char)keypad_scan();
            if (key != 0)
                break;

            delay_ms(MENU_POLL_MS);
            idleCount++;
            if (idleCount >= (MENU_TIMEOUT_MS / MENU_POLL_MS))
            {
                lcd_clear();
                lcd_string("Menu Timeout");
                lcd_gotoxy(1,0);
                lcd_string("Returning...");
                delay_ms(1200);
                admin_flag = 0;
                log_event("Admin menu exit: idle timeout");
                return;
            }
        }

        switch (key)
        {
            case '1':
                log_event("Admin menu: option 1 = CLK setting selected");
                if (!clk_setting())
                    timed_out = 1U;
                break;

            case '2':
                log_event("Admin menu: option 2 = Alarm selected");
                if (!set_alarm_time())
                    timed_out = 1U;
                break;

            case '3':
                log_event("Admin menu: option 3 = Password change selected");
                if (!edit_passwords())
                    timed_out = 1U;
                break;

            case '4':
                log_event("Admin menu: option 4 = Set (save & return) selected");
                lcd_clear();
                lcd_string("Settings Saved");
                lcd_gotoxy(1,0);
                lcd_string("Returning...");
                buzzer_on();
                delay_ms(150);
                buzzer_off();
                delay_ms(1200);
                stay = 0U;
                break;

            case 'D':
                log_event("Admin menu: exit requested by admin (D)");
                stay = 0U;
                break;

            default:
                break;
        }

        if (timed_out)
            stay = 0U;   /* A sub-screen was abandoned - leave the menu entirely */
    }

    log_event(timed_out ? "Admin menu exit: sub-screen timeout"
                        : "Admin menu exit");
    admin_flag = 0;
}

/*============================================================
 * check_alarm
 * Called from the main loop. Rings the buzzer once the stored alarm time is
 * reached, and keeps it ringing until the admin disables or resets the alarm
 * from the Alarm sub-menu.
 *
 * THE BUG THIS FIXES: this used to require an EXACT hour/minute/second match
 *      if ((curHour == alarm_hour) && (curMin == alarm_min) && (curSec == alarm_sec))
 * which only works if the main loop happens to look during the one second the
 * alarm is due. It regularly does not: open_locker_sequence() blocks for about
 * 6 seconds, the Level-2 keypad entry for up to 3 minutes, a lockout for 30
 * seconds, and the boot self-test for longer still. If the due second passed
 * inside any of those, the alarm was missed COMPLETELY and then never fired at
 * all - the one job it had.
 *
 * Now the alarm fires when the due time has been CROSSED. Each call converts
 * the current time to seconds-since-midnight and compares against the previous
 * call's value: if the alarm time lies anywhere in the interval that just
 * elapsed, it fires - however long that interval was. So an alarm due while the
 * locker was busy for 3 minutes still rings, a few seconds late, instead of
 * being skipped.
 *
 * The two-branch test handles time running BACKWARDS as well, which happens at
 * midnight and whenever the admin sets the clock back from the CLK menu: in
 * that case the elapsed interval is "from the previous reading to the end of
 * the day" plus "from midnight to now".
 *
 * alarm_last_secs is refreshed on every call - including the calls that return
 * early because the alarm is off or already ringing - so it can never grow
 * stale and cause a spurious "crossing" the next time the alarm is armed.
 *============================================================*/

/* Seconds since midnight for one tear-free RTC snapshot. */
static u32 alarm_secs_of_day(const rtc_time *t)
{
    return ((u32)t->hour * 3600UL) + ((u32)t->min * 60UL) + (u32)t->sec;
}

/* Re-arm the alarm: clear the "already ringing" latch and take a fresh
 * time baseline, so arming or changing the alarm can never be interpreted as
 * having just crossed the new time. Called from every place in the Alarm
 * sub-menu that changes the alarm settings. */
static void alarm_rearm(void)
{
    rtc_time now;

    rtc_get(&now);
    alarm_last_secs = alarm_secs_of_day(&now);
    alarm_triggered = 0U;
}

void check_alarm(void)
{
    rtc_time now;
    u32 cur;
    u32 target;
    u8  fired = 0U;

    rtc_get(&now);
    cur = alarm_secs_of_day(&now);

    if ((alarm_enabled == 0U) || (alarm_triggered == 1U))
    {
        alarm_last_secs = cur;   /* Keep the baseline fresh even while idle */

        /* Hold the alarm ringing. Without this, any unrelated buzzer_alert()
         * (a wrong password, a tamper event) ends with buzzer_off() and would
         * silently switch off an alarm nobody has acknowledged. */
        if ((alarm_enabled == 1U) && (alarm_triggered == 1U))
            buzzer_on();

        return;
    }

    if (alarm_last_secs == 0xFFFFFFFFUL)
    {
        alarm_last_secs = cur;   /* First call: establish a baseline, fire nothing */
        return;
    }

    target = ((u32)alarm_hour * 3600UL) + ((u32)alarm_min * 60UL) + (u32)alarm_sec;

    if (alarm_last_secs <= cur)
    {
        /* Normal case: time moved forward within the same day. Did the alarm
         * time fall inside the interval that just elapsed? */
        fired = (u8)((target > alarm_last_secs) && (target <= cur));
    }
    else
    {
        /* Time moved backwards: midnight rollover, or the admin set the clock
         * back. The elapsed interval wraps, so the alarm time counts as crossed
         * if it is after the previous reading OR at/before the new one. */
        fired = (u8)((target > alarm_last_secs) || (target <= cur));
    }

    alarm_last_secs = cur;

    if (fired)
    {
        alarm_triggered = 1U;

        lcd_clear();
        lcd_string("** ALARM!! **");
        lcd_gotoxy(1,0);
        lcd_string("Admin=Stop");

        log_event("Alarm time reached: buzzer ON until the admin stops it");
        buzzer_on();
    }
}
