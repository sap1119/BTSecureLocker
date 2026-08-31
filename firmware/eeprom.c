/*=============================================================================
 * File        : eeprom.c
 * Project     : Bluetooth-Based Secure Locker with Access Logging
 * Description : Byte-level read/write driver for an AT24C256 serial EEPROM
 *               (32 KB, 2-byte internal addressing) connected over I2C0.
 *               Used to store the Level-1/Level-2 passwords persistently
 *               so they survive a power cycle.
 *
 * Wiring:
 *   SCL0 -> P0.2   (I2C0 clock)
 *   SDA0 -> P0.3   (I2C0 data)
 *   EEPROM A0/A1/A2 -> GND (address pins tied low, giving device ID 0xA0)
 *   EEPROM VCC -> 3V3, GND -> GND, WP -> GND (write protect disabled)
 *
 * EEPROM_ID (0xA0) is the AT24Cxx 7-bit I2C slave address (1010 000)
 * already shifted left by one bit, with bit 0 used for read(1)/write(0).
 *===========================================================================*/
#include <lpc214x.h>
#include "eeprom.h"
#include "defines.h"
#include "delay.h"

#define EEPROM_ID   0xA0   /* AT24C256 I2C address, all address pins = 0 */

/* Upper bound on how many times any I2C primitive will spin waiting for the
 * SI (interrupt/"operation complete") flag before declaring the bus dead.
 *
 * WHY A BOUND IS ESSENTIAL: every wait in this driver used to be an
 * unconditional `while (!(I2C0CONSET & 0x08));`. If the EEPROM is missing but
 * the bus still has its pull-up resistors, SI does get set (with a
 * "no acknowledge" status) so those loops happened to exit - but if SDA or
 * SCL is shorted to ground, left floating with no pull-ups, or the module is
 * wired to the wrong pins, SCL can never rise, SI is never set, and the
 * firmware hangs forever at boot with a blank LCD and no diagnostic at all.
 * That is exactly the "not connected properly" case the boot self-test has
 * to be able to report, so the wait must be able to give up.
 *
 * One I2C byte at ~100 kHz takes ~90 us; this many iterations of a tight
 * ARM7 load/test loop at 60 MHz is several milliseconds, so it is far longer
 * than any legitimate transfer yet still imperceptible at boot. */
#define I2C_WAIT_LIMIT   200000UL

/* Sticky flag: set whenever an I2C operation times out. Cleared at the start
 * of each self-test so a stale failure is never reported. Callers that care
 * check it via eeprom_bus_fault(). */
static volatile u8 i2c_fault = 0;

/* Wait (with a bound) for the I2C SI flag. Returns 1 if the operation
 * completed, 0 on timeout - and records the timeout in i2c_fault. */
static u8 i2c_wait_si(void)
{
    u32 guard = I2C_WAIT_LIMIT;

    while (!(I2C0CONSET & 0x08))
    {
        if (--guard == 0UL)
        {
            i2c_fault = 1;
            return 0;
        }
    }
    return 1;
}

/* Configure P0.2/P0.3 for their I2C0 alternate function and set up the
 * I2C0 peripheral (clock rate, control flags) for master mode. */
void i2c_init(void)
{
    PINSEL0 &= ~((3UL << 4) | (3UL << 6));  /* Clear P0.2/P0.3 function bits */
    PINSEL0 |=  ((1UL << 4) | (1UL << 6));  /* Select SCL0 (P0.2) / SDA0 (P0.3) */

    I2C0CONCLR = 0x6C;   /* Clear STA, STO, SI and I2EN before reconfiguring */
    I2C0SCLH = 75;        /* High-period duty cycle count for SCL             */
    I2C0SCLL = 75;        /* Low-period duty cycle count for SCL (~100 kHz with PCLK=15MHz) */
    I2C0CONSET = 0x40;   /* Enable the I2C0 interface (I2EN)                  */

    i2c_fault = 0;
}

/* Returns 1 if any I2C operation has timed out since the flag was last
 * cleared (i.e. the bus is stuck / the device is not wired correctly). */
