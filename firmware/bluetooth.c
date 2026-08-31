/*=============================================================================
 * File        : bluetooth.c
 * Project     : Bluetooth-Based Secure Locker with Access Logging
 * Description : Receives password commands from an HC-05 Bluetooth-to-serial
 *               module over UART1, using an interrupt service routine so
 *               the main loop never has to block waiting on individual
 *               bytes.
 *
 * HC-05 wiring (over UART1):
 *   HC-05 RXD <- P0.8  (LPC2148 TXD1)
 *   HC-05 TXD -> P0.9  (LPC2148 RXD1)
 *   HC-05 VCC -> 5V (module has its own onboard regulator/level shifting)
 *   HC-05 GND -> GND
 *
 * Protocol: the paired mobile phone/app sends the password digits followed
 * by a '#' terminator, e.g. "1234#". Any trailing carriage return / line
 * feed characters that the app appends after sending are ignored by the
 * receiver (see UART1_ISR below for why this matters).
 *
 * This file also contains the boot self-test for the Bluetooth interface. See
 * the big "BOOT SELF-TEST" comment block near the bottom: the module cannot be
 * interrogated with AT commands in this project's wiring, so the link is proven
 * with an internal UART1 loopback instead, and an unanswered "AT" is no longer
 * misreported as "HC-05 not configured".
 *===========================================================================*/
#include <lpc214x.h>
#include "bluetooth.h"
#include "defines.h"
#include "delay.h"
#include "uart.h"

/* Ring/line buffer that accumulates incoming characters until a
 * terminator is seen, plus the bookkeeping needed by the ISR and the
 * foreground (main-loop) code that eventually reads it out. */
volatile char bt_buffer[BT_BUF_SIZE];
volatile u8 bt_index = 0;       /* Current write position inside bt_buffer   */
volatile u8 bt_rx_ready = 0;    /* Set to 1 by the ISR once a full command has arrived */

/* Sticky BT_RX_* fault flags describing what was wrong with the command
 * currently being assembled / waiting to be read. Accumulated by the ISR,
 * consumed and cleared by bluetooth_read_command(). */
volatile u8 bt_fault = 0;

/* Incremented on EVERY byte the ISR pulls out of the FIFO, including bytes
 * that are ignored or discarded. The foreground uses it purely as an
 * "is the line still busy?" indicator - see bluetooth_settle() - and as the
 * "did the HC-05 answer?" indicator in bluetooth_selftest(). Wrapping the
 * counter is harmless, since only *changes* in its value are ever tested. */
volatile u16 bt_activity = 0;

__irq void UART1_ISR(void);

/* Configure UART1 for communication with the HC-05 module and enable its
 * receive-data-available interrupt on the VIC (Vectored Interrupt
 * Controller), channel 7 (UART1).
 *
 * The pins, frame format and baud divisor are programmed by uart1_init() so
 * there is exactly ONE copy of that code (this function used to carry a
 * duplicate of it, which left uart1_init() as dead code and let the two copies
 * drift apart). Everything added here is specific to the interrupt-driven
 * receiver. */
void bluetooth_init(u32 baud)
{
    uart1_init(baud);                /* Pins P0.8/P0.9, 8N1, baud divisor, FIFOs */

    U1MCR = 0x00;                    /* Modem control: loopback OFF, lines idle.
                                      * Set explicitly rather than assumed,
                                      * because bluetooth_loopback_test() below
                                      * turns loopback on and must be certain of
                                      * the state it is returning to. */

    U1IER = 0x01;                    /* Enable "Receive Data Available" interrupt */

    VICIntSelect &= ~(1UL << 7);     /* UART1 (VIC channel 7) -> IRQ, not FIQ */
    VICVectAddr1  = (u32)UART1_ISR;  /* Register our ISR in vectored slot 1   */
    VICVectCntl1  = 0x20 | 7;        /* Enable slot 1, assign it to source 7  */
    VICIntEnable  = (1UL << 7);      /* Unmask the UART1 interrupt source     */
}

