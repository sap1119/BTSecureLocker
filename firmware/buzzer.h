/*=============================================================================
 * File        : buzzer.h
 * Project     : Bluetooth-Based Secure Locker with Access Logging
 * Description : Public API for the piezo/electronic buzzer used for audible
 *               access-denied and tamper alerts.
 *===========================================================================*/
#ifndef BUZZER_H
#define BUZZER_H

#include "types.h"

void buzzer_init(void);            /* Configure the buzzer GPIO pin as an output */
void buzzer_on(void);              /* Turn the buzzer on                          */
void buzzer_off(void);             /* Turn the buzzer off                         */
void buzzer_alert(u8 cycles);      /* Beep on/off repeatedly, 'cycles' times       */

#endif
