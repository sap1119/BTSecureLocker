# Clocking — PLL0, VPBDIV, MAM

## Peripheral intro

The LPC2148 boots running from an external 12 MHz crystal but at a default core
clock of 1/4 of that (the internal RC oscillator hands off to the crystal via
the boot ROM). This project needs a 60 MHz core clock and a 15 MHz peripheral
clock — the value that every UART divisor, timer prescaler, I²C clock and RTC
prescaler is calibrated against. That multiplication is the job of **PLL0**
(Phase-Locked Loop 0); the division of the core clock down to the peripheral
(VLSI Peripheral Bus) clock is **VPBDIV**; and **MAM** (Memory Accelerator
Module) adds instruction prefetch so the 60 MHz core can keep the flash from
being its bottleneck.

### The one-line summary

> 12 MHz crystal → **PLL0 ×6 / ÷4** → 60 MHz core → **VPBDIV ÷4** → 15 MHz
> peripheral clock. Then **MAM on** so the flash doesn't stall the core.

## Registers used

### PLL0CON — PLL Control Register (`0xE01FC080`)

| Bit | Name | Meaning |
|---|---|---|
| 0 | PLLE | PLL **enable** (0 = off, 1 = on) |
| 1 | PLLC | PLL **connect** (0 = not connected, 1 = CCLK from PLL) |

Writes only take effect after the **feed sequence** (below). The firmware
sequence: `0x00` (off), then `0x01` (enable), then `0x03` (enable **and**
connect). Bit 1 must only be set once the PLL has locked.

### PLL0CFG — PLL Configuration Register (`0xE01FC084`)

| Bit | Name | Meaning |
|---|---|---|
| 4:0 | MSEL | **Multiplier** — CCLK = crystal × (MSEL + 1) |
| 6:5 | PSEL | **Divider** — 0b00 = ÷4, 0b01 = ÷2, 0b10 = ÷1, 0b11 = ÷8 |

**Firmware value: `0x24`** → MSEL = 4 (so ×5), PSEL = 0b10 (so ÷1).
- CCLK = 12 MHz × (4 + 1) = **60 MHz**.
- PSEL 0b10 (÷1) is what the ARM7 core needs for 60 MHz (the PLL "CCO" runs at
  4× CCLK = 240 MHz, inside the LPC2148's 156–320 MHz CCO range).

### PLL0STAT — PLL Status Register (`0xE01FC088`)

| Bit | Name | Meaning |
|---|---|---|
| 10 | PLOCK | **PLL locked** — read as 1 once the output frequency is stable |

The firmware spins on this bit (see the "one deliberate unbounded wait" note).

### PLL0FEED — PLL Feed Register (`0xE01FC08C`)

Writing `0xAA` then `0x55` to this register **commits** any pending change to
PLL0CON/PLL0CFG. This is a fixed hardware requirement of the LPC2148, not a
project convention. The firmware's helper:

```c
static void pll_feed(void) { PLL0FEED = 0xAA; PLL0FEED = 0x55; }
```

### VPBDIV — VPB Divider Register (`0xE01FC100`)

| Value | PCLK |
|---|---|
| 0b00 | CCLK / 4 |
| 0b01 | CCLK |
| 0b10 | CCLK / 2 |

**Firmware value: `0x00`** → PCLK = 60 MHz / 4 = **15 MHz**. This is the
*only* legal choice at 60 MHz core — the LPC2148's peripherals are rated for a
25 MHz max PCLK.

### MAMCR — MAM Control Register (`0xE01FC000`) and MAMTIM (`0xE01FC004`)

| MAMCR value | MAM mode |
|---|---|
| 0b00 | MAM off |
| 0b01 | Partially enabled |
| 0b10 | Fully enabled |

| MAMTIM value | Flash fetch cycles |
|---|---|
| 0b001 | 1 cycle (≤ 20 MHz) |
| 0b010 | 2 cycles (≤ 40 MHz) |
| 0b100 | 4 cycles (≤ 60 MHz) |

The firmware sets **`MAMTIM = 0x04`** (4 cycles, correct for 60 MHz) and then
**`MAMCR = 0x02`** (fully enabled). The MAM must be **disabled first**
(`MAMCR = 0x00`) while the clock is being changed, then re-enabled after.

## The firmware's sequence (from `projectmain.c`, `SystemInit_SecureLocker()`)

```c
MAMCR = 0x00;                  /* 1. MAM OFF while the clock changes          */
PLL0CON = 0x00; pll_feed();    /* 2. PLL disabled                             */
PLL0CFG = 0x24; pll_feed();    /* 3. Configure: ×5 multiplier, ÷1 divider     */
PLL0CON = 0x01; pll_feed();    /* 4. PLL enabled (not yet connected)          */
while (!(PLL0STAT & (1UL << 10)));   /* 5. Wait for PLOCK                     */
PLL0CON = 0x03; pll_feed();    /* 6. Enable AND connect → CCLK = 60 MHz       */
VPBDIV = 0x00;                 /* 7. PCLK = CCLK/4 = 15 MHz                   */
MAMTIM = 0x04;                 /* 8. 4 fetch cycles for 60 MHz                */
MAMCR  = 0x02;                 /* 9. MAM fully on                             */
```

> The **feed sequence** must follow every PLL0CON/PLL0CFG write or the write is
> silently ignored — a classic LPC2148 gotcha. Each `pll_feed()` is the two
> back-to-back writes `0xAA, 0x55`.

### The one deliberately unbounded wait

```c
while (!(PLL0STAT & (1UL << 10)));   /* PLOCK */
```

This is the **only** unbounded spin in the whole firmware, and it is deliberate
and documented in place. If the 12 MHz crystal is dead, the PLL never locks.
Carrying on anyway would run the CPU at a wrong frequency, silently
invalidating **every** derived constant in the system — UART baud divisors,
Timer0/Timer1 prescalers, the RTC prescaler, the I²C clock. A locker whose
timeouts and baud rates are all quietly wrong is worse than one that visibly
fails to start, so stopping is correct. (Every other wait in the project is
bounded.)

## Hardware consequences

- CCLK = 60 MHz: the ARM7TDMI-S core executes at its maximum rated speed.
- PCLK = 15 MHz: legal for the peripheral bus (max 25 MHz).
- All derived values in the rest of this documentation **assume these two
  numbers**. If either changes, `PREINT/PREFRAC`, `T0PR/T1PR`, the UART
  divisors and the I²C SCL counts are all wrong.
- MAM at 4 cycles + fully enabled: flash reads are prefetched, so instruction
  fetch does not dominate execution time at 60 MHz.

> **Tune-if-changed:** a different crystal (say 12 MHz is already assumed) or a
> different PLL target changes `PLL0CFG` and `MAMTIM` together. They must not
> be changed independently.
