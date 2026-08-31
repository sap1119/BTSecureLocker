# I2C0 — the EEPROM and the External RTC on One Bus

## Peripheral intro

I2C0 carries **two devices** on the same two wires (`SCL0 = P0.2`, `SDA0 =
P0.3`):

| Device | Purpose | I2C address |
|---|---|---|
| AT24C256 EEPROM (32 KB) | Stores the Level-1/Level-2 passwords + magic marker | `0xA0` (write) / `0xA1` (read) |
| DS1307 / DS3231 (external, battery-backed RTC) | Keeps the time through power-off | `0xD0` (write) / `0xD1` (read) |

The EEPROM driver lives in `eeprom.c` and owns the bus configuration
(`i2c_init()`). The external-RTC driver in `rtc.c` uses **byte-identical I²C
primitives** (same register names, same bounded SI/STO waits, same sticky fault
flag) because both share the same I2C0 peripheral. Both parts use 7-bit slave
addresses already shifted left by one, with bit 0 = read(`1`)/write(`0`).

The bus runs at ≈**100 kHz**, set by two duty-cycle counters.

## Registers used (I2C0 base `0xE001C000`)

| Offset | Register | Name | Width |
|---|---|---|---|
| +0x00 | `I2C0CONSET` | Control Set | byte |
| +0x04 | `I2C0STAT` | Status (state code) | byte |
| +0x08 | `I2C0DAT` | Data | byte |
| +0x10 | `I2C0SCLH` | SCL high period | half-word |
| +0x14 | `I2C0SCLL` | SCL low period | half-word |
| +0x18 | `I2C0CONCLR` | Control Clear | byte |

### The control bits (used as *set* in CONSET, *clear* in CONCLR)

| Bit | Mask | Name | Meaning |
|---|---|---|---|
| 2 | `0x04` | **AA** | Assert Acknowledge flag (master ACKs received byte) |
| 3 | `0x08` | **SI** | Interrupt flag — "operation complete, read STAT" |
| 4 | `0x10` | **STO** | STOP condition request (hardware auto-clears it) |
| 5 | `0x20` | **STA** | START condition request |
| 6 | `0x40` | **I2EN** | I2C interface enable |

The hardware is a small state machine: after every operation it raises **SI**
and puts a **state code** in `I2C0STAT`. The firmware's one rule is
`i2c_wait_si()` — a **bounded** spin on `while (!(I2C0CONSET & 0x08))` guarded
by `I2C_WAIT_LIMIT` (200000). Every wait in the driver is bounded, so a bus
shorted low (SCL can never rise → SI never sets) cannot hang the boot.

### I2C0STAT — state codes the firmware actually tests

The code masks the low 3 bits (`I2C0STAT & 0xF8`) — only the upper 5 bits are
the state code.

| Code | Meaning | Used in |
|---|---|---|
| `0x18` | **SLA+W transmitted, ACK received** — a device answered | `eeprom_probe()`, `rtc_ext_probe()` |
| `0x20` | SLA+W transmitted, **no ACK** — nobody home | documented (probe failure case) |

### I2C0SCLH / I2C0SCLL — SCL clock (`+0x10` / `+0x14`)

```
f_SCL = PCLK / (SCLH + SCLL) = 15,000,000 / (75 + 75) = 100 kHz
```

**Firmware values: `I2C0SCLH = 75`, `I2C0SCLL = 75`.** A 100 kHz bus is the
standard-rated speed for both the AT24C256 and the DS1307/DS3231, and leaves
huge timing margin on the jumper-wired vector board. (At PCLK = 15 MHz the max
bus speed the LPC2148 can produce is 375 kHz.)

## The firmware's sequences (`eeprom.c`)

### `i2c_init()`

```c
PINSEL0 &= ~((3UL << 4) | (3UL << 6));   /* P0.2/P0.3 -> GPIO first        */
PINSEL0 |=  ((1UL << 4) | (1UL << 6));   /* then SCL0 (P0.2) / SDA0 (P0.3) */

I2C0CONCLR = 0x6C;   /* I2ENC + STAC + SIC + AC: clear I2EN, START, SI, AA */
I2C0SCLH   = 75;
I2C0SCLL   = 75;
I2C0CONSET = 0x40;   /* I2EN: turn the interface back on                   */
```

