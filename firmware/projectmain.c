/*=============================================================================
 * File        : projectmain.c
 * Project     : Bluetooth-Based Secure Locker with Access Logging
 * Target      : NXP/Philips LPC2148 (ARM7TDMI-S) @ 60 MHz core clock
 *
 * SYSTEM OVERVIEW
 * ----------------
 * A two-factor locker: the user first sends a 4-digit password from a
 * phone over Bluetooth (Level-1), and if correct is then prompted to
 * enter a second 4-digit password on a physical keypad (Level-2). Only
 * when both match does the microcontroller drive a DC motor (via an
 * L293D H-bridge) to open, then automatically close, the locker. Every
 * significant event (boot, tamper, password results, locker open/close,
 * admin actions) is timestamped using the on-chip RTC and streamed out
 * over UART0 as a simple audit log for a PC to capture.
 *
 * An admin push-button (external interrupt EINT2) opens a separate menu
 * (see menu.c) for CLK Setting (time/date/day), an Alarm (checked every
 * main-loop pass via check_alarm()), and changing either stored password;
 * a tamper switch is continuously monitored and triggers an alarm (LCD
 * message + buzzer + log entry) if the enclosure is opened. The CLK
 * Setting/Alarm menu was merged in from the separate "EnviroTime"
 * project and rewritten to use this project's own LCD/keypad/buzzer/
 * RTC/EEPROM drivers; no other SecureLocker file needed to change for
 * that merge beyond the single check_alarm() call in the main loop below.
 *
 * At power-up the LCD splash screen reads "Bluetooth" / "Secure System".
 *
 * PIN CONNECTIONS
 * ----------------
 *   LCD (16x2, 4-bit mode):
 *     RS               -> P0.16
 *     EN               -> P0.17
 *     D4 .. D7         -> P0.18 .. P0.21
 *
 *   UART0 (PC debug / access-log console):
 *     TXD0             -> P0.0
 *     RXD0             -> P0.1
 *
 *   I2C0 (shared 2-wire bus on P0.2 / P0.3):
 *     AT24C256 EEPROM at 0xA0 - stores the two passwords:
 *       SCL0           -> P0.2
 *       SDA0           -> P0.3
 *       EEPROM VCC     -> 3V3
 *       (A0/A1/A2 -> GND, WP -> GND, GND -> GND)
 *
 *     DS1307 / DS3231 external RTC at 0xD0 - OPTIONAL but REQUIRED for the
 *     clock to keep real time through a power-off (see "RTC / EXTERNAL
 *     CLOCK" below). Shares the same SCL0/SDA0 pair as the EEPROM:
 *       SCL0           -> P0.2      (shared with the EEPROM)
 *       SDA0           -> P0.3      (shared with the EEPROM)
 *       VCC            -> 3V3
 *       GND            -> GND
 *       VBAT           -> Semos coin cell (+)   (cell GND -> GND)
 *       X1 / X2        -> 32.768 kHz crystal    (DS1307 ONLY - the DS3231
 *                           has a built-in temperature-compensated crystal,
 *                           so it needs NO external crystal at all)
 *       (Neither part has address-select pins - the I2C address is fixed at
 *        0x68 = write byte 0xD0, read byte 0xD1. Only the EEPROM has A0-A2.)
 *
 *   Tamper switch (active LOW, external pull-up to 3V3):
 *     Signal           -> P0.4
 *
 *   Admin push-button (EINT2, falling edge):
 *     Signal           -> P0.7
 *
 *   UART1 / HC-05 Bluetooth module:
 *     TXD1 (MCU -> HC-05 RXD) -> P0.8
 *     RXD1 (MCU <- HC-05 TXD) -> P0.9
 *     HC-05 GND        -> GND
 *     HC-05 VCC        -> 5V
 *     HC-05 KEY/EN     -> P0.6, OPTIONAL and NOT required. Only needed if you
 *                         want the boot self-test to interrogate the module
 *                         itself; see BT_KEY_CTRL_ENABLED in defines.h.
 *
 *   4x4 matrix keypad:
 *     Rows (outputs)   -> P1.16 .. P1.19
 *     Cols (inputs)    -> P1.20 .. P1.23
 *
 *   DC motor (locker latch actuator, via L293D H-bridge):
 *     IN1              -> P1.24
 *     IN2              -> P1.25
 *
 *   Buzzer (audible alert):
 *     Signal           -> P1.26
 *
 * RTC / EXTERNAL CLOCK - HOW TO CONFIGURE AND WIRE IT
 * ---------------------------------------------------
 * The LPC2148's ON-CHIP RTC is clocked from PCLK only: it has NO dedicated
 * 32.768 kHz crystal pins (no RTCX1/RTCX2) and NO battery-backup (VBAT) pin,
 * so it FREEZES during a full power-off. That is the hardware reason the
 * clock used to "come back at 12:00 PM" every time the board was unplugged -
 * the firmware also used to reset it to 12:00 on every boot, which is now
 * fixed (the default is applied only when the RTC is completely unset).
 *
 * To make the clock genuinely survive a power-off ("real RTC using the
 * external clock"), fit a DS1307 or DS3231 RTC chip on the I2C0 bus (see the
 * PIN CONNECTIONS above) and wire the vector board's coin cell to its VBAT.
 * Both parts are battery-backed, crystal-driven and I2C at address 0xD0.
 *
 * THE ONLY CONFIGURATION SWITCH IS IN defines.h:
 *     RTC_EXT_ENABLED - 1 (DEFAULT) = probe for the external RTC at boot and
 *                       use it if found. It is AUTO-DETECTED, so wiring the
 *                       chip up is all that is needed - no code change.
 *                       0 = never touch the I2C bus for the RTC; the on-chip
 *                       RTC is the only clock and cannot survive power-off.
 *
 * Where the code lives:
 *     rtc_init()           (rtc.c) probes 0xD0, restores a valid battery-
 *                          backed time into the on-chip RTC, and mirrors every
 *                          clock write to the external RTC.
 *     rtc_battery_backed() (rtc.c) reports whether the external RTC was found.
 *     The UART0 boot log prints which case the system is in (look for "RTC:").
 *
 * DEFAULT / FACTORY PASSWORDS (written to EEPROM on first boot only)
 * ----------------
 *   Level-1 (Bluetooth, sent as "1234#" from the phone app) = 1234
 *   Level-2 (keypad, entered after Level-1 succeeds)          = 5678
 *
 * BOOT SELF-TEST
 * ----------------
 * Before the locker will run, boot_self_test() verifies the two external
 * modules it depends on:
 *
 *   AT24C256 I2C EEPROM - probed for an address acknowledge, then read/write
 *     verified with two complementary patterns. A HARD requirement: both
 *     passwords live there, so a failure parks on "I2C EEPROM NOT CONFIGURED"
 *     and re-tests until the module is reseated.
 *
 *   Bluetooth interface - the UART1 link is proven with an INTERNAL LOOPBACK
 *     test (deterministic), and the HC-05 is additionally probed with "AT"
 *     purely as extra evidence. An unanswered "AT" is NOT a failure: an HC-05
 *     whose KEY/EN pin is not wired is permanently in data mode and never
 *     answers AT commands, which is why the old check reported "HC-05 NOT
 *     CONFIGURED" on perfectly good hardware. See the BOOT SELF-TEST comment
 *     block in bluetooth.c for the full explanation.
 *
 * PASSWORD VALIDATION RULES
 * ----------------
 * A Level-1 transmission is accepted only if it is EXACTLY PWD_LEN
 * characters followed by the '#' terminator. Specifically rejected:
 *   - a payload longer than BT_MAX_PAYLOAD characters (BT_RX_OVERFLOW)
 *   - any extra characters after the '#' in the same burst, e.g.
 *     "1234#123456789" (BT_RX_TRAILING) - this used to be granted access
 *   - any length other than PWD_LEN, enforced by password_match()
 * Both levels compare every character with password_match() (security.c).
 *
 * TIMEOUTS (all measured against the Timer1 millisecond time base, millis())
 * ----------------
 * The Level-2 keypad prompt runs TWO limits at once, and whichever expires
 * first ends the entry and returns the system to the Level-1 (Bluetooth)
 * locked state:
 *   - L2_TOTAL_TIMEOUT_MS    (3 min) - hard ceiling on the whole entry, never
 *                                      extended by anything the user does.
 *   - L2_INTERKEY_TIMEOUT_MS (1 min) - how long it will wait for the NEXT
 *                                      character; restarts from a full minute
 *                                      on every keypress.
 * The seconds left before the earlier of the two expires are shown live in the
 * top-right corner of the LCD, with the remaining total on the second line, so
 * the user is never cut off without warning. On expiry the Bluetooth password
 * must be sent again before the keypad is offered a second time.
 *
 * Every other wait in the system is bounded too: the admin password fields
 * (ADMIN_PWD_TIMEOUT_MS, also with a countdown), the admin CLK/Alarm screens
 * (MENU_INPUT_TIMEOUT_MS), the top-level admin menu (MENU_TIMEOUT_MS), the
 * Bluetooth burst settle (BT_SETTLE_MAX_MS), the I2C transfers
 * (I2C_WAIT_LIMIT) and the UART transmit waits (UART_TX_WAIT_LIMIT).
 *===========================================================================*/
