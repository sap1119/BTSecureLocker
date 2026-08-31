/*=============================================================================
 * File        : buzzer.c
 * Project     : Bluetooth-Based Secure Locker with Access Logging
 * Description : Simple GPIO-driven buzzer used to give an audible alert on
 *               wrong-password and tamper-detection events.
 *
 * Wiring:
 *   Buzzer (+) -> P1.26 (through a driver transistor if the buzzer draws
 *                 more current than the LPC2148 GPIO pin can safely supply)
 *   Buzzer (-) -> GND
 *===========================================================================*/
#include <lpc214x.h>
#include "buzzer.h"
#include "delay.h"

#define BUZZER_PIN   (1UL << 26)   /* P1.26 - buzzer control line */

/* Configure the buzzer pin as an output and make sure it starts silent.
 *
 * PINSEL2 bit 2 selects whether P1.26-31 are GPIO (0) or the JTAG/Debug port
 * (1); the buzzer is on P1.26, so that bit must be 0. Bit 3 does the same for
 * the Trace pin group (P1.16-25), which the keypad needs. Only those two bits
 * are cleared - this used to be a blind "PINSEL2 = 0x00000000" which also
 * overwrote the register's reserved bits. */
void buzzer_init(void)
{
    PINSEL2 &= ~0x0000000CUL; /* Clear GPIO/DEBUG (bit 2) + GPIO/TRACE (bit 3) */
    IO1DIR |= BUZZER_PIN;     /* Buzzer pin as output */
    IO1CLR  = BUZZER_PIN;     /* Start with the buzzer off */
}

/* Turn the buzzer on (continuous tone). */
void buzzer_on(void)
{
    IO1SET = BUZZER_PIN;
}

/* Turn the buzzer off. */
void buzzer_off(void)
{
    IO1CLR = BUZZER_PIN;
}

/* Sound the buzzer in a simple on/off pattern, repeated 'cycles' times,
 * with a 300 ms on / 300 ms off period. Used to signal wrong passwords or
 * a detected tamper condition. */
void buzzer_alert(u8 cycles)
{
    u8 i;
    for (i = 0; i < cycles; i++)
    {
        buzzer_on();
        delay_ms(300);
        buzzer_off();
        delay_ms(300);
    }
}