The `0x6C` clear first is what guarantees the peripheral starts from a known
idle state regardless of what a previous boot left behind.

### Primitives

```c
i2c_start():  I2C0CONSET = 0x20;  I2C0CONCLR = 0x08;  i2c_wait_si();
i2c_stop():   I2C0CONSET = 0x10;  I2C0CONCLR = 0x08;
              while (I2C0CONSET & 0x10) { ... }   /* HW clears STO when the
                                                     STOP is actually out */
              delay_us(20);                        /* bus free time         */
i2c_write(d): I2C0DAT = d;      I2C0CONCLR = 0x28;  /* clear STA + SI */
              i2c_wait_si();
i2c_read(a):  if (a) I2C0CONSET = 0x04;            /* ACK (more follows)   */
              else   I2C0CONCLR = 0x04;            /* NACK (last byte)     */
              I2C0CONCLR = 0x08;  i2c_wait_si();  return I2C0DAT;
```

Note the two kinds of acknowledge:
- **AA (bit 2)** in CONSET/CONCLR decides whether the *master* ACKs the byte
  it is reading — ACK for every byte except the last.
- **STAT 0x18 vs 0x20** reports whether the *slave* acknowledged its address.

### `eeprom_byte_write(addr, data)` — the write cycle

```c
i2c_start();
i2c_write(EEPROM_ID);           /* 0xA0: device + write bit  */
i2c_write((addr >> 8) & 0xFF);  /* AT24C256 uses 2 address bytes */
i2c_write(addr & 0xFF);
i2c_write(data);
i2c_stop();
delay_ms(10);                   /* internal write cycle (5 ms max) */
```

### `eeprom_byte_read(addr)` — the repeated-START read

```c
i2c_start();
i2c_write(EEPROM_ID);            /* set the address pointer        */
i2c_write((addr >> 8) & 0xFF);
i2c_write(addr & 0xFF);
i2c_start();                     /* repeated START -> switch to read */
i2c_write(EEPROM_ID | 0x01);     /* 0xA1: device + read bit         */
data = i2c_read(0);              /* one byte, NACK (last)           */
i2c_stop();
```

### `eeprom_probe()` — presence check

`i2c_start()`, then `i2c_write(EEPROM_ID)`, then test
`(I2C0STAT & 0xF8) == 0x18` → acknowledged → present. This is what the boot
self-test's first layer calls.

### `eeprom_selftest()` — presence *and* storage verified

Writes two complementary patterns (`0x5A`, `0xA5`) to the scratch byte at
`EEPROM_SCRATCH_ADDR` and reads each back. Two bitwise-inverse patterns are
deliberate: a bus stuck permanently high (`0xFF`) or low (`0x00`) can
accidentally "pass" a single-pattern test but cannot return both `0x5A` and
`0xA5`. The original byte is restored afterwards — the test is
non-destructive.

## The external RTC shares the bus (`rtc.c`, `RTC_EXT_ENABLED`)

Same primitives, same bounded waits, a separate sticky fault flag
(`rtc_ext_fault`). The DS1307/DS3231 use a register-pointer protocol:

- **Write:** START, `0xD0`, register pointer, data byte, STOP.
- **Read:** START, `0xD0`, pointer → repeated START, `0xD1`, read N bytes (ACK
  all but the last), STOP.
- **Registers:** `0x00` sec (bit 7 = clock halt / osc-stop), `0x01` min,
  `0x02` hour (bit 6 = 12/24 h), `0x03` DOW, `0x04` date, `0x05` month (bit 7
  = century), `0x06` year. All fields are **BCD**.

The full register map of that device, plus the tear-free block read and the
plausibility check, are covered in [rtc.md](rtc.md).

## Hardware consequences

- One 2-wire bus serves both persistent storage and the real-time clock; that
  sharing is why the external-RTC driver deliberately copies the EEPROM
  driver's bounded-wait style rather than writing its own.
- Because every wait is bounded and the result is recorded in sticky fault
  flags, a bus fault surfaces as a *reported hardware fault* (boot self-test,
  or `eeprom_bus_fault()` in the main loop) instead of a hang or silent
  garbage.
- At 100 kHz, an EEPROM byte write takes ~10 ms of the 90 µs of bus time —
  the write-cycle delay dominates, and the firmware just waits it out.