#include <lpc214x.h>
#include <string.h>

#include "types.h"
#include "defines.h"
#include "delay.h"
#include "lcd.h"
#include "keypad.h"
#include "buzzer.h"
#include "motor.h"
#include "uart.h"
#include "bluetooth.h"
#include "eeprom.h"
#include "rtc.h"
#include "security.h"
#include "menu.h"

static void pll_feed(void);
static void SystemInit_SecureLocker(void);
static void boot_self_test(void);
static void print2(u8 val);
static void display_live_clock(void);
static void DisplayStandby(void);
static void DisplayAccessGranted(void);
static void DisplayAccessDenied(const char *reason);
static void l2_draw_prompt(u8 digits_entered);
static void l2_draw_countdown(u32 key_left_ms, u32 total_left_ms, u8 digits_entered);
static u8   read_keypad_password(char *buf);
static void open_locker_sequence(void);
static void system_lockout(void);
static void register_failed_attempt(void);

/* Result codes from read_keypad_password(). The two timeout cases are kept
 * separate so the audit log can say WHICH limit ran out - "the user stopped
 * typing" and "the user took too long overall" are different events, and
 * telling them apart is the difference between a useful log and a vague one. */
#define L2_ENTRY_OK         0U   /* PWD_LEN digits entered                      */
#define L2_ENTRY_IDLE_OUT   1U   /* No keypress for L2_INTERKEY_TIMEOUT_MS      */
#define L2_ENTRY_TOTAL_OUT  2U   /* L2_TOTAL_TIMEOUT_MS ceiling reached         */

/* 3-letter day-of-week names, indexed by the RTC's DOW register (0=SUN
 * .. 6=SAT), used by display_live_clock() below. */
static const char * const dow_names[7] = { "SUN","MON","TUE","WED","THU","FRI","SAT" };

/* Number of consecutive failed authentication attempts (a wrong or
 * malformed Level-1 Bluetooth password OR a wrong Level-2 keypad password
 * each count as one). Reset to 0 on any fully successful open, or after a
 * lockout period completes. See system_lockout() and
 * register_failed_attempt() below.
 *
 * The threshold and duration come from MAX_WRONG_ATTEMPTS /
 * LOCK_DURATION_MS in defines.h, which are now the single source of truth
 * for the lockout. (security.c previously carried a second, complete but
 * entirely unused copy of this logic - two mechanisms where only one was
 * ever called. The dead copy has been removed.) */
static u8 fail_count = 0;

/* Set to 1 the first time a full Level-1 + Level-2 + motor open/close
 * cycle completes successfully. Until then, the idle screen shows the
 * plain "WAIT BT PWD" prompt; only after that first successful access
 * does the idle screen ever switch over to the RTC clock display (see
 * DisplayStandby() and the main loop below). */
static u8 first_access_done = 0;

/* Post-unlock clock window: after a successful unlock the idle screen shows
 * the live real-time clock for POST_UNLOCK_RTC_DISPLAY_MS and then reverts to
 * the "press password" prompt. post_unlock_clock_active is set to 1 when
 * open_locker_sequence() returns, and post_unlock_clock_start records the
 * millis() instant the window opened; idle_clock_active() clears the flag
 * once the window has run its course. idle_clock_on_screen tracks what the
 * LCD is currently showing so the idle loop knows when it must repaint the
 * prompt (the moment the clock window ends mid-wait) rather than leaving a
 * frozen clock up. */
static u8  post_unlock_clock_active = 0;
static u32 post_unlock_clock_start  = 0;
static u8  idle_clock_on_screen     = 0;

/* Set the first time any data at all arrives from the HC-05, so that fact can
 * be logged exactly once.
 *
 * This is the positive counterpart to the Bluetooth POST. With the module's
 * KEY/EN pin unconnected the POST can only prove that the UART1 LINK is
 * configured - it cannot prove a module is attached, because an unconnected
 * RXD1 pin idles HIGH exactly like a connected idle module's transmit line
 * (see bluetooth.c). Actual traffic arriving IS proof, so the audit log records
 * it the moment it happens. */
static u8 bt_module_confirmed = 0;

/* Required "feed" sequence (0xAA then 0x55 written to PLL0FEED) that must
 * follow any write to the PLL control/configuration registers for the
 * change to actually take effect; this is a fixed hardware requirement
 * of the LPC2148's PLL, not project-specific logic. */
static void pll_feed(void)
{
    PLL0FEED = 0xAA;
    PLL0FEED = 0x55;
}