u8 eeprom_bus_fault(void)
{
    return i2c_fault;
}

/* Clear the sticky bus-fault flag.
 *
 * Callers use this to make a fault reading refer to one specific operation:
 * clear it, do the EEPROM access, then test eeprom_bus_fault(). The main loop
 * does exactly that around each password read, so an EEPROM that dies while
 * the locker is running is reported as a hardware fault instead of silently
 * returning garbage that can never match a password - the same failure the
 * boot self-test exists to catch, but happening later. */
void eeprom_clear_fault(void)
{
    i2c_fault = 0;
}

/* Issue an I2C START condition and wait for it to be signalled complete. */
static void i2c_start(void)
{
    I2C0CONSET = 0x20;   /* Set STA (request START)     */
    I2C0CONCLR = 0x08;   /* Clear SI (interrupt flag) before waiting */
    i2c_wait_si();        /* Wait for SI to be set (START sent), with timeout */
}

/* Issue an I2C STOP condition, releasing the bus.
 *
 * The hardware clears STO by itself once the STOP condition has actually been
 * driven onto the bus, so that is what is waited for - bounded by the same
 * I2C_WAIT_LIMIT as every other wait in this driver, so a bus held low by a
 * fault cannot hang here either. The old code just waited a fixed 20 us and
 * assumed the STOP had completed, which is a guess rather than a check. */
static void i2c_stop(void)
{
    u32 guard = I2C_WAIT_LIMIT;

    I2C0CONSET = 0x10;   /* Set STO (request STOP) */
    I2C0CONCLR = 0x08;   /* Clear SI               */

    while (I2C0CONSET & 0x10)     /* Hardware clears STO when the STOP is out */
    {
        if (--guard == 0UL)
        {
            i2c_fault = 1;         /* Bus stuck low - record it and move on */
            break;
        }
    }

    delay_us(20);         /* Bus free time before the next transfer may start */
}

/* Write one byte onto the I2C bus (address byte or data byte) and wait
 * for the transfer to complete. */
static void i2c_write(u8 data)
{
    I2C0DAT = data;
    I2C0CONCLR = 0x28;   /* Clear STA and SI                     */
    i2c_wait_si();        /* Wait for SI (byte transferred), with timeout */
}

/* Read one byte from the I2C bus.
 * ack = 1 -> send ACK  (more bytes to follow)
 * ack = 0 -> send NACK (this is the last byte being read) */
static u8 i2c_read(u8 ack)
{
    if (ack) I2C0CONSET = 0x04;    /* AA = 1 -> ACK after this byte  */
    else     I2C0CONCLR = 0x04;    /* AA = 0 -> NACK after this byte */

    I2C0CONCLR = 0x08;              /* Clear SI                        */
    i2c_wait_si();                   /* Wait for the byte to arrive, with timeout */
    return I2C0DAT;
}

/* Write a single byte to a given 16-bit EEPROM address.
 * Sequence: START, device address (write), address high byte,
 * address low byte, data byte, STOP. A 10 ms delay follows to allow the
 * EEPROM's internal write cycle to complete before any further access. */
void eeprom_byte_write(u16 addr, u8 data)
{
    i2c_start();
    i2c_write(EEPROM_ID);          /* Device address + write bit (0) */
    i2c_write((addr >> 8) & 0xFF); /* EEPROM address high byte        */
    i2c_write(addr & 0xFF);        /* EEPROM address low byte         */
    i2c_write(data);               /* The byte to store                */
    i2c_stop();
    delay_ms(10);                  /* Wait for the internal write cycle */
}

/* Read a single byte from a given 16-bit EEPROM address.
 * Sequence: START, device address (write), address high/low bytes,
 * repeated-START, device address (read), read one byte with NACK, STOP. */
u8 eeprom_byte_read(u16 addr)
{
    u8 data;

    i2c_start();
    i2c_write(EEPROM_ID);           /* Device address + write bit, to set the address pointer */
    i2c_write((addr >> 8) & 0xFF);
    i2c_write(addr & 0xFF);

    i2c_start();                    /* Repeated START to switch to read mode */
    i2c_write(EEPROM_ID | 0x01);    /* Device address + read bit (1)          */
    data = i2c_read(0);             /* Read the byte, NACK (last/only byte)   */
    i2c_stop();

    return data;
}

