/*=============================================================================
 * File        : lcd_defines.h
 * Project     : Bluetooth-Based Secure Locker with Access Logging
 * Description : Bit-mask definitions for the 16x2 character LCD, which is
 *               driven in 4-bit mode using 6 GPIO lines on Port 0.
 *
 * LCD Pins (all on GPIO Port 0):
 *   RS (Register Select) -> P0.16   (0 = command, 1 = data)
 *   EN (Enable / Strobe) -> P0.17   (rising+falling edge latches the nibble)
 *   D4 (Data bit 4)      -> P0.18
 *   D5 (Data bit 5)      -> P0.19
 *   D6 (Data bit 6)      -> P0.20
 *   D7 (Data bit 7)      -> P0.21
 *   R/W                  -> tied to GND (always write mode, not driven by MCU)
 *===========================================================================*/
#ifndef LCD_DEFINES_H
#define LCD_DEFINES_H

#define LCD_RS        (1UL << 16)   /* P0.16 - Register Select pin mask   */
#define LCD_EN        (1UL << 17)   /* P0.17 - Enable/strobe pin mask     */
#define LCD_D4        (1UL << 18)   /* P0.18 - Data line D4               */
#define LCD_D5        (1UL << 19)   /* P0.19 - Data line D5               */
#define LCD_D6        (1UL << 20)   /* P0.20 - Data line D6               */
#define LCD_D7        (1UL << 21)   /* P0.21 - Data line D7               */
#define LCD_DATA_MSK  (0x0FUL << 18) /* Combined mask for D4-D7 (4 data bits) */

#endif
