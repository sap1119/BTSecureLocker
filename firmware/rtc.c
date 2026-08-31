/*=============================================================================
 * File        : rtc.c
 * Project     : Bluetooth-Based Secure Locker with Access Logging
 * Description : RTC front-end for this project. The LPC2148's on-chip RTC is
 *               the "working copy" every reader uses (menu, alarm, audit log,
 *               idle clock display) - it is clocked from PCLK via the
 *               prescaler below and is read tear-free through CTIME0/CTIME1.
 *
 *               OPTIONALLY (RTC_EXT_ENABLED, see defines.h) a battery-backed
 *               external RTC (DS1307 or DS3231, I2C address 0xD0, crystal
 *               driven, powered by the coin cell on the vector board) is
 *               probed at boot. If one is found its real time is loaded into
 *               the on-chip RTC and every clock write is mirrored back to it,
 *               so the time genuinely SURVIVES a full power-off. If none is
 *               fitted, the on-chip RTC is the only clock and it freezes while
 *               the board is unpowered - an unavoidable limit of this chip.
 *
 * CCR (Clock Control Register) values used:
 *   CCR = 0x02 -> CCALEN + reset: clears/resets the RTC clock divider
 *   CCR = 0x01 -> CLKEN : enable the RTC to start counting
 *===========================================================================*/
#include <lpc214x.h>
#include "rtc.h"
#include "defines.h"
#include "delay.h"

/*=============================================================================
 * Battery-backed external RTC (DS1307 / DS3231)
 *=============================================================================
 * WHY IT EXISTS: the LPC2148 has no RTCX1/RTCX2 crystal pins and no VBAT pin -
 * its RTC is PCLK-derived, so it cannot tick through a power-off. The
 * DS1307/DS3231 is a tiny I2C clock with its own 32.768 kHz crystal and a VBAT
 * input for a coin cell; it keeps counting while the main board is unpowered.
 * Both parts share the same time/date register map, so one driver serves both.
 *
 * I2C WRITE:  START, device addr (0xD0), register pointer, data byte, STOP.
 * I2C READ:   START, 0xD0, register pointer, repeated-START, 0xD1, read N
 *             bytes (ACK all but the last), STOP.
 * REGISTERS:  0x00 sec (bit7 = clock-halt/osc-stop), 0x01 min, 0x02 hour
 *             (bit6 = 12/24 mode), 0x03 day-of-week (1=SUN..7=SAT),
 *             0x04 date, 0x05 month (bit7 = century), 0x06 year (00-99).
 *             All values are BCD.
 *
 * The I2C primitives below follow the exact pattern of the EEPROM driver in
 * eeprom.c (same register names, same bounded SI/STO waits, same sticky fault
 * flag) because they share the I2C0 bus. Every wait is bounded - no wait in
 * this firmware is unbounded.
 *===========================================================================*/

#if RTC_EXT_ENABLED

#define RTC_EXT_REG_SEC   0x00U
#define RTC_EXT_REG_MIN   0x01U
#define RTC_EXT_REG_HOUR  0x02U
#define RTC_EXT_REG_DOW   0x03U
#define RTC_EXT_REG_DATE  0x04U
#define RTC_EXT_REG_MONTH 0x05U
#define RTC_EXT_REG_YEAR  0x06U

/* Upper bound on how many times any external-RTC I2C primitive will spin
 * waiting for the SI flag before declaring the bus dead (same philosophy and
 * value as I2C_WAIT_LIMIT in eeprom.c). */
#define RTC_EXT_I2C_LIMIT 200000UL

/* Sticky flag: set whenever an external-RTC I2C operation times out. Used to
 * detect a bus that died after boot; a failed mirror-write simply leaves the
 * battery-backed copy stale (the on-chip RTC keeps ticking). */
static volatile u8 rtc_ext_fault = 0;

/* 1 once a battery-backed external RTC has been found and its time loaded.
 * This is what rtc_battery_backed() reports and what gates the write-mirror. */
static u8 rtc_battery = 0;

/* Wait (with a bound) for the I2C SI flag; record a timeout in rtc_ext_fault. */
static u8 rtc_i2c_wait_si(void)
{
    u32 guard = RTC_EXT_I2C_LIMIT;

    while (!(I2C0CONSET & 0x08))
    {
        if (--guard == 0UL)
        {
            rtc_ext_fault = 1;
            return 0;
        }
    }
    return 1;
}