/* One-time hardware bring-up: configure the PLL for a 60 MHz core clock
 * from a 12 MHz crystal, set up the Memory Accelerator Module (MAM) for
 * that clock speed, then initialise every peripheral driver used by the
 * project (LCD, both UARTs, I2C/EEPROM, RTC, keypad, buzzer, motor,
 * tamper input, and the admin-button interrupt). */
static void SystemInit_SecureLocker(void)
{
    /* PLL setup:
       Crystal = 12MHz
       CCLK    = 60MHz
       PCLK    = 15MHz */
    MAMCR = 0x00;              /* Disable the MAM while changing clock speed */

    PLL0CON = 0x00;            /* Disable the PLL before reconfiguring it     */
    pll_feed();

    PLL0CFG = 0x24;            /* M=5, P=2 -> CCLK = 12MHz * (M+1) = 60MHz    */
    pll_feed();

    PLL0CON = 0x01;            /* Enable the PLL (not yet connected)          */
    pll_feed();

    /* The ONLY deliberately unbounded wait left in the firmware.
     *
     * Every other spin in the project now has a time or iteration limit (the
     * keypad, the I2C transfers, the UART transmit waits, the Bluetooth settle
     * window, the UART1 receive ISR). This one is left alone on purpose: it
     * waits for the PLL to lock, and if the 12 MHz crystal is dead or absent
     * the PLL never will. Giving up and carrying on would leave the CPU running
     * at the wrong frequency, which silently invalidates EVERY derived constant
     * in the system - the UART baud divisors, the Timer0/Timer1 prescalers, the
     * RTC prescaler, the I2C clock. A locker whose timeouts and baud rates are
     * all quietly wrong is far more dangerous than one that visibly fails to
     * start, so stopping here is the correct behaviour. */
    while (!(PLL0STAT & (1UL << 10)));   /* Wait for the PLL to lock */

    PLL0CON = 0x03;            /* Enable AND connect the PLL as the clock source */
    pll_feed();

    VPBDIV = 0x00;             /* PCLK = CCLK / 4 = 15MHz (VPB divider = 1/4) */

    MAMTIM = 0x04;             /* MAM fetch cycles tuned for 60MHz operation  */
    MAMCR  = 0x02;             /* Re-enable the MAM in fully-enabled mode      */

    /* Start the free-running millisecond time base FIRST: every timeout in the
     * project (and the boot self-test itself) measures elapsed time against it,
     * and its Timer1 prescaler assumes the PCLK the lines above have just
     * settled. */
    timebase_init();

    /* Bring up every peripheral driver used by the system. */
    lcd_init();
    uart0_init(9600);
    bluetooth_init(BT_DATA_BAUD);
    i2c_init();
    rtc_init();
    keypad_init();
    buzzer_init();
    motor_init();
    security_init();
    admin_int_init();
}

/*============================================================
 * boot_self_test
 * Power-on self-test (POST): confirm that the two external modules the
 * locker cannot work without are actually present and responding, and say
 * so plainly on the LCD and the UART0 log if they are not.
 *
 * Previously SystemInit_SecureLocker() called i2c_init() and
 * bluetooth_init() and simply assumed both devices were there. Those
 * functions only configure the LPC2148's own pins and peripherals - they
 * cannot tell whether anything is wired to the other end. With no EEPROM
 * connected the symptom was especially misleading: every read returned
 * garbage, the "LKR1" magic marker never matched, so the firmware wrote
 * "default passwords" into a device that was not there, and then denied
 * every password forever with no explanation.
 *
 * Called from main() AFTER the RTC has been set (so log timestamps are
 * sane) and BEFORE ensure_default_passwords() (which must not run against
 * an EEPROM that is not verified working).
 *
 * Handling of a failure differs between the two devices, deliberately:
 *
 *   I2C EEPROM - HARD requirement. Both passwords live there, so there is
 *     nothing useful the locker can do without it. The POST parks on the
 *     "NOT CONFIGURED" screen and re-tests every POST_RETRY_MS, so simply
 *     reseating the module recovers without a power cycle.
 *
 *   Bluetooth - reported in three states, because only part of it can be
 *     tested honestly (see the BOOT SELF-TEST block in bluetooth.c):
 *
 *       BT_POST_UART_FAIL   the UART1 internal loopback failed. Nothing to do
 *                           with the HC-05: the MCU-side interface itself is
 *                           not configured. Reported as a fault.
 *       BT_POST_MODULE_FAIL only possible when the optional HC-05 KEY/EN wire
 *                           is fitted (BT_KEY_CTRL_ENABLED): the module was
 *                           put into command mode and stayed silent, so it
 *                           really is missing or dead. Reported as a fault.
 *       BT_POST_LINK_OK     the link is proven and the module simply cannot be
 *                           interrogated in this wiring. A PASS. The boot
 *                           continues without pausing.
 *       BT_POST_MODULE_OK   the module answered. A PASS.
 *
 *     THIS IS THE FIX for "the Bluetooth is wired correctly and it still says
 *     BT is not configured". The old POST sent "AT" and treated silence as a
 *     missing module - but an HC-05 with KEY unconnected is always in data
 *     mode, where "AT" is not a command and is simply transmitted over the air.
 *     Silence was therefore the NORMAL response from healthy hardware, and the
 *     locker accused itself of a fault at every boot. Both fault states above
 *     are still reported the way the faculty asked for, but neither of them can
 *     be reached by a correctly wired module sitting in data mode.
 *
 *     A genuine fault keeps the failure screen up and re-tests every
 *     POST_RETRY_MS, and any keypad press continues past it.
 *
 * The tamper switch is polled inside both retry loops (tamper_poll(), which
 * logs and reports without touching the LCD), so the enclosure is still being
 * watched while the locker sits on a hardware-fault screen. That used to be a
 * blind spot: the only tamper function available seized the LCD, so the two
 * screens would have fought over the display.
 *============================================================*/
