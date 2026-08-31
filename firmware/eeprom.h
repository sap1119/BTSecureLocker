/*=============================================================================
 * File        : eeprom.h
 * Project     : Bluetooth-Based Secure Locker with Access Logging
 * Description : Public API for the I2C-based external EEPROM used as
 *               non-volatile password storage.
 *===========================================================================*/
#ifndef EEPROM_H
#define EEPROM_H

#include "types.h"

void i2c_init(void);                                            /* Configure I2C0 for talking to the EEPROM */
void eeprom_byte_write(u16 addr, u8 data);                       /* Write a single byte at 'addr'            */
u8   eeprom_byte_read(u16 addr);                                 /* Read a single byte from 'addr'           */
void eeprom_write_str(u16 addr, const char *str, u8 len);        /* Write 'len' bytes starting at 'addr'     */
void eeprom_read_str(u16 addr, char *buf, u8 len);               /* Read 'len' bytes starting at 'addr'      */

/*-------------- Boot self-test and runtime fault reporting ---------------
 * eeprom_probe()      - does anything acknowledge the AT24C256's I2C address?
 * eeprom_selftest()   - present AND verified able to store/return data, by
 *                       writing two complementary patterns to a spare scratch
 *                       byte and reading them back (non-destructive: the
 *                       original byte is restored). This is what the boot POST
 *                       in projectmain.c calls before the locker is allowed to
 *                       run, since both passwords are stored here.
 * eeprom_bus_fault()  - 1 if an I2C operation has timed out (bus stuck/
 *                       miswired) since the flag was last cleared. Sticky, so
 *                       a fault part-way through a multi-byte transfer is not
 *                       lost.
 * eeprom_clear_fault()- clear that flag, so the next reading applies to one
 *                       specific access. Clear it, do the access, then test:
 *                       that is how the main loop turns a mid-operation EEPROM
 *                       failure into a reported hardware fault instead of a
 *                       password that mysteriously never matches.
 * ------------------------------------------------------------------------ */
u8   eeprom_probe(void);
u8   eeprom_selftest(void);
u8   eeprom_bus_fault(void);
void eeprom_clear_fault(void);

#endif