/* UART1 receive interrupt handler: read every available byte from the
 * RX FIFO, appending it to bt_buffer. Only '#' terminates a command and
 * signals the main loop (via bt_rx_ready) that a full command is ready.
 *
 * IMPORTANT: '\r' and '\n' are deliberately IGNORED here, not treated as
 * additional terminators, AND not treated as trailing junk either. Most
 * Bluetooth serial-terminal apps automatically append a trailing CR and/or
 * LF after the text you send, in addition to the '#' you typed (e.g.
 * sending "1234#" actually transmits '1','2','3','4','#','\n'). If that
 * trailing '\n' were treated as a terminator it would arrive in its own
 * interrupt shortly after the '#' and overwrite the just-received command
 * with an empty string; if it were treated as trailing junk it would
 * invalidate every single correct password. So CR/LF are checked for and
 * skipped FIRST, before any other classification.
 *
 * Two abuse cases are detected and recorded in bt_fault:
 *
 *   BT_RX_TRAILING - any real (non-CR/LF) character that arrives while a
 *     completed command is still waiting to be read. This is the
 *     "1234#123456789" bypass: previously those bytes were silently
 *     dropped, so the main loop saw a pristine "1234", matched it, and
 *     granted access even though the user typed a much longer string. The
 *     bytes are still dropped (they must not corrupt the pending command)
 *     but the attempt is now permanently tainted, and the foreground
 *     rejects it.
 *
 *   BT_RX_OVERFLOW - a payload longer than BT_MAX_PAYLOAD characters.
 *     Previously the excess was silently truncated, so a 40-character entry
 *     was compared as its first 31 characters. Now the whole attempt is
 *     rejected and logged.
 *
 * The drain loop is BOUNDED by BT_ISR_MAX_BYTES. The UART1 receive FIFO is 16
 * bytes deep, so a healthy interrupt can never need more passes than that; the
 * bound only matters if RDR ever failed to clear, in which case an unbounded
 * "while (U1LSR & 0x01)" would spin inside an interrupt handler forever and
 * take the entire locker down with no diagnostic and no way out. Bytes still
 * waiting after the bound simply cause the interrupt to fire again. */
__irq void UART1_ISR(void)
{
    char ch;
    u8   drain = BT_ISR_MAX_BYTES;

    while ((U1LSR & 0x01) && (drain > 0U))   /* While Receiver Data Ready (RDR) is set */
    {
        drain--;

        ch = U1RBR;         /* Reading U1RBR also clears the RDR flag    */

        bt_activity++;      /* Mark the line as busy (see bluetooth_settle) */

        if (ch == '\r' || ch == '\n')
        {
            /* Framing noise added by phone apps: never payload, never a
             * terminator, and explicitly NOT counted as trailing junk. */
            continue;
        }

        if (bt_rx_ready)
        {
            /* A completed command is still waiting for the main loop to
             * read it. Discard this byte so it cannot corrupt the pending
             * command, but remember that it happened - the pending attempt
             * is no longer a clean, exact password. */
            bt_fault |= BT_RX_TRAILING;
            continue;
        }

        if (ch == '#')
        {
            bt_buffer[bt_index] = '\0';   /* Terminate the received string */
            bt_rx_ready = 1;              /* Tell the main loop it's ready */
            bt_index = 0;                 /* Reset for the next command    */
        }
        else if (bt_index < BT_MAX_PAYLOAD)
        {
            bt_buffer[bt_index++] = ch;   /* Store the character */
        }
        else
        {
            /* Buffer full and still no terminator: the sender has exceeded
             * the RX buffer size. Record it and keep dropping bytes until a
             * terminator turns up (or the foreground gives up on us). */
            bt_fault |= BT_RX_OVERFLOW;
        }
    }

    VICVectAddr = 0;   /* Acknowledge interrupt to the VIC (end-of-interrupt) */
}

/* Non-blocking check: returns non-zero once there is something for the
 * foreground to act on - either a complete terminated command, OR an
 * over-length payload that must be reported even though no '#' ever
 * arrived (otherwise a long unterminated flood would be ignored in
 * silence and the user would get no feedback at all). */
u8 bluetooth_available(void)
{
    return (u8)(bt_rx_ready || (bt_fault & BT_RX_OVERFLOW));
}

/* Wait until the HC-05 receive line has been quiet for BT_SETTLE_MS before
 * a pending command is acted on.
 *
 * WHY THIS IS NEEDED: a phone app sends a whole line as one back-to-back
 * burst. At 9600 baud a character takes only ~1 ms, but the main loop polls
 * every 100 ms, so for "1234#123456789" the '#' and all nine trailing
 * characters arrive within ~15 ms - long before the foreground looks. That
 * ordering happens to work in our favour, but relying on it would make
 * security depend on timing luck. Explicitly waiting for the burst to end
 * makes the trailing-junk check deterministic: by the time this returns,
 * every byte the sender transmitted has been classified by the ISR.
 *
 * The quiet window restarts whenever another byte arrives, so this tracks
 * the true end of the transmission however long it is - but the TOTAL wait
 * is capped at BT_SETTLE_MAX_MS, measured against the millis() time base.
 * Without that cap, a sender who streams bytes continuously would keep
 * restarting the quiet window and hold the main loop here forever, which would
 * also stop the tamper switch from being polled. Hitting the cap is itself a
 * sign of abuse, and the caller still rejects the attempt: such a flood will
 * have set BT_RX_OVERFLOW and/or BT_RX_TRAILING long before the cap is
 * reached. */