static void boot_self_test(void)
{
    u8 bt_result;

    lcd_clear();
    lcd_string("SELF TEST...");
    uart0_string("POST: starting power-on self-test\r\n");
    delay_ms(500);

    /* ---------- 1. I2C EEPROM (AT24C256) ---------- */
    uart0_string("POST: checking I2C EEPROM (AT24C256) on P0.2/P0.3...\r\n");

    if (!eeprom_selftest())
    {
        log_event("POST FAIL: I2C EEPROM is not configured / not connected");
        buzzer_alert(5);

        while (!eeprom_selftest())
        {
            lcd_clear();
            lcd_string("I2C EEPROM NOT");
            lcd_gotoxy(1, 0);
            lcd_string("CONFIGURED");

            uart0_string("POST: I2C EEPROM NOT CONFIGURED - no response at address 0xA0.\r\n");
            uart0_string("POST: check SCL0=P0.2, SDA0=P0.3, VCC=3V3, GND, WP=GND,\r\n");
            uart0_string("POST: and that both lines have pull-up resistors. Retrying...\r\n");

            /* Keep watching the enclosure while parked here. Logs only - it
             * must not wipe the fault screen the technician is reading. */
            tamper_poll();

            delay_ms(POST_RETRY_MS);
        }

        log_event("POST: I2C EEPROM detected and verified after retry");
    }

    lcd_clear();
    lcd_string("I2C EEPROM OK");
    uart0_string("POST: I2C EEPROM OK (address ACK + read/write verified)\r\n");
    delay_ms(800);

    /* ---------- 2. Bluetooth interface (UART1 link + HC-05) ---------- */
    lcd_clear();
    lcd_string("CHECK BT LINK");
    uart0_string("POST: checking the Bluetooth interface on UART1...\r\n");

    bt_result = bluetooth_selftest();

    if ((bt_result == BT_POST_UART_FAIL) || (bt_result == BT_POST_MODULE_FAIL))
    {
        log_event((bt_result == BT_POST_UART_FAIL)
                  ? "POST FAIL: Bluetooth UART1 link is not configured (internal loopback failed)"
                  : "POST FAIL: HC-05 Bluetooth module is not configured / did not answer in command mode");
        buzzer_alert(5);

        for (;;)
        {
            lcd_clear();

            if (bt_result == BT_POST_UART_FAIL)
            {
                lcd_string("BT UART1 NOT");
                lcd_gotoxy(1, 0);
                lcd_string("CONFIGURED");

                uart0_string("POST: UART1 INTERNAL LOOPBACK FAILED - the Bluetooth interface on\r\n");
                uart0_string("POST: the MCU side is not configured. This does NOT depend on the\r\n");
                uart0_string("POST: HC-05: test bytes never came back inside the UART itself.\r\n");
                uart0_string("POST: check that PCLK is 15 MHz (PLL/VPBDIV), that the 9600 baud\r\n");
                uart0_string("POST: divisor was programmed with DLAB set, and that PINSEL0\r\n");
                uart0_string("POST: selects TXD1 on P0.8 and RXD1 on P0.9.\r\n");
            }
            else
            {
                lcd_string("HC-05 BT NOT");
                lcd_gotoxy(1, 0);
                lcd_string("CONFIGURED");

                uart0_string("POST: HC-05 NOT CONFIGURED - UART1 is fine, but the module did not\r\n");
                uart0_string("POST: answer AT in command mode with KEY/EN driven high.\r\n");
                uart0_string("POST: check VCC=5V, GND, TXD1=P0.8 -> HC-05 RXD,\r\n");
                uart0_string("POST: RXD1=P0.9 <- HC-05 TXD, and the KEY/EN wire.\r\n");
            }

            uart0_string("POST: press any keypad key to continue anyway. Retrying...\r\n");

            tamper_poll();   /* Enclosure stays monitored while parked here */

            /* Wait POST_RETRY_MS for an override key, then re-test. */
            if (keypad_getkey_timeout(POST_RETRY_MS, 0) != 0)
            {
                log_event("POST: Bluetooth check overridden from keypad, continuing");
                break;
            }

            bt_result = bluetooth_selftest();

            if ((bt_result != BT_POST_UART_FAIL) && (bt_result != BT_POST_MODULE_FAIL))
            {
                log_event("POST: Bluetooth interface verified after retry");
                break;
            }
        }
    }

    /* ---------- Report the final Bluetooth state ---------- */
    lcd_clear();

    if (bt_result == BT_POST_MODULE_OK)
    {
        lcd_string("HC-05 BT OK");
        uart0_string("POST: HC-05 OK - UART1 loopback passed AND the module answered AT.\r\n");
    }
    else if (bt_result == BT_POST_LINK_OK)
    {
        lcd_string("BT LINK OK");
        lcd_gotoxy(1, 0);
        lcd_string("HC-05 IN DATA");
        uart0_string("POST: Bluetooth link OK - UART1 internal loopback passed, so the\r\n");
        uart0_string("POST: interface (pins, baud rate, frame format, RX path) is configured.\r\n");
        uart0_string("POST: The HC-05 did not answer the AT probe, which is EXPECTED here and\r\n");
        uart0_string("POST: is not a fault: with KEY/EN unwired the module is always in data\r\n");
        uart0_string("POST: mode, where it forwards AT over the air instead of answering it.\r\n");
        uart0_string("POST: Module presence is confirmed the moment it delivers a password.\r\n");
        uart0_string("POST: (For a definitive module test, see BT_KEY_CTRL_ENABLED in defines.h.)\r\n");
    }
    else
    {
        lcd_string("BT NOT VERIFIED");
        lcd_gotoxy(1, 0);
        lcd_string("CONTINUING");
        uart0_string("POST: continuing with the Bluetooth interface UNVERIFIED (overridden).\r\n");
    }

    delay_ms(800);
    uart0_string("POST: self-test complete\r\n");
}

/* Print a byte as two zero-padded decimal digits (e.g. 7 -> "07"). */
static void print2(u8 val)
{
    lcd_data((val / 10) + '0');
    lcd_data((val % 10) + '0');
}

/* Refresh the idle screen in place from ONE tear-free RTC snapshot
 * (rtc_get(), see rtc.h) - no lcd_clear() here, so the digits are simply
 * overwritten each call and the display doesn't flicker.
 *
 * Line 1: HH:MM:SS
 * Line 2: DD/MM/YYYY DOW
 *
 * The fields used to be read one register at a time (HOUR, then MIN, then
 * SEC, ...), which can TEAR across a rollover: read HOUR at 12:59:59.999 and
 * the rest a moment later and the screen shows "12:00:00" - an hour wrong,
 * roughly once an hour. rtc_get() latches the whole calendar consistently.
 *
 * This is only ever called from the idle/standby wait loop in main()
 * below - never while a Level-1 or Level-2 password is actually being
 * entered - so it refreshes the clock without ever interrupting or
 * re-issuing a password prompt mid-entry. */
static void display_live_clock(void)
{
    rtc_time now;

    rtc_get(&now);

    lcd_gotoxy(0, 0);
    print2(now.hour); lcd_data(':');
    print2(now.min);  lcd_data(':');
    print2(now.sec);
    lcd_string("        ");   /* Pad out the rest of line 1 (clears any leftovers) */

    lcd_gotoxy(1, 0);
    print2(now.dom);   lcd_data('/');
    print2(now.month); lcd_data('/');
    lcd_data((u8)((now.year / 1000) % 10 + '0'));
    lcd_data((u8)((now.year /  100) % 10 + '0'));
    lcd_data((u8)((now.year /   10) % 10 + '0'));
    lcd_data((u8)( now.year         % 10 + '0'));
    lcd_data(' ');
    lcd_string((now.dow <= 6) ? dow_names[now.dow] : "???");
}

/* Is the live clock the correct idle screen right now?
 *
 * The clock is shown only during the POST_UNLOCK_RTC_DISPLAY_MS window that
 * starts when a successful unlock completes (open_locker_sequence() sets
 * post_unlock_clock_active). When the window expires this clears the flag, so
 * the next repaint falls back to the "press password" prompt. Before the
 * first successful access ever (first_access_done == 0) it never shows the
 * clock, exactly as before. */