/* Write 'len' consecutive bytes starting at 'addr' (simple byte-at-a-time
 * loop; sufficient for the short 4-digit passwords used in this project). */
void eeprom_write_str(u16 addr, const char *str, u8 len)
{
    u8 i;
    for (i = 0; i < len; i++)
        eeprom_byte_write(addr + i, str[i]);
}

/* Read 'len' consecutive bytes starting at 'addr' into buf, then
 * null-terminate the result so it can be used as a C string. */
void eeprom_read_str(u16 addr, char *buf, u8 len)
{
    u8 i;
    for (i = 0; i < len; i++)
        buf[i] = eeprom_byte_read(addr + i);
    buf[len] = '\0';
}

/*=============================================================================
 * BOOT SELF-TEST
 *===========================================================================*/

/* Bus-level presence check: is anything actually answering at the AT24C256's
 * I2C address?
 *
 * Sends a START followed by the device address with the write bit, then reads
 * the I2C status register. Status 0x18 means "address byte transmitted and the
 * slave acknowledged it" - i.e. a device is present and responding. Status
 * 0x20 means the address was sent but nobody acknowledged (no EEPROM, wrong
 * address pins, or unpowered), and a timeout means the bus itself is stuck.
 *
 * Returns 1 if the EEPROM acknowledged, 0 otherwise. */
u8 eeprom_probe(void)
{
    u8 acked;

    i2c_fault = 0;

    i2c_start();
    if (i2c_fault)          /* Bus stuck - SCL/SDA shorted or no pull-ups */
    {
        i2c_stop();
        return 0;
    }

    i2c_write(EEPROM_ID);   /* Device address + write bit */
    if (i2c_fault)
    {
        i2c_stop();
        return 0;
    }

    /* 0x18 = SLA+W transmitted, ACK received. Mask off the low 3 status
     * bits, which are not part of the state code. */
    acked = ((I2C0STAT & 0xF8) == 0x18) ? 1 : 0;

    i2c_stop();
    return acked;
}

/* Full EEPROM self-test: the device must be present AND actually able to
 * store and return data.
 *
 * A presence check alone is not enough. A device can acknowledge its address
 * yet still fail to retain anything - for example with WP (write protect)
 * tied high, or with SDA held by a bus fault so every read returns the same
 * fixed value. Since both locker passwords live in this EEPROM, silently
 * booting in that state means no password can ever match.
 *
 * The test writes two complementary patterns (0x5A / 0xA5) to a spare
 * scratch byte and reads each back. Using two patterns that are bitwise
 * inverses of one another is deliberate: a bus stuck permanently high (0xFF)
 * or low (0x00) can accidentally "pass" a single-pattern test, but cannot
 * return both 0x5A and 0xA5.
 *
 * The scratch byte lives at EEPROM_SCRATCH_ADDR, well clear of the magic
 * marker and both stored passwords, and its original contents are written
 * back afterwards, so the test is non-destructive.
 *
 * Returns 1 if the EEPROM is present and verified, 0 otherwise. */
u8 eeprom_selftest(void)
{
    u8 original;
    u8 readback_1;
    u8 readback_2;

    if (!eeprom_probe())
        return 0;

    i2c_fault = 0;

    original = eeprom_byte_read(EEPROM_SCRATCH_ADDR);

    eeprom_byte_write(EEPROM_SCRATCH_ADDR, 0x5A);
    readback_1 = eeprom_byte_read(EEPROM_SCRATCH_ADDR);

    eeprom_byte_write(EEPROM_SCRATCH_ADDR, 0xA5);
    readback_2 = eeprom_byte_read(EEPROM_SCRATCH_ADDR);

    eeprom_byte_write(EEPROM_SCRATCH_ADDR, original);   /* Leave no trace */

    if (i2c_fault)
        return 0;            /* Something timed out part-way through */

    return (u8)((readback_1 == 0x5A) && (readback_2 == 0xA5));
}
