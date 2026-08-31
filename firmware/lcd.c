/*=============================================================================
 * File        : lcd.c
 * Project     : Bluetooth-Based Secure Locker with Access Logging
 * Description : Driver for a standard 16x2 character LCD (HD44780-compatible
 *               controller) operated in 4-bit mode, using 6 GPIO lines on
 *               Port 0 (see lcd_defines.h for the exact pin assignment).
 *
 * LCD Command reference used below:
 *   0x01 -> Clear display
 *   0x02 -> Return home / part of the 4-bit initialisation sequence
 *   0x06 -> Entry mode set: increment cursor position after each character
 *   0x0C -> Display ON, cursor OFF, blink OFF
 *   0x28 -> Function set: 4-bit interface, 2 display lines, 5x8 font
 *===========================================================================*/
#include <lpc214x.h>
#include "lcd.h"
#include "lcd_defines.h"
#include "delay.h"

/* Pulse the EN (Enable) line so the HD44780 controller latches whatever
 * nibble is currently present on D4-D7. A short delay before and after the
 * edge is required to satisfy the controller's timing requirements. */
static void lcd_enable_pulse(void)
{
    IO0SET = LCD_EN;     /* EN high  */
    delay_us(50);
    IO0CLR = LCD_EN;     /* EN low -> falling edge latches the data */
    delay_us(50);
}

/* Place a 4-bit nibble on D4-D7 and strobe EN to send it to the LCD.
 * Since only the lower 4 bits of 'nib' are meaningful, the data pins are
 * cleared first and then set according to the nibble value. */
static void lcd_send_nibble(u8 nib)
{
    IO0CLR = LCD_DATA_MSK;                 /* Clear D4-D7                */
    IO0SET = ((u32)(nib & 0x0F) << 18);    /* Set D4-D7 from low nibble  */
    lcd_enable_pulse();
}

/* Send a command byte to the LCD (RS = 0). Every byte is split into a
 * high nibble followed by a low nibble because the interface is 4-bit. */
void lcd_cmd(u8 cmd)
{
    IO0CLR = LCD_RS;                 /* RS = 0 -> command register       */
    lcd_send_nibble(cmd >> 4);       /* Send high nibble first           */
    lcd_send_nibble(cmd & 0x0F);     /* Then send low nibble              */
    delay_ms(2);                     /* Allow the controller time to process */
}

/* Send a data byte (a displayable character) to the LCD (RS = 1). */
void lcd_data(u8 data)
{
    IO0SET = LCD_RS;                 /* RS = 1 -> data register          */
    lcd_send_nibble(data >> 4);      /* High nibble                       */
    lcd_send_nibble(data & 0x0F);    /* Low nibble                        */
    delay_ms(2);
}

/* One-time LCD setup: configure the 6 control/data pins as outputs and
 * run the standard HD44780 4-bit-mode initialisation sequence. */
void lcd_init(void)
{
    PINSEL1 &= ~(0x00000FFF);                       /* P0.16-P0.21 as GPIO (not alt. function) */
    IO0DIR |= (LCD_RS | LCD_EN | LCD_DATA_MSK);      /* RS, EN, D4-D7 as outputs */

    delay_ms(20);       /* Wait for LCD power-on reset to complete */
    lcd_cmd(0x02);       /* Return home / select 4-bit mode          */
    lcd_cmd(0x28);       /* 4-bit interface, 2 lines, 5x8 font        */
    lcd_cmd(0x0C);       /* Display ON, cursor OFF, blink OFF          */
    lcd_cmd(0x06);       /* Auto-increment cursor after each character */
    lcd_cmd(0x01);       /* Clear display                               */
    delay_ms(2);
}

/* Clear the whole display and return the cursor to the home position. */
void lcd_clear(void)
{
    lcd_cmd(0x01);
    delay_ms(2);
}

/* Print each character of a null-terminated string starting at the
 * current cursor position. */
void lcd_string(const char *str)
{
    while (*str)
        lcd_data(*str++);
}

/* Move the cursor to a given row/column.
 * row = 0 -> first line  (DDRAM address 0x00 + col)
 * row = 1 -> second line (DDRAM address 0x40 + col) */
void lcd_gotoxy(u8 row, u8 col)
{
    if (row == 0) lcd_cmd(0x80 + col);
    else          lcd_cmd(0xC0 + col);
}

/* Print a signed decimal integer by converting it to characters one digit
 * at a time (least-significant digit first into a temporary buffer, then
 * printed back out in the correct order). */
void lcd_int(s32 num)
{
    char buf[12];
    s32 i = 0;

    if (num == 0)
    {
        lcd_data('0');
        return;
    }

    if (num < 0)
    {
        lcd_data('-');   /* Print the minus sign, then continue with the magnitude */
        num = -num;
    }

    while (num > 0)
    {
        buf[i++] = (num % 10) + '0';   /* Extract digits, least-significant first */
        num /= 10;
    }

    while (i > 0)
        lcd_data(buf[--i]);            /* Print digits back in the correct order   */
}

/* Print an unsigned value right-aligned in a 'width'-character field, padded
 * with leading spaces. See the rationale in lcd.h: this is what keeps the
 * live seconds countdowns pinned to the same LCD columns as they shrink from
 * three digits to two to one.
 *
 * Digits are extracted least-significant-first into a small local buffer, the
 * padding spaces are emitted first, and then the digits are printed back in
 * the correct order. If the value needs more than 'width' characters it is
 * printed in full - never truncated, since a truncated countdown would show a
 * wrong (smaller) number of seconds. */
void lcd_uint_pad(u32 val, u8 width)
{
    char buf[11];      /* A 32-bit value is at most 10 decimal digits */
    u8   n = 0;

    do
    {
        buf[n++] = (char)((val % 10U) + '0');
        val /= 10U;
    } while ((val != 0U) && (n < 10U));

    while (width > n)  /* Leading padding, so the field width is constant */
    {
        lcd_data(' ');
        width--;
    }

    while (n > 0U)
        lcd_data((u8)buf[--n]);
}