/* Issue an I2C START condition and wait for it to be signalled complete. */
static void rtc_i2c_start(void)
{
    I2C0CONSET = 0x20;   /* Set STA (request START)     */
    I2C0CONCLR = 0x08;   /* Clear SI (interrupt flag) before waiting */
    rtc_i2c_wait_si();
}

/* Issue an I2C STOP condition and wait for the hardware to clear STO, so a
 * bus held low by a fault cannot hang here either. */
static void rtc_i2c_stop(void)
{
    u32 guard = RTC_EXT_I2C_LIMIT;

    I2C0CONSET = 0x10;   /* Set STO (request STOP) */
    I2C0CONCLR = 0x08;   /* Clear SI               */

    while (I2C0CONSET & 0x10)     /* Hardware clears STO when the STOP is out */
    {
        if (--guard == 0UL)
        {
            rtc_ext_fault = 1;    /* Bus stuck low - record it and move on */
            break;
        }
    }

    delay_us(20);         /* Bus free time before the next transfer may start */
}

/* Write one byte (address byte or data byte) and wait for the transfer. */
static void rtc_i2c_write_byte(u8 data)
{
    I2C0DAT = data;
    I2C0CONCLR = 0x28;   /* Clear STA and SI                     */
    rtc_i2c_wait_si();
}

/* Read one byte. ack = 1 -> ACK (more to follow), 0 -> NACK (last byte). */
static u8 rtc_i2c_read_byte(u8 ack)
{
    if (ack) I2C0CONSET = 0x04;    /* AA = 1 -> ACK after this byte  */
    else     I2C0CONCLR = 0x04;    /* AA = 0 -> NACK after this byte */

    I2C0CONCLR = 0x08;              /* Clear SI                        */
    rtc_i2c_wait_si();               /* Wait for the byte to arrive     */
    return I2C0DAT;
}

/* Binary-coded-decimal <-> plain decimal conversions. The DS1307/3231 store
 * every time field as BCD (e.g. 0x34 = 34, 0x12 = 12). */
static u8 rtc_bcd_to_bin(u8 bcd)
{
    return (u8)((((bcd >> 4) & 0x0FU) * 10U) + (bcd & 0x0FU));
}

static u8 rtc_bin_to_bcd(u8 bin)
{
    return (u8)(((bin / 10U) << 4) | (bin % 10U));
}

/* Presence check: does anything answer at the DS1307/DS3231 I2C address?
 * Status 0x18 means the address byte was transmitted and acknowledged. */
static u8 rtc_ext_probe(void)
{
    u8 acked;

    rtc_ext_fault = 0;

    rtc_i2c_start();
    if (rtc_ext_fault)
    {
        rtc_i2c_stop();
        return 0;
    }

    rtc_i2c_write_byte(RTC_EXT_ADDR);   /* Device address + write bit */
    if (rtc_ext_fault)
    {
        rtc_i2c_stop();
        return 0;
    }

    acked = ((I2C0STAT & 0xF8) == 0x18) ? 1 : 0;

    rtc_i2c_stop();
    return acked;
}

/* Read one coherent snapshot of the time/date from the external RTC.
 *
 * The DS1307/3231 do NOT latch a block read, so a 1-second tick can land
 * mid-read and tear the snapshot. The seconds register is therefore re-read
 * afterwards and the whole block is retried (bounded) until two readings
 * agree. Registers come back BCD; the top bits (clock-halt / oscillator-stop,
 * 12/24-hour, century) are masked off, and the DS1307's weekday 1-7 is
 * converted to the LPC2148 convention 0-6.
 *
 * Returns 1 on success, 0 if the bus faulted. */