void bluetooth_settle(void)
{
    u16 last;
    u8  quiet_steps = 0;
    u32 start       = millis();

    while (quiet_steps < (BT_SETTLE_MS / BT_SETTLE_STEP_MS))
    {
        if (elapsed_since(start) >= BT_SETTLE_MAX_MS)
            return;              /* Cap reached - stop waiting on a hostile stream */

        last = bt_activity;
        delay_ms(BT_SETTLE_STEP_MS);

        if (bt_activity != last)
            quiet_steps = 0;     /* Still receiving - restart the quiet window */
        else
            quiet_steps++;
    }
}

/* Copy the most recently received command out of the (volatile) internal
 * buffer into the caller's buffer (which must be at least BT_BUF_SIZE
 * bytes), then clear the ready/fault flags so a new command can be
 * received.
 *
 * Returns the accumulated BT_RX_* status flags: BT_RX_OK for a clean
 * payload, or a combination of BT_RX_EMPTY / BT_RX_OVERFLOW /
 * BT_RX_TRAILING. The caller MUST reject anything other than BT_RX_OK
 * without comparing it against the stored password.
 *
 * The UART1 interrupt is masked for the duration so the snapshot of
 * (buffer + flags) and the reset of that state happen atomically with
 * respect to the ISR - otherwise a byte arriving mid-copy could either be
 * lost or leak a stale fault flag into the NEXT command. */
u8 bluetooth_read_command(char *buf)
{
    u8 i;
    u8 status;

    VICIntEnClr = (1UL << 7);      /* --- critical section: mask UART1 IRQ --- */

    for (i = 0; i < BT_BUF_SIZE; i++)
    {
        buf[i] = bt_buffer[i];
        if (buf[i] == '\0')
            break;
    }
    buf[BT_BUF_SIZE - 1] = '\0';   /* Guarantee null-termination */

    status = bt_fault;

    /* An over-length payload is reported even if no '#' ever arrived, so
     * the partial contents of the buffer are meaningless - make sure the
     * caller cannot accidentally compare them as a password. */
    if (status & BT_RX_OVERFLOW)
        buf[0] = '\0';
    else if (buf[0] == '\0')
        status |= BT_RX_EMPTY;     /* A bare '#' with no digits before it */

    /* Reset everything for the next command. */
    bt_buffer[0] = '\0';
    bt_index     = 0;
    bt_fault     = 0;
    bt_rx_ready  = 0;

    VICIntEnable = (1UL << 7);     /* --- end critical section --- */

    return status;
}

/* Discard whatever has been received so far without processing it, and
 * clear every status flag (e.g. used to recover from an unexpected or
 * partial command, after a lockout, or after a Level-2 timeout, so stale
 * input cannot trigger the next authentication attempt). */
void bluetooth_clear(void)
{
    VICIntEnClr = (1UL << 7);

    bt_index    = 0;
    bt_buffer[0] = '\0';
    bt_rx_ready = 0;
    bt_fault    = 0;

    VICIntEnable = (1UL << 7);
}

