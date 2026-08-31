/*=============================================================================
 * File        : lcd.h
 * Project     : Bluetooth-Based Secure Locker with Access Logging
 * Description : Public API for the 16x2 character LCD driver (4-bit mode).
 *===========================================================================*/
#ifndef LCD_H
#define LCD_H

#include "types.h"

void lcd_init(void);              /* Configure GPIO and initialise the LCD controller */
void lcd_cmd(u8 cmd);             /* Send a raw command byte (RS = 0)                 */
void lcd_data(u8 data);           /* Send a raw data/character byte (RS = 1)          */
void lcd_string(const char *str); /* Print a null-terminated string                   */
void lcd_clear(void);             /* Clear the display and home the cursor            */
void lcd_gotoxy(u8 row, u8 col);  /* Move the cursor to (row, col); row: 0 or 1        */
void lcd_int(s32 num);            /* Print a signed integer in decimal                */

/* Print an unsigned value right-aligned in a fixed-width field, padded with
 * leading SPACES (not zeros).
 *
 * This exists for the live seconds countdowns (Level-2 entry, admin password
 * fields, lockout). A countdown must always occupy the same columns, or going
 * from "100" to "99" would leave the old '0' behind and display "990". Padding
 * with spaces rather than zeros keeps it readable as a number of seconds.
 *
 * A value too wide for 'width' is printed in full rather than truncated - a
 * countdown must never show a misleadingly small number. */
void lcd_uint_pad(u32 val, u8 width);

#endif