static u8 rtc_ext_read(rtc_time *t)
{
    u8 tries = 4U;
    u8 sec0;
    u8 sec1;
    u8 yr;

    do
    {
        /* Set the address pointer to the seconds register. */
        rtc_ext_fault = 0;
        rtc_i2c_start();
        rtc_i2c_write_byte(RTC_EXT_ADDR);        /* SLA+W            */
        rtc_i2c_write_byte(RTC_EXT_REG_SEC);     /* pointer -> 0x00  */
        rtc_i2c_stop();
        if (rtc_ext_fault)
            return 0;

        /* Read the 7-register block, low to high. */
        rtc_i2c_start();
        rtc_i2c_write_byte(RTC_EXT_ADDR | 0x01); /* SLA+R            */
        sec0    = (u8)(rtc_i2c_read_byte(1) & 0x7FU);  /* clear CH/OSF  */
        t->min  = rtc_i2c_read_byte(1);
        t->hour = rtc_i2c_read_byte(1);
        t->dow  = rtc_i2c_read_byte(1);
        t->dom  = rtc_i2c_read_byte(1);
        t->month= rtc_i2c_read_byte(1);
        yr      = rtc_i2c_read_byte(0);          /* NACK the last byte */
        rtc_i2c_stop();
        if (rtc_ext_fault)
            return 0;

        /* Did a seconds tick happen during the block read? */
        rtc_ext_fault = 0;
        rtc_i2c_start();
        rtc_i2c_write_byte(RTC_EXT_ADDR);
        rtc_i2c_write_byte(RTC_EXT_REG_SEC);
        rtc_i2c_stop();
        rtc_i2c_start();
        rtc_i2c_write_byte(RTC_EXT_ADDR | 0x01);
        sec1 = (u8)(rtc_i2c_read_byte(0) & 0x7FU);
        rtc_i2c_stop();
        if (rtc_ext_fault)
            return 0;

        tries--;
    } while ((sec0 != sec1) && (tries > 0U));

    t->sec   = rtc_bcd_to_bin(sec0);
    t->min   = rtc_bcd_to_bin((u8)(t->min   & 0x7FU));
    t->hour  = rtc_bcd_to_bin((u8)(t->hour  & 0x3FU)); /* 24-hour mode */
    t->dow   = rtc_bcd_to_bin((u8)(t->dow   & 0x07U));
    t->dom   = rtc_bcd_to_bin((u8)(t->dom   & 0x3FU));
    t->month = rtc_bcd_to_bin((u8)(t->month & 0x1FU)); /* clear century bit */
    t->year  = (u16)(2000U + rtc_bcd_to_bin(yr));       /* DS1307 stores year 00-99 */

    /* DS1307/3231 weekday 1=SUN..7=SAT -> LPC2148 0=SUN..6=SAT. */
    t->dow = (u8)((t->dow >= 1U) ? (t->dow - 1U) : 0U);

    return 1;
}

/* Write a single register byte on the external RTC. */
static void rtc_ext_write_byte(u8 reg, u8 value)
{
    rtc_ext_fault = 0;
    rtc_i2c_start();
    rtc_i2c_write_byte(RTC_EXT_ADDR);
    rtc_i2c_write_byte(reg);
    rtc_i2c_write_byte(value);
    rtc_i2c_stop();
}

/* Mirror the time fields. Bit7 of seconds is cleared on write so the
 * oscillator keeps running (DS1307: clock-halt off; DS3231: clears OSF). */
static void rtc_ext_set_time(u8 hh, u8 mm, u8 ss)
{
    rtc_ext_write_byte(RTC_EXT_REG_SEC,  (u8)(rtc_bin_to_bcd(ss) & 0x7FU));
    rtc_ext_write_byte(RTC_EXT_REG_MIN,  (u8)(rtc_bin_to_bcd(mm) & 0x7FU));
    rtc_ext_write_byte(RTC_EXT_REG_HOUR, (u8)(rtc_bin_to_bcd(hh) & 0x3FU));
}

/* Mirror the date fields. Bit7 of month (the century flag) is cleared. */
static void rtc_ext_set_date(u8 dd, u8 mon, u16 yy)
{
    rtc_ext_write_byte(RTC_EXT_REG_DATE,  (u8)(rtc_bin_to_bcd(dd)  & 0x3FU));
    rtc_ext_write_byte(RTC_EXT_REG_MONTH, (u8)(rtc_bin_to_bcd(mon) & 0x1FU));
    rtc_ext_write_byte(RTC_EXT_REG_YEAR,  rtc_bin_to_bcd((u8)(yy % 100U)));
}

/* Mirror the day-of-week field (converting LPC2148 0-6 back to DS1307 1-7). */
static void rtc_ext_set_dow(u8 dow)
{
    rtc_ext_write_byte(RTC_EXT_REG_DOW, (u8)(rtc_bin_to_bcd((u8)(dow + 1U)) & 0x07U));
}

/* Sanity-check a snapshot that came back from the external RTC. A dead coin
 * cell or a never-set chip can leave the registers all-zero or holding garbage
 * BCD; restoring that into the on-chip RTC would be worse than a default. */
