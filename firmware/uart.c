/*=============================================================================
 * File        : uart.c
 * Project     : Bluetooth-Based Secure Locker with Access Logging
 * Description : UART0 is used as a debug/log console (connected to a PC via
 *               a USB-to-serial adapter). UART1 provides basic polled
 *               transmit support; UART1 is also used by the HC-05 Bluetooth
 *               module (see bluetooth.c, which additionally configures the
 *               UART1 receive interrupt on the same pins).
 *
 * Wiring:
 *   UART0 TXD0 -> P0.0   (LPC2148 -> PC RX, via serial adapter)
 *   UART0 RXD0 -> P0.1   (LPC2148 <- PC TX, via serial adapter)
 *   UART1 TXD1 -> P0.8   (LPC2148 -> HC-05 RXD)
 *   UART1 RXD1 -> P0.9   (LPC2148 <- HC-05 TXD)
 *
 * Register value reference:
 *   UxLCR = 0x83 -> 8 data bits, no parity, 1 stop bit, DLAB = 1 (baud rate
 *                   divisor latches accessible for programming)
 *   UxLCR = 0x03 -> same line format, DLAB = 0 (normal operating mode)
 *   UxFCR = 0x07 -> enable and reset the RX/TX FIFOs
 *
 * Baud rate divisor is computed assuming PCLK = 15 MHz (see PLL setup in
 * projectmain.c): divisor = PCLK / (16 * baud).
 *===========================================================================*/
#include <lpc214x.h>
#include "uart.h"

/* Upper bound on how long either transmitter will spin waiting for its
 * transmit holding register to go empty (THRE) before giving up and dropping
 * the byte.
 *
 * WHY A BOUND IS NEEDED: these waits used to be unconditional
 * "while (!(U0LSR & 0x20));" spins. THRE always does go empty on a correctly
 * configured UART, but if the peripheral is ever left unclocked or
 * misconfigured (a wrong VPBDIV/PCLK, or a divisor programmed while DLAB was
 * clear) it never sets - and because log_event() calls this for every single
 * audit-log character, the whole firmware would hang inside the logger. Worse,
 * that includes the boot self-test's own failure messages, so a UART fault
 * would present as a completely dead board with no diagnostic at all.
 *
 * Dropping a log character is a cosmetic loss; hanging the locker is not. This
 * many iterations of a tight ARM7 load/test loop at 60 MHz is several
 * milliseconds - far longer than the ~1 ms one character takes at 9600 baud,
 * so it can never trip during normal operation. */
#define UART_TX_WAIT_LIMIT   200000UL

/* Initialise UART0 for the given baud rate (PCLK = 15 MHz assumed).
 * P0.0/P0.1 are switched from GPIO to their UART0 TXD0/RXD0 alternate
 * function via PINSEL0. */
void uart0_init(u32 baud)
{
    u32 divisor = 15000000UL / (16UL * baud);

    PINSEL0 &= ~0x0000000F;   /* Clear P0.0/P0.1 function bits             */
    PINSEL0 |=  0x00000005;   /* Select TXD0 (P0.0) and RXD0 (P0.1)         */

    U0LCR = 0x83;             /* 8N1, DLAB = 1 to access divisor registers  */
    U0DLL = divisor & 0xFF;         /* Divisor low byte                    */
    U0DLM = (divisor >> 8) & 0xFF;  /* Divisor high byte                   */
    U0LCR = 0x03;             /* DLAB = 0, back to normal register access   */
    U0FCR = 0x07;             /* Enable and reset the TX/RX FIFOs           */
}

/* Transmit a single byte on UART0, waiting (with a bound - see
 * UART_TX_WAIT_LIMIT) until the transmit holding register is empty (THRE bit
 * set in U0LSR). The byte is dropped rather than hanging the system if THRE
 * never comes. */
void uart0_tx(u8 ch)
{
    u32 guard = UART_TX_WAIT_LIMIT;

    while (!(U0LSR & 0x20))    /* Wait until THR is empty */
    {
        if (--guard == 0UL)
            return;             /* Transmitter stuck - drop the byte, keep running */
    }

    U0THR = ch;
}

/* Transmit a null-terminated string on UART0, one byte at a time. */
void uart0_string(const char *str)
{
    while (*str)
        uart0_tx(*str++);
}

/* Transmit a signed integer as human-readable decimal text on UART0.
 * Digits are extracted least-significant-first into a small buffer, then
 * sent back out in the correct (most-significant-first) order. */
void uart0_int(s32 num)
{
    char buf[12];
    s32 i = 0;

    if (num == 0)
    {
        uart0_tx('0');
        return;
    }

    if (num < 0)
    {
        uart0_tx('-');
        num = -num;
    }

    while (num > 0)
    {
        buf[i++] = (num % 10) + '0';
        num /= 10;
    }

    while (i > 0)
        uart0_tx(buf[--i]);
}

/* Initialise UART1 for the given baud rate.
 *
 * This is the SINGLE place UART1's pins, frame format and baud divisor are
 * programmed. bluetooth_init() calls it and then adds only what is specific to
 * the interrupt-driven HC-05 receiver (loopback off, RX interrupt enabled, VIC
 * slot registered). Previously bluetooth_init() carried its own duplicate copy
 * of this setup, so uart1_init() was dead code and the two copies could drift
 * apart. */
void uart1_init(u32 baud)
{
    u32 divisor = 15000000UL / (16UL * baud);

    PINSEL0 &= ~(0x000F0000);              /* Clear P0.8/P0.9 function bits */
    PINSEL0 |=  (1UL << 16) | (1UL << 18); /* Select TXD1 (P0.8) and RXD1 (P0.9) */

    U1LCR = 0x83;
    U1DLL = divisor & 0xFF;
    U1DLM = (divisor >> 8) & 0xFF;
    U1LCR = 0x03;
    U1FCR = 0x07;
}

/* Transmit a single byte on UART1, waiting (with the same bound as UART0 -
 * see UART_TX_WAIT_LIMIT) until the transmit holding register is empty. */
void uart1_tx(u8 ch)
{
    u32 guard = UART_TX_WAIT_LIMIT;

    while (!(U1LSR & 0x20))
    {
        if (--guard == 0UL)
            return;             /* Transmitter stuck - drop the byte, keep running */
    }

    U1THR = ch;
}
/* Transmit a null-terminated string on UART1, one byte at a time. */
void uart1_string(const char *str)
{
    while (*str)
        uart1_tx(*str++);
}
