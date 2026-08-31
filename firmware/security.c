/*=============================================================================
 * File        : security.c
 * Project     : Bluetooth-Based Secure Locker with Access Logging
 * Description : Ties together tamper detection, RTC-timestamped access
 *               logging (sent out over UART0 for a PC to capture), and
 *               one-time initialisation of the default EEPROM passwords.
 *
 * Wiring:
 *   Tamper switch -> P0.4, active LOW (switch pulls the pin to GND when
 *                    the enclosure is opened/tampered with; an external
 *                    pull-up resistor keeps the pin HIGH when closed)
 *===========================================================================*/
#include <lpc214x.h>
#include <string.h>
#include "security.h"
#include "defines.h"
#include "lcd.h"
#include "uart.h"
#include "rtc.h"
#include "eeprom.h"
#include "buzzer.h"
#include "delay.h"

#define TAMPER_PIN   (1UL << 4)   /* P0.4 - tamper switch input */

/* Configure the tamper switch pin as a GPIO input. */
void security_init(void)
{
    PINSEL0 &= ~(3UL << 8);   /* P0.4 as plain GPIO (not an alternate function) */
    IO0DIR  &= ~TAMPER_PIN;   /* Configure as input */
}

/* Read the current state of the tamper switch.
 * The switch is active LOW, so a HIGH pin reading means "not tampered"
 * (returns 0) and a LOW reading means "tampered" (returns 1). */
u8 tamper_detected(void)
{
    if (IO0PIN & TAMPER_PIN) return 0;   /* Pin HIGH -> switch closed -> OK       */
    else                     return 1;   /* Pin LOW  -> switch open  -> tampered  */
}

/* Print a single timestamped log line over UART0 in the form:
 * "[DD/MM/YYYY HH:MM:SS] <message>\r\n"
 * This is intended to be captured by a PC terminal for an audit trail. */
void log_event(const char *msg)
{
    char stamp[20];
    rtc_get_stamp(stamp);

    uart0_string("[");
    uart0_string(stamp);
    uart0_string("] ");
    uart0_string(msg);
    uart0_string("\r\n");
}

/* Same timestamped line, but with one string argument appended to the message
 * before the newline. This is what lets the admin menu put the exact value it
 * just wrote into the audit log - e.g.
 *     log_event2("Admin set clock time to ", "12:07:30");
 * without pulling a printf into the firmware. */
void log_event2(const char *msg, const char *arg)
{
    char stamp[20];
    rtc_get_stamp(stamp);

    uart0_string("[");
    uart0_string(stamp);
    uart0_string("] ");
    uart0_string(msg);
    uart0_string(arg);
    uart0_string("\r\n");
}

/* Poll the tamper switch once and report whether this call saw a FRESH
 * tamper event (a not-tampered -> tampered transition, edge-detected via
 * 'old_state' so one physical event is reported once instead of continuously
 * while the switch stays open). The event is written to the audit log here,
 * because that must happen no matter what the caller does next.
 *
 * WHAT THIS FUNCTION DELIBERATELY DOES NOT DO: touch the LCD or the buzzer.
 * That is left to the caller, and it is the whole reason this is split out of
 * check_tamper_and_alert() below.
 *
 * Tamper detection used to be impossible during three long-running phases,
 * precisely because the only available function unconditionally seized the
 * LCD:
 *   - the Level-2 keypad entry (up to 3 minutes, with Level-1 ALREADY
 *     satisfied - the worst possible time to stop watching the enclosure);
 *   - the boot self-test's retry loops, which can run indefinitely if a
 *     module is missing;
 *   - anywhere else that owns the screen.
 * Those callers can now poll for the event and decide for themselves how to
 * report it without destroying whatever they are displaying.
 *
 * Returns 1 on a fresh tamper event, 0 otherwise. */
u8 tamper_poll(void)
{
    static u8 old_state = 0;

    if (tamper_detected())
    {
        if (old_state == 0)   /* Only trigger on the transition into "tampered" */
        {
            old_state = 1;
            log_event("Tamper detected");
            return 1;
        }
    }
    else
    {
        old_state = 0;   /* Switch closed again -> re-arm for the next event */
    }

    return 0;
}

/* Poll the tamper switch and, on a fresh event, put the alert on the LCD and
 * sound the buzzer. Used from the idle/standby paths, which own the screen and
 * have nothing more important to display. */
void check_tamper_and_alert(void)
{
    if (tamper_poll())
    {
        lcd_clear();
        lcd_string("TAMPER ALERT");
        buzzer_alert(5);
    }
}

/*------------------------------------------------------------
 * Strict password comparison.
 *
 * This is the single approved comparison used for BOTH the Level-1
 * Bluetooth password and the Level-2 keypad password (and for the
 * admin menu's password-change screen). See the detailed contract in
 * security.h.
 *
 * The bug this exists to close: the old code compared with a bare
 * strcmp(bt_cmd, l1_pwd). Combined with a receiver that silently threw
 * away everything after the '#' terminator, sending "1234#123456789"
 * produced bt_cmd == "1234", which strcmp() happily matched - so a
 * clearly wrong entry was granted access. The receiver now flags that
 * trailing junk (see bluetooth.c), and this function additionally makes
 * the length requirement explicit and unmissable at the point of
 * comparison, so neither layer is relied on alone.
 *------------------------------------------------------------*/
u8 password_match(const char *entered, const char *stored, u8 expected_len)
{
    u8 i;
    u8 diff = 0U;

    if ((entered == 0) || (stored == 0))
        return 0U;

    /* --- Length gate ---------------------------------------------------
     * Reject anything that is not EXACTLY expected_len characters long.
     * Checked before the value comparison so a short entry can never be
     * read past its terminator, and a long entry can never be accepted
     * on the strength of its first expected_len characters. */
    for (i = 0U; i < expected_len; i++)
    {
        if (entered[i] == '\0')
            return 0U;                  /* Too short */
    }
    if (entered[expected_len] != '\0')
        return 0U;                      /* Too long  */

    /* --- Full character-by-character (bit-level) comparison ------------
     * XOR each pair of characters and OR the differences together. Every
     * character is always examined - there is no early exit - so the
     * execution time does not reveal WHERE the first mismatch was. 'diff'
     * ends up zero only if every single bit of every character matched. */
    for (i = 0U; i < expected_len; i++)
        diff |= (u8)(entered[i] ^ stored[i]);

    return (u8)(diff == 0U);
}

/* On first boot (or after a blank/corrupted EEPROM), write the 4-byte
 * "LKR1" magic marker plus the two factory-default passwords
 * (Level-1/Bluetooth = "1234", Level-2/keypad = "5678") into the EEPROM.
 * On subsequent boots the marker will already match and this function
 * does nothing, preserving any passwords the admin has since changed.
 *
 * The caller must only reach this after the EEPROM has passed its boot
 * self-test (see boot_self_test() in projectmain.c). Without that check a
 * missing EEPROM would read back as garbage, fail the magic-marker test,
 * and then "write" defaults into a device that is not there - leaving the
 * locker with passwords that can never match anything. */
void ensure_default_passwords(void)
{
    char magic[5];

    eeprom_read_str(EEPROM_MAGIC_ADDR, magic, 4);

    if (strcmp(magic, "LKR1") != 0)
    {
        eeprom_write_str(EEPROM_MAGIC_ADDR, "LKR1", 4);
        eeprom_write_str(EEPROM_L1_ADDR, "1234", PWD_LEN);
        eeprom_write_str(EEPROM_L2_ADDR, "5678", PWD_LEN);
        log_event("EEPROM initialized with default passwords");
    }
}