static u8 rtc_time_plausible(const rtc_time *t)
{
    if (t->sec   > 59U)  return 0;
    if (t->min   > 59U)  return 0;
    if (t->hour  > 23U)  return 0;
    if (t->dow   > 6U)   return 0;
    if (t->dom   < 1U || t->dom   > 31U) return 0;
    if (t->month < 1U || t->month > 12U) return 0;
    if (t->year  < 2000U || t->year > 2099U) return 0;
    return 1;
}

#endif /* RTC_EXT_ENABLED */

/* Has the on-chip RTC been completely unset (i.e. fresh silicon or the first
 * power-up after a full power-off with no battery)?
 *
 * The 4-digit YEAR is the discriminator: it is 0 only when the RTC has never
 * been provisioned. A cold power-up starts the RTC counting from 0, and no
 * valid time ever has year 0 - the boot default writes 2024, and the admin
 * menu and the external-RTC restore both write a real 4-digit year. The older
 * check demanded EVERY field be zero, which is fragile: rtc_init() runs within
 * milliseconds of the clock being enabled so it happened to work, but if a
 * future boot path ever delayed this check past the first 1-second tick, sec
 * would be 1 and a meaningless RTC would look "set". */
static u8 rtc_onchip_unset(void)
{
    rtc_time now;

    rtc_get(&now);
    return (u8)(now.year == 0U);
}

/* Reset the RTC's clock divider, program the prescaler (PREINT/PREFRAC)
 * so a 1-second tick is generated from PCLK, then enable the RTC. */
void rtc_init(void)
{
    CCR = 0x02;          /* Reset the RTC's internal prescaler/divider */
    PREINT  = 456;        /* Prescaler integer part                     */
    PREFRAC = 25024;      /* Prescaler fractional part                  */
    CCR = 0x01;           /* Enable (start) the RTC                     */

#if RTC_EXT_ENABLED
    /* Try to recover the real time from a battery-backed external RTC
     * (DS1307/DS3231 on I2C0, crystal driven, powered by the coin cell). If
     * one answers with a plausible time it is loaded into the on-chip RTC so
     * every existing reader (menu, alarm, audit log, idle display) keeps
     * working unchanged, and the system is flagged as battery-backed.
     *
     * If none answers, this LPC2148's on-chip RTC is PCLK-derived and has no
     * VBAT domain, so it FREEZES during a power-off no matter what; the
     * default below then stands in until the admin sets the clock. */
    rtc_battery = 0;
    if (rtc_ext_probe())
    {
        rtc_time ext;
        if (rtc_ext_read(&ext) && rtc_time_plausible(&ext))
        {
            /* The setters mirror back to the external RTC, which is a
             * harmless re-write of the very values just read. */
            rtc_set_time(ext.hour, ext.min, ext.sec);
            rtc_set_date(ext.dom,  ext.month, ext.year);
            rtc_set_dow(ext.dow);
            rtc_battery = 1;
            return;   /* real time restored - do NOT apply the default below */
        }
        /* External RTC present but unset/corrupt: fall through to the default. */
    }
#endif

    /* No usable battery-backed time: apply a fixed default ONLY if the on-chip
     * RTC is completely unset. If it already holds a time from this power
     * session (a reset, a re-flash, a debugger restart) it is left alone. This
     * used to be an unconditional "reset to 12:00:00 on EVERY boot", which is
     * exactly why the clock always came back at 12:00 PM. */
    if (rtc_onchip_unset())
    {
        rtc_set_date(1, 1, 2024);
        rtc_set_time(12, 0, 0);
    }
}

/* Returns 1 if a battery-backed external RTC was found at boot and is being
 * kept in step, 0 if the on-chip RTC is the only clock. */
u8 rtc_battery_backed(void)
{
    return rtc_battery;
}

/* Set the current time. The RTC clock is briefly disabled while the
 * HOUR/MIN/SEC registers are written, then re-enabled, to avoid a
 * rollover mid-update. */
void rtc_set_time(u8 hh, u8 mm, u8 ss)
{
    CCR &= ~0x01;   /* Pause the RTC while updating */
    HOUR = hh;
    MIN  = mm;
    SEC  = ss;
    CCR |= 0x01;    /* Resume the RTC */

#if RTC_EXT_ENABLED
    if (rtc_battery)
        rtc_ext_set_time(hh, mm, ss);   /* keep the battery-backed copy in step */
#endif
}

/* Set the current date (day of month, month, 4-digit year). Same
 * pause/update/resume pattern as rtc_set_time(). */
