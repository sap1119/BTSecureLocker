# Register-Level Reference

Every peripheral the SecureLocker firmware touches, documented at register
level. Each page follows the same shape:

1. **Peripheral intro** — what the peripheral is and how this project uses it.
2. **Registers used** — name, address, bit layout, the exact value the firmware
   writes, and *why* that value.
3. **The firmware's sequence** — the order of writes/reads, with the code.
4. **Hardware consequences** — what is physically true because of those values.

All values below were read out of the actual source (`firmware/*.c`), not
guessed. Addresses are from the [LPC2148 User Manual](https://www.nxp.com/docs/en/user-guide/UM10120.pdf).

## How to read a register entry

```
PINSEL0   0xE002C000   Pin function select, Port 0
Bits [3:2] P0.1   function      | 0b01 = RXD0 (UART0 receive)
Bits [1:0] P0.0   function      | 0b01 = TXD0 (UART0 transmit)
                 firmware: PINSEL0 |= 0x00000005  → P0.0 = TXD0, P0.1 = RXD0
```

*The **bit range**, the **field's meaning**, the **binary value** the firmware
programs, and the **source line** (file + value) are all in one place.*

---

## The pages

| Page | Covers | Why it matters |
|---|---|---|
| [clocking.md](clocking.md) | PLL0, VPBDIV, MAM | Turns the 12 MHz crystal into a 60 MHz core / 15 MHz peripheral clock |
| [gpio.md](gpio.md) | PINSEL0/1/2, IO0/IO1 | Every pin: LCD, keypad, motor, buzzer, tamper, admin, I²C, UARTs |
| [uart.md](uart.md) | U0/U1 LCR·DLL·DLM·FCR·LSR·MCR·IER | Audit log (9600) + Bluetooth receive (9600) |
| [i2c.md](i2c.md) | I2C0 CONSET·CONCLR·DAT·STAT·SCLL·SCLH | AT24C256 EEPROM + DS1307/DS3231 external RTC |
| [timers.md](timers.md) | T0/T1 CTCR·PR·TC·TCR | `delay_ms/us` (Timer0) + the `millis()` time base (Timer1) |
| [rtc.md](rtc.md) | CCR·PREINT·PREFRAC·CTIME0/1, time regs | The wall clock, tear-free reads, and the external battery-backed RTC |
| [interrupts.md](interrupts.md) | VIC, EINT2, EXTINT·EXTMODE·EXTPOLAR | UART1 receive ISR + the admin-button EINT2 ISR |
| [memory-map.md](memory-map.md) | On-chip flash/RAM + EEPROM layout | Where the code runs and where the passwords live |

> **Related:** the behaviour-level docs ([`../firmware/`](../firmware/README.md))
> explain *what the system does*; these pages explain *exactly which registers
> make that happen*.

## Load-bearing values at a glance

| Register | Value | Effect |
|---|---|---|
| `PLL0CFG` | `0x24` | CCLK = 12 MHz × 6 = 60 MHz |
| `VPBDIV` | `0x00` | PCLK = 60/4 = 15 MHz |
| `PREINT` / `PREFRAC` | `456` / `25024` | On-chip RTC ticks exactly 1/s |
| `T0PR` / `T1PR` | `14999` | Timers tick exactly 1/ms |
| `UxDLL`/`UxDLM` | `15000000/(16·9600)` | UART baud = 9600 |
| `I2C0SCLH`/`I2C0SCLL` | `75` / `75` | I²C SCL ≈ 100 kHz |
| `PINSEL2` | `&= ~0x0C` | P1.16–31 = GPIO (JTAG/Trace off) |