/*=============================================================================
 * BOOT SELF-TEST
 *
 * THE BUG THIS SECTION EXISTS TO FIX
 * ----------------------------------
 * The previous self-test sent "AT" to the HC-05 and declared the module
 * missing if nothing came back. On correctly wired hardware that reported
 * "HC-05 BT NOT CONFIGURED" at every boot - a false alarm, and the reason the
 * boot check could not be trusted.
 *
 * The cause is a property of the module, not of the wiring. An HC-05 has two
 * modes:
 *   DATA mode    - KEY/EN pin LOW or unconnected. Everything the MCU sends is
 *                  transmitted over the air. "AT" is NOT a command here; the
 *                  module sends the two characters to the phone and answers
 *                  nothing. This is the mode this project's wiring always puts
 *                  the module in, because KEY is not connected.
 *   COMMAND mode - KEY/EN pin held HIGH. AT commands work, at a fixed 38400
 *                  baud regardless of the data-mode baud rate.
 * So with KEY unwired, silence in response to "AT" is the EXPECTED behaviour of
 * a perfectly healthy module, and cannot be used as a fault indication.
 *
 * WHAT IS TESTED INSTEAD
 * ----------------------
 * 1. bluetooth_loopback_test() - deterministic, and the part that can actually
 *    fail for a real reason: the UART1 transmitter is internally connected to
 *    its own receiver and known byte patterns are pushed through. This proves
 *    the pin selection, baud divisor, frame format, FIFOs and the whole receive
 *    path really are configured. If this fails, the Bluetooth interface on the
 *    MCU side is genuinely not configured and the locker says so.
 * 2. An "AT" probe, kept only as EXTRA EVIDENCE. If the module does answer
 *    (some are left in command mode, or have KEY tied high on the carrier
 *    board) that is proof it is present, and the POST reports the stronger
 *    result. Silence is never treated as a failure.
 * 3. Optionally, with one extra wire, a definitive module test - see
 *    BT_KEY_CTRL_ENABLED in defines.h.
 *
 * WHAT THE POST STILL CANNOT PROVE, and why that is honest
 * -------------------------------------------------------
 * Without the KEY wire there is no way to distinguish "healthy HC-05 sitting
 * quietly in data mode" from "no HC-05 attached at all", because the LPC2148's
 * port pins have fixed internal pull-ups: an unconnected RXD1 idles HIGH,
 * which is exactly what a connected, idle module's TXD line also looks like. So
 * the firmware reports precisely what it knows - the link is configured - and
 * says plainly in the log that module presence could not be interrogated.
 * Claiming more than that is what produced the false alarm in the first place.
 *===========================================================================*/

/* Internal-loopback test of the UART1 peripheral itself.
 *
 * Setting the LMS bit in U1MCR wires the transmitter's output straight to the
 * receiver's input INSIDE the UART block, and holds the TXD1 pin in its idle
 * (marking) state while it is set - so this test sends nothing at all to the
 * HC-05 and cannot disturb a paired phone.
 *
 * Four byte patterns are used. 0x55 and 0xAA are bitwise inverses, so a
 * receive line stuck permanently high or low cannot pass both; 0x0F and 0xF0
 * additionally exercise a transition in the middle of the frame, which catches
 * a grossly wrong baud divisor. Each byte must come back within
 * BT_LOOPBACK_BYTE_MS, unchanged, with no framing/parity/overrun error - a
 * framing error here means the divisor or the 8N1 format is wrong.
 *
 * The UART1 interrupt is masked for the duration and both FIFOs are flushed
 * afterwards, so a looped-back test byte can never reach UART1_ISR and be
 * mistaken for part of a password.
 *
 * Returns 1 if the UART1 link is proven good, 0 if it is not. */
u8 bluetooth_loopback_test(void)
{
    static const u8 pattern[4] = { 0x55U, 0xAAU, 0x0FU, 0xF0U };

    u8  i;
    u8  ok = 1U;
    u8  drain;
    u32 lsr;
    u32 start;

    /* --- critical section: the ISR must not see the test bytes --- */
    VICIntEnClr = (1UL << 7);

    U1FCR = 0x07;              /* Flush TX+RX FIFOs: start from a known state */
    U1MCR = 0x10;              /* LMS = 1: internal loopback on, TXD1 held idle */

    /* Drain any byte that survived the flush (bounded, for the same reason as
     * the drain loop in UART1_ISR). */
    drain = BT_ISR_MAX_BYTES;
    while ((U1LSR & 0x01) && (drain > 0U))
    {
        drain--;
        (void)U1RBR;
    }

    for (i = 0U; i < 4U; i++)
    {
        uart1_tx(pattern[i]);   /* Bounded THRE wait lives inside uart1_tx() */

        /* Bounded wait for the byte to appear at the receiver. */
        start = millis();
        do
        {
            lsr = (u32)U1LSR;
        } while (((lsr & 0x01UL) == 0UL) &&
                 (elapsed_since(start) < BT_LOOPBACK_BYTE_MS));

        if ((lsr & 0x01UL) == 0UL)
        {
            ok = 0U;            /* Nothing came back - receive path is dead */
            break;
        }

        /* Bits 1-4 = overrun/parity/framing/break, bit 7 = error in RX FIFO.
         * Any of them means the byte was mangled, i.e. a wrong divisor or
         * frame format rather than a wiring problem. */
        if ((lsr & 0x9EUL) != 0UL)
            ok = 0U;

        if ((u8)U1RBR != pattern[i])
            ok = 0U;            /* Came back corrupted */

        if (!ok)
            break;
    }

    U1MCR = 0x00;              /* Loopback off: back to the real TXD1/RXD1 pins */
    U1FCR = 0x07;              /* Flush again so no test byte can ever be read */

    /* Reset the receiver state and re-enable the UART1 interrupt (this call
     * ends by unmasking VIC channel 7, which closes the critical section). */
    bluetooth_clear();

    return ok;
}

