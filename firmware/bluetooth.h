/*=============================================================================
 * File        : bluetooth.h
 * Project     : Bluetooth-Based Secure Locker with Access Logging
 * Description : Public API for the interrupt-driven HC-05 Bluetooth command
 *               receiver running over UART1.
 *===========================================================================*/
#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include "types.h"

/* --- Receive status codes returned by bluetooth_read_command() -------------
 * These are BIT FLAGS, so more than one problem can be reported at once
 * (e.g. an over-length payload that also had junk after the terminator).
 * Test them with '&', not '=='.
 *
 * BT_RX_OK        - a clean, properly terminated payload is in the buffer.
 * BT_RX_EMPTY     - a bare '#' with no password characters before it.
 * BT_RX_OVERFLOW  - the payload exceeded BT_MAX_PAYLOAD characters. The old
 *                   code silently DROPPED the excess characters, so an
 *                   over-long entry could still be compared (and could still
 *                   match) on its first 31 characters; it is now rejected.
 * BT_RX_TRAILING  - extra characters arrived AFTER the '#' terminator but in
 *                   the same transmission burst. This is the "1234#123456789"
 *                   bypass: the old ISR discarded those trailing bytes, so
 *                   the main loop only ever saw a clean "1234" and granted
 *                   access. The attempt is now tainted and rejected.
 * ------------------------------------------------------------------------ */
#define BT_RX_OK        0x00U
#define BT_RX_EMPTY     0x01U
#define BT_RX_OVERFLOW  0x02U
#define BT_RX_TRAILING  0x04U

/* --- Boot self-test result codes, returned by bluetooth_selftest() ---------
 * These are DISTINCT VALUES, not bit flags - compare with '=='.
 *
 * The old self-test returned a plain yes/no: it sent "AT" and called the module
 * missing if nothing came back. That produced a FALSE ALARM on correctly wired
 * hardware - the exact symptom of "the HC-05 is wired properly and it still
 * says BT is not configured" - because an HC-05 whose KEY/EN pin is not driven
 * high is permanently in DATA mode, where "AT" is not a command at all: the
 * module simply transmits those two characters over the air to the phone and
 * stays silent. Silence therefore says nothing about the module, and blocking
 * the boot on it bricked a perfectly good board.
 *
 * The test is now layered, and reports which layer it got to:
 *
 * BT_POST_UART_FAIL   - the UART1 INTERNAL LOOPBACK test failed. This one is
 *                       deterministic and has nothing to do with the module:
 *                       bytes pushed into the transmitter did not come back out
 *                       of the receiver inside the same peripheral. Something
 *                       on the MCU side is genuinely not configured (pin
 *                       selection, baud divisor, frame format, FIFO or the
 *                       receive path). A real, reportable fault.
 * BT_POST_MODULE_FAIL - only reachable when BT_KEY_CTRL_ENABLED is 1 (i.e. the
 *                       HC-05 KEY/EN pin has been wired): UART1 is proven fine,
 *                       the module was put into command mode, and it still did
 *                       not answer. THAT is a genuinely absent or dead module.
 * BT_POST_LINK_OK     - UART1 is proven working and the module did not answer
 *                       "AT". This is the NORMAL, HEALTHY result for this
 *                       project's wiring, and it is a PASS: the boot continues
 *                       and just notes that the module could not be interrogated.
 * BT_POST_MODULE_OK   - the module answered. Present and confirmed talking.
 * ------------------------------------------------------------------------ */
#define BT_POST_UART_FAIL    0U
#define BT_POST_MODULE_FAIL  1U
#define BT_POST_LINK_OK      2U
#define BT_POST_MODULE_OK    3U

void bluetooth_init(u32 baud);          /* Configure UART1 + enable the RX interrupt for the HC-05 module */
u8   bluetooth_available(void);         /* Returns non-zero once a complete command (or an overflow) is pending */
void bluetooth_settle(void);            /* Wait for the RX line to fall quiet so a whole burst is accounted for */
u8   bluetooth_read_command(char *buf); /* Copy the command into buf, clear the flags, and return a BT_RX_* status */
void bluetooth_clear(void);             /* Discard any partially received command and reset all status flags */
u8   bluetooth_loopback_test(void);     /* Prove the UART1 link itself with an internal TX->RX loopback */
u8   bluetooth_selftest(void);          /* Boot POST: returns one of the BT_POST_* codes above */

#endif