static u8 idle_clock_active(void)
{
    if (post_unlock_clock_active &&
        elapsed_since(post_unlock_clock_start) >= POST_UNLOCK_RTC_DISPLAY_MS)
        post_unlock_clock_active = 0;   /* window over -> back to the prompt */

    return (u8)(first_access_done && post_unlock_clock_active);
}

/* Show the idle/waiting screen while the system waits for a Bluetooth
 * password attempt.
 *
 * Before the very first successful Level-1 + Level-2 + motor access:
 *   shows the plain "WAIT BT PWD" prompt (same as the original
 *   behaviour), since there's nothing meaningful to show yet.
 *
 * After the first successful access:
 *   shows the continuously-updating live clock/date/day display for
 *   POST_UNLOCK_RTC_DISPLAY_MS after each unlock, then returns to the
 *   password prompt - so the clock is a post-unlock feature rather than
 *   a permanent idle screen. */
static void DisplayStandby(void)
{
    lcd_clear();

    if (idle_clock_active())
    {
        display_live_clock();
        idle_clock_on_screen = 1;
    }
    else
    {
        /* Prompt only - deliberately NOT the password itself. This line
         * used to read "SEND 1234#", which printed the Level-1 credential
         * in plain sight on the front panel and, worse, went stale and
         * actively misleading the moment an admin changed it via the
         * menu. */
        lcd_string("WAIT BT PWD");
        lcd_gotoxy(1,0);
        lcd_string("SEND PWD THEN #");
        idle_clock_on_screen = 0;
    }
}

/* Show the "access granted" message on the LCD. */
static void DisplayAccessGranted(void)
{
    lcd_clear();
    lcd_string("ACCESS GRANTED");
}

/* Show the "access denied" message on the LCD, log the specific reason
 * for the audit trail, and sound the buzzer as feedback. */
static void DisplayAccessDenied(const char *reason)
{
    lcd_clear();
    lcd_string("ACCESS DENIED");
    log_event(reason);
    buzzer_alert(5);
}

/*============================================================
 * LEVEL-2 KEYPAD ENTRY
 *
 * Screen layout (16x2), with both countdowns visible while the user types:
 *
 *        col: 0123456789012345
 *      row 0: KEYPAD PWD:  60s     <- seconds until the entry times out
 *      row 1: **      TOT:175s     <- masked digits, and the total time left
 *
 * Row 0 shows the countdown that actually matters: the number of seconds until
 * the entry will be abandoned, i.e. the SMALLER of the two limits below. Row 1
 * shows how much of the 3-minute ceiling is left, so it is obvious that the
 * per-character timer restarting does not buy unlimited time.
 *
 * TWO LIMITS RUN AT THE SAME TIME (see defines.h):
 *
 *   L2_INTERKEY_TIMEOUT_MS (1 min) - how long the locker waits for the NEXT
 *       character. It RESTARTS FROM A FULL MINUTE on every keypress: for the
 *       password "5678", pressing '5' gives a fresh minute to press '6',
 *       pressing '6' gives a fresh minute to press '7', and so on. ANY key
 *       restarts it - digits, the '*' backspace, the '#' clear, even the unused
 *       A-D keys - because its purpose is to detect that the user has WALKED
 *       AWAY, and any keypress at all proves somebody is still standing there.
 *
 *   L2_TOTAL_TIMEOUT_MS (3 min) - a hard ceiling on the WHOLE entry, measured
 *       from the moment the prompt appears and never extended by anything. This
 *       is what stops the restarting per-character timer from being abused to
 *       hold an authenticated session open indefinitely by tapping one key
 *       every 59 seconds.
 *
 * Whichever expires first ends the entry, and the two cases are reported
 * separately (L2_ENTRY_IDLE_OUT vs L2_ENTRY_TOTAL_OUT) so the audit log records
 * which one it was.
 *
 * WHY THIS EXISTS AT ALL: this function used to call the unconditionally
 * blocking keypad_getkey(), so it waited for the Level-2 password forever. That
 * left the locker parked with Level-1 already satisfied for an unbounded time -
 * the captured audit log in 1.TXT shows a real 15-minute-55-second gap between
 * "Level-1 Bluetooth password matched" and "Level-2 keypad password matched".
 * Anyone walking past during that window only had to defeat the keypad, which
 * reduces two-factor entry to one factor.
 *
 * Line editing is unchanged:
 *   0-9  -> append the digit (masked with '*' on the LCD)
 *   '*'  -> backspace: erase the previous digit from the buffer and the LCD
 *   '#'  -> clear the entire entry and start again
 *   A-D  -> ignored, and they do not consume a digit position
 *
 * Returns L2_ENTRY_OK, L2_ENTRY_IDLE_OUT or L2_ENTRY_TOTAL_OUT.
 *============================================================*/

/* Draw the fixed parts of the Level-2 screen and leave the cursor where the
 * next masked digit belongs. Also used to restore the screen after a tamper
 * alert has temporarily taken it over. */
static void l2_draw_prompt(u8 digits_entered)
{
    u8 n;

    lcd_clear();
    lcd_string("KEYPAD PWD:");                 /* Columns 0-10 */

    lcd_gotoxy(0, LCD_COUNTDOWN_UNIT);
    lcd_data('s');                              /* Fixed unit for the row-0 countdown */

    lcd_gotoxy(1, 8);
    lcd_string("TOT:");                         /* Columns 8-11 */
    lcd_gotoxy(1, LCD_COUNTDOWN_UNIT);
    lcd_data('s');

    /* Re-show the digits already typed, still masked. */
    lcd_gotoxy(1, 0);
    for (n = 0; n < digits_entered; n++)
        lcd_data('*');
}

/* Redraw both countdowns (rounded UP to whole seconds, so the display only
 * reaches 0 when the limit really has expired) and put the cursor back where
 * the next masked digit goes. Called only when the displayed second actually
 * changes: each lcd_data() costs about 2 ms, so redrawing continuously would
 * waste a noticeable slice of every second and could miss a quick keypress. */
static void l2_draw_countdown(u32 key_left_ms, u32 total_left_ms, u8 digits_entered)
{
    lcd_gotoxy(0, LCD_COUNTDOWN_COL);
    lcd_uint_pad((key_left_ms + 999UL) / 1000UL, 3);

    lcd_gotoxy(1, LCD_COUNTDOWN_COL);
    lcd_uint_pad((total_left_ms + 999UL) / 1000UL, 3);

    lcd_gotoxy(1, digits_entered);
}

