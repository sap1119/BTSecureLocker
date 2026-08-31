/*=============================================================================
 * File        : defines.h
 * Project     : Bluetooth-Based Secure Locker with Access Logging
 * Description : Global project-wide constants shared by several modules
 *               (EEPROM memory map, password length, Bluetooth buffer size,
 *               and the admin push-button interrupt configuration).
 *===========================================================================*/
#ifndef DEFINES_H
#define DEFINES_H

#include <lpc214x.h>   /* LPC2148 peripheral register map (SFRs)          */
#include "types.h"      /* u8 / u16 / u32 / s8 / s16 / s32 aliases         */

/* Number of digits in every password (Bluetooth Level-1 and keypad Level-2) */
#define PWD_LEN            4

/* --- EEPROM (AT24C256) address map -----------------------------------------
 * A small "magic" marker is stored first; if it does not match the expected
 * value the EEPROM is considered blank/uninitialised and is (re)loaded with
 * the two factory-default passwords the first time the system boots.
 * ------------------------------------------------------------------------ */
#define EEPROM_MAGIC_ADDR  0x0000   /* 4-byte marker "LKR1"                */
#define EEPROM_L1_ADDR     0x0010   /* Level-1 (Bluetooth) password, 4 digits */
#define EEPROM_L2_ADDR     0x0020   /* Level-2 (keypad) password, 4 digits */
#define EEPROM_SCRATCH_ADDR 0x0040  /* Spare byte used only by the boot self-test */

/* --- Bluetooth (HC-05) receive buffer -------------------------------------
 * BT_BUF_SIZE is the total size of the UART1 receive buffer, so the longest
 * password payload that can ever be accepted is BT_BUF_SIZE - 1 characters
 * (one byte is reserved for the '\0' string terminator). Anything longer is
 * rejected outright as an over-length attempt rather than being silently
 * truncated - see BT_RX_OVERFLOW in bluetooth.h.
 * ------------------------------------------------------------------------ */
#define BT_BUF_SIZE        32
#define BT_MAX_PAYLOAD     (BT_BUF_SIZE - 1)   /* Longest acceptable payload: 31 chars */

/* Maximum bytes UART1_ISR will pull from the receive FIFO in one interrupt.
 * The FIFO is 16 bytes deep, so this is generous; its only job is to make sure
 * the handler's drain loop can never become an infinite loop INSIDE an
 * interrupt (which would take the whole locker down with no diagnostic) if the
 * Receiver-Data-Ready flag ever failed to clear. Anything still queued just
 * triggers the interrupt again. */
#define BT_ISR_MAX_BYTES   32U

/* --- Bluetooth burst "settle" timing --------------------------------------
 * A phone app sends a whole line (e.g. "1234#1234") as one back-to-back
 * burst; at 9600 baud each character takes only ~1 ms, while the main loop
 * polls every 100 ms. After the '#' terminator arrives, the receiver waits
 * for the line to fall quiet for BT_SETTLE_MS before the command is accepted,
 * which guarantees that any extra characters belonging to the SAME burst are
 * seen and counted as trailing junk instead of being silently discarded.
 * Checked in BT_SETTLE_STEP_MS steps; the quiet window restarts whenever
 * another byte arrives.
 * ------------------------------------------------------------------------ */
#define BT_SETTLE_MS       40U   /* Line must be idle this long before accepting */
#define BT_SETTLE_STEP_MS  5U    /* Granularity of the quiet-window check         */
#define BT_SETTLE_MAX_MS   2000U /* Hard cap on the total settle wait (see below) */

/* --- Boot self-test (POST) timing ----------------------------------------
 * POST_RETRY_MS is how long a failure screen is shown before the check is
 * retried.
 *
 * The HC-05 stage of the POST is layered, because of a hard constraint on the
 * module itself (see bluetooth_selftest() for the full explanation):
 *
 *   1. UART1 INTERNAL LOOPBACK. The UART1 transmitter is internally wired to
 *      its own receiver (U1MCR loopback mode) and known bytes are pushed
 *      through it. This is deterministic and proves the whole MCU-side chain:
 *      pin selection, baud divisor, frame format, FIFOs and the receive path.
 *      BT_LOOPBACK_BYTE_MS bounds the wait for each looped-back byte.
 *
 *   2. "AT" COMMAND PROBE (extra evidence only, NEVER a failure). Any reply
 *      byte within BT_PROBE_WINDOW_MS, retried BT_PROBE_ATTEMPTS times, proves
 *      the module itself is alive - but silence proves nothing, because an
 *      HC-05 whose KEY/EN pin is not driven high is permanently in DATA mode
 *      and forwards "AT" over the air instead of answering it. Treating
 *      silence as a fault is exactly what used to report "HC-05 NOT
 *      CONFIGURED" for a perfectly wired module.
 *
 *   3. OPTIONAL definitive module check - see BT_KEY_CTRL_ENABLED below.
 * ------------------------------------------------------------------------ */