/* Send "AT" and report whether ANY byte came back within BT_PROBE_WINDOW_MS,
 * retried BT_PROBE_ATTEMPTS times.
 *
 * A reply is strong evidence the module is present. NO reply means nothing at
 * all in this project's wiring (see the section header above), which is why the
 * result of this probe can only ever UPGRADE the POST result, never fail it. */
static u8 bt_at_probe(void)
{
    u8  attempt;
    u8  window;
    u16 before;

    for (attempt = 0; attempt < BT_PROBE_ATTEMPTS; attempt++)
    {
        bluetooth_clear();          /* Ignore anything already buffered */
        before = bt_activity;

        uart1_string("AT\r\n");     /* Polled TX helper from uart.c; UART1 is
                                     * already configured by bluetooth_init() */

        for (window = 0; window < (BT_PROBE_WINDOW_MS / 10U); window++)
        {
            delay_ms(10);

            if (bt_activity != before)
            {
                /* Something came back - the module is alive. Drop whatever
                 * it replied ("OK", or an error string) so the reply is
                 * never mistaken for a password attempt. */
                bluetooth_clear();
                return 1;
            }
        }
    }

    bluetooth_clear();
    return 0;
}

#if BT_KEY_CTRL_ENABLED
/* OPTIONAL definitive module check - compiled in only when the HC-05 KEY/EN
 * pin has been wired to BT_KEY_PIN_BIT and BT_KEY_CTRL_ENABLED is set to 1
 * (see the full instructions in defines.h).
 *
 * Drives KEY high to force the module into command mode, re-programs UART1 to
 * the fixed 38400 baud that command mode uses, sends "AT", and requires a
 * reply. Then it puts everything back: KEY low, data-mode baud rate restored,
 * receiver state cleared. In this configuration silence IS a real fault, so
 * the caller can report a genuinely missing module with certainty.
 *
 * Returns 1 if the module answered in command mode, 0 if it did not. */
static u8 bt_command_mode_probe(void)
{
    u8 answered;

    /* KEY/EN pin as a plain GPIO output, driven HIGH -> command mode. */
    PINSEL0 &= ~(3UL << (BT_KEY_PIN_BIT * 2));
    IO0DIR  |=  (1UL << BT_KEY_PIN_BIT);
    IO0SET   =  (1UL << BT_KEY_PIN_BIT);
    delay_ms(200);                       /* Let the module switch modes */

    uart1_init(BT_CMD_BAUD);             /* Command mode is fixed at 38400 baud */
    U1MCR = 0x00;
    U1IER = 0x01;                        /* Re-arm RX interrupt after the FIFO reset */

    answered = bt_at_probe();

    IO0CLR = (1UL << BT_KEY_PIN_BIT);    /* KEY low -> back to data mode */
    delay_ms(200);

    uart1_init(BT_DATA_BAUD);            /* Restore the data-mode baud rate */
    U1MCR = 0x00;
    U1IER = 0x01;
    bluetooth_clear();

    return answered;
}
#endif

/* Boot self-test. Returns one of the BT_POST_* codes documented in
 * bluetooth.h:
 *
 *   BT_POST_UART_FAIL   - internal loopback failed: the MCU-side UART1 link is
 *                         genuinely not configured. A real fault.
 *   BT_POST_MODULE_FAIL - (KEY pin wired only) UART1 fine, module silent in
 *                         command mode: a genuinely missing/dead module.
 *   BT_POST_LINK_OK     - UART1 proven; module not interrogable. A PASS.
 *   BT_POST_MODULE_OK   - module answered. A PASS, with proof of presence.
 */
u8 bluetooth_selftest(void)
{
    /* Layer 1: the only part that can fail for an unambiguous reason. */
    if (!bluetooth_loopback_test())
        return BT_POST_UART_FAIL;

#if BT_KEY_CTRL_ENABLED
    /* KEY is wired, so the module can be interrogated properly and silence is
     * meaningful. */
    if (bt_command_mode_probe())
        return BT_POST_MODULE_OK;

    return BT_POST_MODULE_FAIL;
#else
    /* KEY is not wired. An answer is a bonus; silence is normal and must NOT
     * be reported as a fault - that was the false alarm. */
    if (bt_at_probe())
        return BT_POST_MODULE_OK;

    return BT_POST_LINK_OK;
#endif
}