static u8 read_keypad_password(char *buf)
{
    u32 entry_start = millis();       /* Reference for the 3-minute ceiling      */
    u32 key_start   = millis();       /* Reference for the 1-minute idle limit   */
    u32 shown       = 0xFFFFFFFFUL;   /* Countdown value last drawn (forces one) */
    u32 total_left;
    u32 key_left;
    u32 shorter_left;
    u32 secs;
    u8  i = 0;
    u8  k;

    /* Whether the 1-minute idle limit is already in force. See
     * L2_IDLE_TIMER_ARMED_AT_START in defines.h: with the default of 1 it
     * applies from the moment the prompt appears; set it to 0 and the user gets
     * the full 3 minutes to press the FIRST character, after which the
     * per-character limit takes over. Either way the first keypress arms it. */
    u8 idle_armed = (u8)(L2_IDLE_TIMER_ARMED_AT_START);

    l2_draw_prompt(0);

    while (i < PWD_LEN)
    {
        /* --- how much of each budget is left, right now --------------------
         * Both are derived from the hardware millisecond counter, so time
         * spent scanning the matrix, redrawing the LCD or sounding the buzzer
         * for a tamper alert is all counted honestly. */
        total_left = elapsed_since(entry_start);
        total_left = (total_left >= L2_TOTAL_TIMEOUT_MS)
                     ? 0UL : (L2_TOTAL_TIMEOUT_MS - total_left);

        if (idle_armed)
        {
            key_left = elapsed_since(key_start);
            key_left = (key_left >= L2_INTERKEY_TIMEOUT_MS)
                       ? 0UL : (L2_INTERKEY_TIMEOUT_MS - key_left);
        }
        else
        {
            key_left = total_left;   /* Not armed yet: only the ceiling applies */
        }

        /* The user is shown the limit that will actually bite first. */
        shorter_left = (key_left < total_left) ? key_left : total_left;
        secs         = (shorter_left + 999UL) / 1000UL;

        if (secs != shown)
        {
            shown = secs;
            l2_draw_countdown(shorter_left, total_left, i);
        }

        /* Checked AFTER the redraw above, so the countdown is seen reaching 0.
         * The ceiling is tested first: if both ran out in the same instant, the
         * 3-minute limit is the more accurate description of what happened. */
        if (total_left == 0UL)
            return L2_ENTRY_TOTAL_OUT;

        if (idle_armed && (key_left == 0UL))
            return L2_ENTRY_IDLE_OUT;

        /* --- tamper detection stays live during the whole entry -------------
         * This window can last three minutes with Level-1 ALREADY satisfied,
         * which makes it the worst possible time to stop watching the
         * enclosure - yet nothing here used to poll it. tamper_poll() logs the
         * event without touching the LCD, so the alert is shown briefly and the
         * password screen is then restored exactly as it was, digits included. */
        if (tamper_poll())
        {
            lcd_clear();
            lcd_string("TAMPER ALERT");
            lcd_gotoxy(1, 0);
            lcd_string("L2 ENTRY OPEN");
            buzzer_alert(5);

            l2_draw_prompt(i);          /* Restore the prompt and the masks */
            shown = 0xFFFFFFFFUL;       /* Force the countdowns to redraw    */
            continue;
        }

        /* Poll the matrix directly instead of handing the whole remaining
         * budget to keypad_getkey_timeout(): that call would not return until a
         * key arrived or the entire minute had passed, which would freeze the
         * on-screen countdown. keypad_scan() still does its own debounce and
         * bounded wait-for-release. */
        k = keypad_scan();

        if (k == 0)
        {
            delay_ms(KEY_POLL_MS);
            continue;                   /* Nothing yet - refresh and keep waiting */
        }

        /* --- A KEY WAS PRESSED: restart the per-character timer -------------
         * Done for EVERY key, before working out what the key means, including
         * the editing keys and the unused A-D. Somebody is clearly still at the
         * keypad, which is exactly what this limit is testing for. The
         * 3-minute ceiling above is untouched, so this cannot extend the
         * session beyond it. */
        idle_armed = 1U;                /* First keypress always arms the limit  */
        key_start  = millis();
        shown      = 0xFFFFFFFFUL;      /* Snap the countdown back to 60 at once */

        if (k >= '0' && k <= '9')
        {
          buf[i++] = (char)k;
          lcd_data('*');
        }
        else if (k == '*')          /* Backspace: remove the last entered digit */
        {
          if(i>0)
          {
            i--;
            buf[i] ='\0';

            lcd_gotoxy(1, i);       /* Move cursor back to the erased position */
            lcd_data(' ');           /* Blank out the '*' that was shown there   */
            lcd_gotoxy(1, i);        /* Leave the cursor ready for the next digit */
          }
        }
        else if (k == '#')          /* Clear: erase the whole entry so far */
        {
          while(i>0)
          {
             i--;
             buf[i] = '\0';

             lcd_gotoxy(1, i);
             lcd_data(' ');
          }
          lcd_gotoxy(1, 0);
        }
        /* A-D: ignored, and they do not consume a digit position. */
    }

    buf[PWD_LEN] = '\0';
    return L2_ENTRY_OK;
}

/* Runs the physical locker actuation sequence once both passwords have
 * matched: show "access granted", turn the motor forward ONCE for a
 * single ~180-degree turn to open the locker (gate-style), hold it open,
 * then turn the motor in reverse ONCE for the same duration to bring it
 * back to its original (closed) position. Every stage is written to the
 * audit log.
 *
 * A "busy" guard prevents this function from ever being re-entered or
 * overlapped (e.g. if it were accidentally called again while already
 * running) - only one forward pulse and one reverse pulse can ever be in
 * flight, which is what stops the motor from appearing to run forward
 * and reverse "at the same time". */
static void open_locker_sequence(void)
{
    static u8 locker_busy = 0;

    if (locker_busy)
        return;         /* Already mid-sequence - ignore a duplicate call */
    locker_busy = 1;

    DisplayAccessGranted();
    log_event("Access granted, opening locker");

    /* --- Single forward pulse: turn the gate/latch open ~180 degrees --- */
    motor_stop();                 /* Make sure we start from a full stop   */
    motor_forward();
    delay_ms(MOTOR_ROTATE_MS);    /* One short, tuned pulse - NOT a multi-second
                                    * continuous run, so the motor turns once
                                    * instead of spinning through several
                                    * full rotations. */
    motor_stop();

    log_event("Locker opened");

    lcd_clear();
    lcd_string("LOCKER OPEN");
    lcd_gotoxy(1,0);
    lcd_string("WAIT...");
    delay_ms(LOCKER_OPEN_HOLD_MS);   /* Hold the gate open for the visitor    */

    lcd_clear();
    lcd_string("LOCKER CLOSE");

    /* --- Brief full-stop settle time before reversing direction --------
     * Reversing an H-bridge output straight from one direction to the
     * other while the motor still has momentum can cause a mechanical/
     * electrical jolt; a short stop first avoids that. */
    delay_ms(MOTOR_SETTLE_MS);

    /* --- Single reverse pulse: return the gate/latch to its original
     * (closed) position, using the exact same duration as the opening
     * pulse so it ends up back where it started. --- */
    motor_reverse();
    delay_ms(MOTOR_ROTATE_MS);
    motor_stop();

    log_event("Locker closed");

    first_access_done = 1;   /* From now on, the idle screen shows the live clock */
    locker_busy = 0;
}