#define BT_PROBE_WINDOW_MS 300U
#define BT_PROBE_ATTEMPTS  3U
#define POST_RETRY_MS      2000U
#define BT_LOOPBACK_BYTE_MS 20U   /* Max wait for one looped-back byte */

/*######################################################################################################*/
/*OPTIONAL BT WIRING : FOR PIN P0.6 TO BT MODULE EN/KEY . */
/*######################################################################################################*/
/* --- OPTIONAL: definitive HC-05 presence check via its KEY/EN pin ---------
 * DEFAULT: DISABLED (0). Enable this ONLY if you add one extra wire.
 *
 * An HC-05 only accepts AT commands while its KEY (a.k.a. EN) pin is held
 * HIGH, and in that command mode it runs at a fixed 38400 baud regardless of
 * the baud rate configured for data mode. With KEY left unconnected - which is
 * how this project is wired - the module can NEVER answer an AT probe, so
 * "module present" cannot be proven, only "MCU side is fine" (step 1 above).
 *
 * If you want the POST to state definitively whether the HC-05 module itself
 * is present and healthy:
 *   1. Wire HC-05 KEY/EN  ->  the LPC2148 pin named by BT_KEY_PIN_BIT below
 *      (P0.6 by default: a free GPIO, not used by anything else in this
 *      project - see the pin map at the top of projectmain.c).
 *   2. Change BT_KEY_CTRL_ENABLED to 1 and rebuild.
 * The POST will then drive KEY high, re-program UART1 to BT_CMD_BAUD, send
 * "AT", require "OK" back, and restore data mode afterwards - a real
 * pass/fail answer for the module, not just for the wiring.
 * ------------------------------------------------------------------------ */
#define BT_KEY_CTRL_ENABLED 0            /* 0 = KEY pin not wired (default). Change this from: 0 --> 1 */
#define BT_KEY_PIN_BIT      6            /* P0.6 -> HC-05 KEY/EN, if wired   */
#define BT_DATA_BAUD        9600UL       /* Normal (data mode) baud rate     */
#define BT_CMD_BAUD         38400UL      /* HC-05 command-mode baud (fixed)  */

/* --- Locker motor (gate-style open/close) timing --------------------------
 * The locker motor is a plain DC motor driven through an L293D (no encoder
 * or servo positioning), so "how far it turns" is controlled purely by how
 * long it is driven for. MOTOR_ROTATE_MS is the ON-time used for a single
 * open (forward) or close (reverse) movement.
 *
 * TUNE THIS VALUE FOR YOUR HARDWARE: start low (e.g. 300-500 ms) and
 * increase gradually while checking the physical rotation, until one pulse
 * turns the gate/latch about half a turn (180 degrees). Driving the motor
 * for several seconds (as a raw "hold direction" delay) lets it complete
 * many full rotations instead of a single controlled half-turn, which is
 * what previously made it look like it was spinning repeatedly.
 * MOTOR_SETTLE_MS is a short full-stop pause inserted before reversing
 * direction, to avoid mechanically/electrically shocking the motor and
 * H-bridge by reversing it while it still has momentum.
 * ------------------------------------------------------------------------ */
#define MOTOR_ROTATE_MS    500   /* ON-time for one ~180-degree open/close turn */
#define MOTOR_SETTLE_MS    200   /* Full-stop pause before reversing direction  */

/* --- Login lockout (merged in from the "EnviroTime" project's security
 * module) ---------------------------------------------------------------
 * After MAX_WRONG_ATTEMPTS consecutive failed login attempts (a failed
 * attempt is either a wrong Level-1 Bluetooth password, or a correct
 * Level-1 followed by a wrong Level-2 keypad password), the system locks
 * out for LOCK_DURATION_MS milliseconds, showing "SYSTEM LOCKED" on the
 * LCD with a live seconds countdown, before automatically resetting the
 * attempt counter.
 * ------------------------------------------------------------------------ */
#define MAX_WRONG_ATTEMPTS   3U       /* Lock after this many consecutive failures */
#define LOCK_DURATION_MS     30000U   /* Lock duration: 30 seconds                 */

/* --- Locker "held open" time ----------------------------------------------
 * How long the gate/latch stays open, between the opening pulse and the
 * closing pulse, for the visitor to use the locker. */