void rtc_set_date(u8 dd, u8 mon, u16 yy)
{
    CCR &= ~0x01;
    DOM   = dd;
    MONTH = mon;
    YEAR  = yy;
    CCR |= 0x01;

#if RTC_EXT_ENABLED
    if (rtc_battery)
        rtc_ext_set_date(dd, mon, yy);
#endif
}

/* Set the day-of-week counter (0 = SUN .. 6 = SAT). The LPC2148 RTC does not
 * derive DOW from the date, so it is a separate field the admin can set.
 * Uses the same pause/update/resume pattern as the two setters above - the
 * admin menu used to write DOW (and HOUR/MIN/SEC/DOM/MONTH/YEAR) directly with
 * the clock still running, which can race with a rollover happening in the
 * same instant. */
void rtc_set_dow(u8 dow)
{
    CCR &= ~0x01;
    DOW = dow;
    CCR |= 0x01;

#if RTC_EXT_ENABLED
    if (rtc_battery)
        rtc_ext_set_dow(dow);
#endif
}

/* Read one coherent snapshot of the date and time. See the tearing
 * explanation in rtc.h.
 *
 * CTIME0 and CTIME1 are the RTC's "consolidated" registers: each packs
 * several calendar fields into a single 32-bit word that the hardware updates
 * atomically, so one read of CTIME0 can never contain a half-updated
 * time. Two reads are still needed to cover both words, and the RTC could
 * tick between them (giving today's time with yesterday's date at midnight),
 * so CTIME0 is re-read afterwards and the pair is taken again if it changed.
 *
 * The retry is BOUNDED: the RTC only advances once per second, so a second
 * pass always succeeds in practice, and the loop gives up after a few tries
 * rather than spinning - no wait anywhere in this firmware is unbounded.
 *
 * CTIME0 bit layout:  [5:0] seconds, [13:8] minutes, [20:16] hours,
 *                     [26:24] day of week
 * CTIME1 bit layout:  [4:0] day of month, [11:8] month, [27:16] year */
void rtc_get(rtc_time *t)
{
    u32 c0;
    u32 c1;
    u8  tries = 4U;

    if (t == 0)
        return;

    do
    {
        c0 = CTIME0;
        c1 = CTIME1;
        tries--;
    } while ((c0 != CTIME0) && (tries > 0U));

    t->sec   = (u8) ( c0        & 0x3FUL);
    t->min   = (u8) ((c0 >> 8)  & 0x3FUL);
    t->hour  = (u8) ((c0 >> 16) & 0x1FUL);
    t->dow   = (u8) ((c0 >> 24) & 0x07UL);

    t->dom   = (u8) ( c1        & 0x1FUL);
    t->month = (u8) ((c1 >> 8)  & 0x0FUL);
    t->year  = (u16)((c1 >> 16) & 0x0FFFUL);
}

/* Build a fixed-width, human-readable timestamp string in the form
 * "DD/MM/YYYY HH:MM:SS\0" (20 bytes, including the null terminator) by
 * converting each field of one tear-free rtc_get() snapshot to ASCII digits
 * directly (no sprintf, to keep the code small and fast). */
void rtc_get_stamp(char *buf)
{
    rtc_time now;

    rtc_get(&now);

    buf[0]  = (now.dom / 10) + '0';           /* Day, tens digit    */
    buf[1]  = (now.dom % 10) + '0';           /* Day, units digit    */
    buf[2]  = '/';
    buf[3]  = (now.month / 10) + '0';         /* Month, tens digit   */
    buf[4]  = (now.month % 10) + '0';         /* Month, units digit  */
    buf[5]  = '/';
    buf[6]  = (char)((now.year / 1000) + '0');       /* Year, thousands */
    buf[7]  = (char)(((now.year / 100) % 10) + '0'); /* Year, hundreds  */
    buf[8]  = (char)(((now.year / 10) % 10) + '0');  /* Year, tens      */
    buf[9]  = (char)((now.year % 10) + '0');         /* Year, units     */
    buf[10] = ' ';
    buf[11] = (now.hour / 10) + '0';          /* Hour, tens digit    */
    buf[12] = (now.hour % 10) + '0';          /* Hour, units digit   */
    buf[13] = ':';
    buf[14] = (now.min / 10) + '0';           /* Minute, tens digit  */
    buf[15] = (now.min % 10) + '0';           /* Minute, units digit */
    buf[16] = ':';
    buf[17] = (now.sec / 10) + '0';           /* Second, tens digit  */
    buf[18] = (now.sec % 10) + '0';           /* Second, units digit */
    buf[19] = '\0';
}