/*============================================================
 * system_lockout
 * Called once fail_count reaches MAX_WRONG_ATTEMPTS (3 consecutive
 * wrong Level-1 or Level-2 password attempts). Discards any
 * Bluetooth command that may already be sitting in the receive
 * buffer (so a password sent during the lockout can't sneak
 * through the instant it ends), shows "SYSTEM LOCKED" with a live
 * seconds countdown, sounds the buzzer, and then blocks for
 * LOCK_DURATION_MS while still watching the tamper switch. Once the
 * lockout elapses, the failed-attempt counter is reset and normal
 * operation resumes.
 *
 * The lockout length is measured against the millisecond time base, so it
 * lasts exactly LOCK_DURATION_MS regardless of how much time a tamper alert or
 * the countdown redraws consume. It used to be a loop of 30 one-second delays,
 * which a single 3-second tamper buzz silently stretched to 33 seconds.
 *============================================================*/
static void system_lockout(void)
{
    u32 start;
    u32 left;
    u32 secs;
    u32 shown = 0xFFFFFFFFUL;

    log_event("System locked: 3 consecutive failed attempts");

    bluetooth_clear();     /* Discard anything already buffered/in-flight */

    lcd_clear();
    lcd_string("SYSTEM LOCKED");
    lcd_gotoxy(1,0);
    lcd_string("WAIT");
    lcd_gotoxy(1, LCD_COUNTDOWN_UNIT);
    lcd_data('s');
    buzzer_alert(3);

    start = millis();

    for (;;)
    {
        left = elapsed_since(start);
        left = (left >= LOCK_DURATION_MS) ? 0UL : (LOCK_DURATION_MS - left);
        secs = (left + 999UL) / 1000UL;

        if (secs != shown)
        {
            shown = secs;
            lcd_gotoxy(1, LCD_COUNTDOWN_COL);
            lcd_uint_pad(secs, 3);
        }

        if (left == 0UL)
            break;

        /* Tamper detection stays active during the lockout. Reported with
         * tamper_poll() plus a manual redraw rather than
         * check_tamper_and_alert(), so the lockout screen and its countdown
         * come back afterwards instead of being left wiped. */
        if (tamper_poll())
        {
            lcd_clear();
            lcd_string("TAMPER ALERT");
            buzzer_alert(5);

            lcd_clear();
            lcd_string("SYSTEM LOCKED");
            lcd_gotoxy(1,0);
            lcd_string("WAIT");
            lcd_gotoxy(1, LCD_COUNTDOWN_UNIT);
            lcd_data('s');
            shown = 0xFFFFFFFFUL;
        }

        delay_ms(100);
    }

    fail_count = 0;

    /* Drop anything that arrived DURING the lockout as well, so a password
     * sent while the system was locked cannot be acted on the instant the
     * lockout ends. */
    bluetooth_clear();

    log_event("System unlocked after the lockout period");

    lcd_clear();
    lcd_string("SYSTEM UNLOCKED");
    delay_ms(1000);
}

/*============================================================
 * register_failed_attempt
 * Single place where a failed authentication attempt is counted and the
 * lockout threshold is enforced, so the "increment then test" pair can
 * never drift out of step between the several call sites that need it.
 *============================================================*/
static void register_failed_attempt(void)
{
    fail_count++;

    if (fail_count >= MAX_WRONG_ATTEMPTS)
        system_lockout();
}