#define LOCKER_OPEN_HOLD_MS  5000U

/* --- Post-unlock idle display ----------------------------------------------
 * After a successful Level-1 + Level-2 unlock (and the motor open/close
 * sequence has finished), the idle screen shows the live real-time clock for
 * this long, then reverts to the "press password" prompt so nobody is left
 * staring at a clock with no instruction on it. The window restarts on every
 * unlock. Measured against millis() like every other timeout in the project.
 * -------------------------------------------------------------------------- */
#define POST_UNLOCK_RTC_DISPLAY_MS  30000U   /* 30 s of live clock after each unlock */

/* --- Keypad (Level-2) entry timeouts --------------------------------------
 * The Level-2 keypad prompt must never wait forever: if the user walks away
 * mid-entry the locker would otherwise sit indefinitely with Level-1
 * already satisfied, so the next person to touch the keypad only has to
 * beat one factor instead of two. The captured audit log in 1.TXT shows a
 * real 15-minute-55-second gap of exactly this kind.
 *
 * TWO INDEPENDENT LIMITS RUN AT THE SAME TIME, and whichever expires first
 * ends the entry:
 *
 *   L2_TOTAL_TIMEOUT_MS (3 min) - a HARD CEILING on the whole Level-2 entry,
 *       measured from the moment the prompt appears. It is never extended by
 *       anything the user does. This is what stops the authenticated window
 *       from being held open indefinitely by tapping a key every so often.
 *
 *   L2_INTERKEY_TIMEOUT_MS (1 min) - a PER-CHARACTER idle limit: how long the
 *       locker will wait for the NEXT keypress. It restarts from the full
 *       minute on every keypress, so for the password "5678", pressing '5'
 *       gives a fresh minute to press '6', pressing '6' gives a fresh minute
 *       to press '7', and so on. It is deliberately restarted by ANY key -
 *       including the '*' backspace, the '#' clear and the unused A-D keys -
 *       because its job is to detect a user who has WALKED AWAY, and any
 *       keypress at all proves somebody is still standing there. Restarting it
 *       can never extend the session beyond L2_TOTAL_TIMEOUT_MS above.
 *
 * The number of whole seconds left before the earlier of the two expires is
 * shown live in the top-right corner of the LCD while the user types, with
 * the remaining total on the second line, so nobody is ever cut off without
 * warning. Both are measured against the Timer1 millisecond time base
 * (millis(), see delay.h), so the countdown tracks a real stopwatch.
 *
 * On expiry the entry is abandoned, the Bluetooth receive buffer is flushed,
 * and the system returns to the Level-1 locked state - so the user must send
 * the Bluetooth password again before being offered the keypad prompt a
 * second time.
 *
 * A Level-2 timeout is deliberately NOT counted as a failed attempt toward
 * MAX_WRONG_ATTEMPTS: no password was actually guessed, so the session is
 * simply abandoned rather than treated as an attack.
 *
 * ADMIN_PWD_TIMEOUT_MS is the equivalent budget for each password field in
 * the admin menu's password-change screen, and MENU_INPUT_TIMEOUT_MS for the
 * CLK/Alarm value-entry screens (both shorter, since an admin is standing at
 * the device). Both also show a live countdown.
 *
 * KEY_POLL_MS is how long the keypad wait sleeps between matrix scans;
 * KEY_RELEASE_MAX_MS bounds how long keypad_scan() waits for a key to be
 * released, so a stuck or held-down key can never stall a timeout.
 * ------------------------------------------------------------------------ */
#define L2_TOTAL_TIMEOUT_MS    180000UL /* 3 min ceiling for the whole L2 entry   */
#define L2_INTERKEY_TIMEOUT_MS 60000UL  /* 1 min to press the NEXT character      */
#define ADMIN_PWD_TIMEOUT_MS   60000UL  /* 1 min per admin password field         */
#define MENU_INPUT_TIMEOUT_MS  60000UL  /* 1 min per admin CLK/Alarm input screen */
#define KEY_POLL_MS            10U      /* Keypad re-scan interval while waiting   */
#define KEY_RELEASE_MAX_MS     500U     /* Max wait for key release (anti-stall)   */

