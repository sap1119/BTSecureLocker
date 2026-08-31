/*=============================================================================
 * File        : uart.h
 * Project     : Bluetooth-Based Secure Locker with Access Logging
 * Description : Public API for UART0 (PC/debug logging) and UART1 (basic
 *               polled transmit helpers; UART1 reception is handled
 *               separately via interrupt in bluetooth.c).
 *===========================================================================*/
#ifndef UART_H
#define UART_H

#include "types.h"

void uart0_init(u32 baud);            /* Initialise UART0 at the given baud rate */
void uart0_tx(u8 ch);                 /* Transmit a single byte on UART0 (blocking) */
void uart0_string(const char *str);   /* Transmit a null-terminated string on UART0 */
void uart0_int(s32 num);              /* Transmit a signed integer as decimal text on UART0 */

void uart1_init(u32 baud);            /* Initialise UART1 at the given baud rate */
void uart1_tx(u8 ch);                 /* Transmit a single byte on UART1 (blocking) */
void uart1_string(const char *str);   /* Transmit a null-terminated string on UART1 */

#endif