int main(void)
{
    char bt_cmd[BT_BUF_SIZE];
    char l1_pwd[PWD_LEN + 1];
    char l2_pwd[PWD_LEN + 1];
    char kp_pwd[PWD_LEN + 1];
    u8   bt_status;
    u8   l2_status;

    SystemInit_SecureLocker();

    /* RTC boot policy now lives inside rtc_init(): if a battery-backed
     * external RTC (DS1307/DS3231) is fitted on the I2C bus, its real time is
     * loaded into the on-chip RTC so the clock survives a power-off; otherwise
     * the on-chip RTC is given the 12:00:00 / 01-01-2024 default ONLY when it
     * is completely unset, instead of being blindly reset on every boot -
     * that unconditional reset is what used to make the clock always come back
     * at 12:00 PM. See rtc.c. */
    lcd_clear();
    lcd_string("Bluetooth");
    lcd_gotoxy(1,0);
    lcd_string("Secure System");

    uart0_string("\r\n====================================\r\n");
    uart0_string(" Bluetooth Secure Locker Started\r\n");
    uart0_string("====================================\r\n");

    delay_ms(1500);

    /* Verify the external modules BEFORE trusting them. This must run
     * before ensure_default_passwords(), which would otherwise happily
     * "provision" a missing EEPROM and leave the locker permanently
     * unopenable. Blocks (with retries) if the EEPROM is absent. */
    boot_self_test();

    /* On the very first boot (or if the EEPROM was blank/corrupted),
     * populate it with the factory-default passwords. Safe to do now that
     * the EEPROM is confirmed present and verified read/write-capable. */
    ensure_default_passwords();

    log_event("System booted");

    /* Report honestly what kind of clock the system is running on: with a
     * battery-backed external RTC fitted, time survives a full power-off;
     * without one, the LPC2148's on-chip RTC is PCLK-derived with no VBAT
     * domain and freezes while the board is unpowered. */
    if (rtc_battery_backed())
        log_event("RTC: battery-backed external RTC found - real time restored");
    else
        log_event("RTC: no external RTC - on-chip clock cannot keep time across a power-off");

    delay_ms(1000);

    while (1)
    {
        /* Highest priority: if the admin button was pressed, service
         * the admin menu before doing anything else. */
        if (admin_flag)
            admin_menu();

        check_tamper_and_alert();
        check_alarm();   /* From the merged EnviroTime menu module: ring the buzzer if the alarm time is reached */

        /* While the alarm is ringing, leave its "** ALARM!! **" banner on the
         * display instead of redrawing the standby screen over the top of it.
         * check_alarm() drew that banner and the very next statement used to
         * erase it, so the buzzer sounded with no indication on screen of what
         * was happening or how to stop it. The banner now stays until the admin
         * silences the alarm from the Alarm sub-menu (alarm_triggered, declared
         * in menu.h, is cleared there). */
        if (!alarm_triggered)
            DisplayStandby();

        /* Wait for a Bluetooth command to arrive, while continuously
         * refreshing the live clock (so it keeps ticking, not just a
         * single frozen snapshot), still polling the tamper switch, and
         * bailing out early if the admin button is pressed during the
         * wait. */
        while (!bluetooth_available())
        {
            check_tamper_and_alert();
            check_alarm();

            if (first_access_done && !alarm_triggered)
            {
                if (idle_clock_active())
                {
                    display_live_clock();   /* Refresh HH:MM:SS / date / day in place, no re-clear */
                    idle_clock_on_screen = 1;
                }
                else if (idle_clock_on_screen)
                {
                    /* The post-unlock clock window just ended while we were
                     * still waiting for Bluetooth: swap the frozen clock for
                     * the password prompt instead of leaving it stuck up. */
                    DisplayStandby();
                }
            }

            if (admin_flag)
                break;

            delay_ms(100);
        }

        if (admin_flag)
             continue;   /* Go back to the top of the loop and open the admin menu */

        /* Let the incoming burst finish before judging it. Without this the
         * trailing-junk check below would depend on the main loop's 100 ms
         * poll happening to land after the whole line arrived. */
        bluetooth_settle();

        /* Traffic from the module is the proof of presence that the boot POST
         * could not obtain by itself. Logged once, the first time it happens. */
        if (!bt_module_confirmed)
        {
            bt_module_confirmed = 1;
            log_event("HC-05 link confirmed: data received from the Bluetooth module");
        }

        bt_status = bluetooth_read_command(bt_cmd);

        /* ---- Reject malformed transmissions before any comparison ----
         * Each of these is a wrong password, not a retry: the user did not
         * send exactly PWD_LEN characters followed by '#'. */
        if (bt_status & BT_RX_OVERFLOW)
        {
            log_event("Bluetooth authentication request received");
            DisplayAccessDenied("Level-1 DENIED: exceeded the RX buffer size from BT level");
            bluetooth_clear();
            register_failed_attempt();
            delay_ms(1000);
            continue;
        }

        if (bt_status & BT_RX_TRAILING)
        {
            /* e.g. "1234#123456789" - the correct password followed by more
             * characters. The old receiver silently dropped everything after
             * the '#', so this was granted access. */
            log_event("Bluetooth authentication request received");
            DisplayAccessDenied("Level-1 DENIED: extra characters after the # terminator");
            bluetooth_clear();
            register_failed_attempt();
            delay_ms(1000);
            continue;
        }

        if (bt_status & BT_RX_EMPTY)
            continue;   /* A bare '#' with no digits - not an attempt at all */

        log_event("Bluetooth authentication request received");

        /* --- Level 1: check the Bluetooth password ---
         * The EEPROM read is bracketed by a fault check. If the I2C bus dies
         * while the locker is running (a wire works loose, the module is pulled
         * out) every read returns garbage, no password can ever match, and the
         * locker would just deny everyone with no explanation - the exact
         * failure the boot self-test was added to prevent, only happening later.
         * A bus fault is reported as a hardware fault and is deliberately NOT
         * counted as a failed attempt: the user did nothing wrong. */
        eeprom_clear_fault();
        eeprom_read_str(EEPROM_L1_ADDR, l1_pwd, PWD_LEN);

        if (eeprom_bus_fault())
        {
            log_event("HARDWARE FAULT: I2C EEPROM did not respond while reading the Level-1 password");

            lcd_clear();
            lcd_string("EEPROM FAULT");
            lcd_gotoxy(1,0);
            lcd_string("CHECK WIRING");
            buzzer_alert(3);

            bluetooth_clear();
            delay_ms(2000);
            continue;
        }

        /* NOTE: a debug block used to print the received password AND the
         * stored EEPROM password here in plain text (the captured log in
         * 1.TXT contains lines like "DEBUG: received='1234' stored
         * L1='1234'"). That leaked both credentials to anyone watching the
         * audit console, so it has been removed. The BT_RX_* status codes
         * above and the specific denial reasons logged below give the same
         * diagnostic value without ever printing a password. */

        /* Exact, full-length, character-by-character comparison - see
         * password_match() in security.c. */
        if (password_match(bt_cmd, l1_pwd, PWD_LEN))
        {
            lcd_clear();
            lcd_string("LEVEL1 OK");
            lcd_gotoxy(1,0);
            lcd_string("ENTER L2");

            log_event("Level-1 Bluetooth password matched");
            delay_ms(1000);

            /* --- Level 2: prompt for and check the keypad password ---
             * read_keypad_password() draws its own prompt and both live
             * countdowns, and enforces the two limits described at its
             * definition: L2_INTERKEY_TIMEOUT_MS (1 min for the NEXT character,
             * restarted by every keypress) and L2_TOTAL_TIMEOUT_MS (3 min hard
             * ceiling on the whole entry). Whichever runs out first abandons the
             * entry and drops back to the Level-1 locked state, so the Bluetooth
             * password has to be sent again before the keypad is offered a
             * second time. */
            l2_status = read_keypad_password(kp_pwd);

            if (l2_status != L2_ENTRY_OK)
            {
                if (l2_status == L2_ENTRY_IDLE_OUT)
                {
                    log_event("Level-2 keypad entry abandoned: no key pressed for 1 min, returning to Level-1 lock");
                    lcd_clear();
                    lcd_string("NO KEY 60 SEC");
                }
                else
                {
                    log_event("Level-2 keypad entry abandoned: 3 min total limit reached, returning to Level-1 lock");
                    lcd_clear();
                    lcd_string("L2 TIMEOUT 3MIN");
                }

                lcd_gotoxy(1,0);
                lcd_string("SEND BT PWD");
                buzzer_alert(2);

                /* Drop any Bluetooth traffic that arrived while the keypad
                 * prompt was open, so the retry starts from a clean slate
                 * and stale input cannot auto-satisfy Level-1. */
                bluetooth_clear();

                /* Deliberately NOT counted as a failed attempt: nothing was
                 * guessed, the user simply walked away. See defines.h. */
                delay_ms(2000);
                continue;
            }

            /* Same runtime bus-fault guard as the Level-1 read above. */
            eeprom_clear_fault();
            eeprom_read_str(EEPROM_L2_ADDR, l2_pwd, PWD_LEN);

            if (eeprom_bus_fault())
            {
                log_event("HARDWARE FAULT: I2C EEPROM did not respond while reading the Level-2 password");

                lcd_clear();
                lcd_string("EEPROM FAULT");
                lcd_gotoxy(1,0);
                lcd_string("CHECK WIRING");
                buzzer_alert(3);

                bluetooth_clear();
                delay_ms(2000);
                continue;
            }

            if (password_match(kp_pwd, l2_pwd, PWD_LEN))
            {
                log_event("Level-2 keypad password matched");
                fail_count = 0;         /* Full success - clear the failed-attempt counter */
                open_locker_sequence();

                /* The idle screen now shows the live real-time clock for
                 * POST_UNLOCK_RTC_DISPLAY_MS, then returns to the password
                 * prompt (idle_clock_active()/DisplayStandby() read this
                 * window). */
                post_unlock_clock_active = 1;
                post_unlock_clock_start  = millis();
            }
            else
            {
                DisplayAccessDenied("Wrong Level-2 keypad password");
                register_failed_attempt();
            }

            /* Whatever the outcome, discard any Bluetooth traffic that arrived
             * while the keypad prompt or the motor sequence was running. Each
             * attempt must start from a freshly sent Level-1 password, never
             * from something left queued in the buffer minutes earlier. */
            bluetooth_clear();
        }
        else
        {
            DisplayAccessDenied("Wrong Level-1 Bluetooth password");
            register_failed_attempt();
        }

        delay_ms(1000);
    }
}