/* --- Is the 1-minute idle timer armed BEFORE the first character? ----------
 * The specification is explicit about what happens once typing has started
 * ("if the user types 5 the timer should count again for 60 sec"), but it can be
 * read two ways for the period BEFORE any key has been touched. This constant
 * makes the choice deliberate instead of accidental:
 *
 *   1 (DEFAULT) - the 1-minute idle limit applies from the moment the keypad
 *       prompt appears. Nobody at the keypad for a minute means the entry is
 *       abandoned. This is the stricter reading and the safer posture: it stops
 *       an authenticated Level-1 session from standing open for three minutes
 *       with nobody there. Both limits still matter, because four characters at
 *       up to a minute apiece would exceed the 3-minute ceiling.
 *
 *   0 - the user gets the full 3 minutes to press the FIRST character, and the
 *       1-minute per-character limit only starts applying afterwards.
 *
 * Either way the countdown on the LCD always shows the limit that is actually
 * in force, so the behaviour is never a surprise to the person standing there.
 * ------------------------------------------------------------------------ */
#define L2_IDLE_TIMER_ARMED_AT_START  1

/* --- Admin menu idle timeout ----------------------------------------------
 * If no key is pressed for MENU_TIMEOUT_MS while the TOP-LEVEL admin menu is
 * on screen, the menu exits by itself (ATM-style).
 *
 * IMPORTANT: this covers ONLY the top-level menu screen. Every sub-screen
 * reached from it has to bound its own waits - which is why the password
 * fields use ADMIN_PWD_TIMEOUT_MS and the CLK/Alarm screens use
 * MENU_INPUT_TIMEOUT_MS. (These two values used to be #defined inside
 * admin_menu() itself; they live here now so every timeout in the project has
 * one, findable home.)
 * ------------------------------------------------------------------------ */
#define MENU_TIMEOUT_MS      15000U   /* 15 s idle on the top-level menu -> exit */
#define MENU_POLL_MS         100U     /* How often that screen re-scans the keypad */

/* --- LCD layout: where a seconds countdown is drawn -----------------------
 * A countdown is always drawn right-aligned as 3 digits at this column, with
 * a fixed 's' in the last column (15) of a 16-column display, i.e. columns
 * 12-14 hold "180", " 60", "  9" and column 15 holds 's'. Keeping it in one
 * place means the L2 prompt, the admin password fields and the lockout screen
 * all put their countdown in the same corner.
 * ------------------------------------------------------------------------ */
#define LCD_COUNTDOWN_COL    12U      /* First of the 3 digit columns   */
#define LCD_COUNTDOWN_UNIT   15U      /* Column holding the fixed 's'    */

/* --- OPTIONAL battery-backed external RTC (DS1307 / DS3231) ----------------
 * The LPC2148's on-chip RTC is clocked from PCLK only: it has NO dedicated
 * 32.768 kHz crystal pins (no RTCX1/RTCX2) and NO VBAT backup pin, so it
 * FREEZES during a full power-off no matter what. The clock "coming back at
 * 12:00 PM" every power cycle had two causes - the boot code reset it to that
 * value unconditionally on every start (fixed in rtc.c: the default is now
 * applied only when the RTC is completely unset), and nothing on the chip can
 * tick while unpowered.
 *
 * To make the clock genuinely survive power-off (the faculty's "real RTC
 * using the external clock" advice), fit a DS1307 or DS3231 RTC IC: both are
 * I2C, both live at address 0xD0, both are battery-backed (wire the vector
 * board's coin cell to the IC's VBAT) and both keep time from their own
 * 32.768 kHz crystal while the system is off. rtc.c probes for one at every
 * boot; if found it restores the real time into the on-chip RTC (so the menu,
 * the alarm and the audit log all keep working unchanged) and mirrors every
 * clock write back to it.
 *
 * RTC_EXT_ENABLED: 1 = probe for and use the external RTC (recommended,
 * default). 0 = never touch the I2C bus for the RTC; the on-chip RTC is the
 * only clock and time cannot survive power-off.
 * -------------------------------------------------------------------------- */
#define RTC_EXT_ENABLED    1     /* 1 = use the battery-backed external RTC if fitted */
#define RTC_EXT_ADDR       0xD0  /* DS1307/DS3231 I2C address, write bit clear (0xD1 = read) */

/* --- Admin push-button (EINT2) configuration --------------------------------
 * The admin button is wired to P0.7 which, when configured for its
 * alternate function, becomes external interrupt input EINT2.
 * ------------------------------------------------------------------------ */
#define ADMIN_EINT_BIT     2    /* Bit position of EINT2 in EXTINT/EXTMODE/EXTPOLAR */
#define ADMIN_VIC_SRC      16   /* VIC channel number for the EINT2 interrupt source */

/* U1IER (UART1 Interrupt Enable Register) is not defined in every version
 * of the vendor header <lpc214x.h>, so it is defined manually here as a
 * safety net to guarantee the project always compiles. */
#ifndef U1IER
#define U1IER (*((volatile unsigned long *)0xE0010004))
#endif

#endif
