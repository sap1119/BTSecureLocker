# RTC — the Wall Clock (On-Chip + External Battery-Backed)

## Peripheral intro

The LPC2148 has a built-in real-time clock, and this project adds an optional
external one. They play two different roles:

- **On-chip RTC** — the "working copy" every reader uses (the idle clock
  display, the admin menu, the audit-log timestamp). It is clocked from PCLK
  through a prescaler and read tear-free via the consolidated registers.
- **External RTC (DS1307/DS3231)** — a battery-backed I²C clock kept **only
  when fitted** (`RTC_EXT_ENABLED = 1`). At boot its real time is loaded into
  the on-chip RTC; every clock write is then mirrored back to it. This is what
  makes the time survive a full power-off.

Why two clocks? The LPC2148 has **no RTCX1/RTCX2 crystal pins and no VBAT pin**
— its RTC is PCLK-derived and simply cannot tick while the board is
unpowered. The DS1307/DS3231 has its own 32.768 kHz crystal and a coin-cell
input, so it keeps counting through power-off. Without it, a power cycle loses
the time (an honest, documented limit — see `SECURITY.md`).

## Registers used (on-chip RTC base `0xE0024000`)

| Offset | Register | Name |
|---|---|---|
| +0x08 | `CCR` | Clock Control Register |
| +0x14 | `CTIME0` | Consolidated time (32-bit, atomic) |
| +0x18 | `CTIME1` | Consolidated calendar (32-bit, atomic) |
| +0x20 | `SEC` | Seconds |
| +0x24 | `MIN` | Minutes |
| +0x28 | `HOUR` | Hours (0–23) |
| +0x2C | `DOM` | Day of month |
| +0x30 | `DOW` | Day of week (0 = SUN … 6 = SAT) |
| +0x38 | `MONTH` | Month (1–12) |
| +0x3C | `YEAR` | Year (16-bit, e.g. 2024) |
| +0x80 | `PREINT` | Prescaler integer part |
| +0x84 | `PREFRAC` | Prescaler fractional part |

### CCR — Clock Control Register (`+0x08`)

| Bit | Mask | Meaning |
|---|---|---|
| 0 | `0x01` | **CLKEN** — clock enable (1 = counting) |
| 1 | `0x02` | **CCALEN** — clock calibration / prescaler reset |

Firmware values:
- `CCR = 0x02` — reset the prescaler/divider (a "calibration enable" pulse).
- `CCR = 0x01` — enable the clock and let it start counting.
- Pause/resume: `CCR &= ~0x01` before writing time registers, `CCR |= 0x01`
  after — so a rollover can never race a field update.

### PREINT / PREFRAC — the 1-second prescaler (`+0x80` / `+0x84`)

The RTC derives its 1 Hz tick from PCLK:

```
PREINT  = trunc((PCLK / 32768) - 1)           = 456
PREFRAC = PCLK - ((PREINT + 1) × 32768)        = 15,000,000 - (457 × 32768) = 25024
```

**Firmware values: `PREINT = 456`, `PREFRAC = 25024`** — the exact values that
make the on-chip RTC tick once per second at PCLK = 15 MHz. (These are the
well-known LPC2148 values; the formula above shows they are not magic.)

### CTIME0 / CTIME1 — the tear-free read (`+0x14` / `+0x18`)

Each register packs several calendar fields into one 32-bit word that the
hardware updates **atomically**, so a single read can never see a half-updated
time:

```
CTIME0:  [5:0] seconds   [13:8] minutes   [20:16] hours   [26:24] day of week
CTIME1:  [4:0] day-of-month   [11:8] month   [27:16] year
```

One read of CTIME0 covers the whole time-of-day; one read of CTIME1 covers the
whole date. But a tick can land *between* the two word reads (today's time with
yesterday's date at midnight), so `rtc_get()` re-reads CTIME0 and retakes the
pair if it changed — **bounded** to a few tries, since the RTC only advances
once per second.

## The firmware's sequences (`rtc.c`)

### `rtc_init()` — the boot sequence

```c
CCR = 0x02;        /* reset the prescaler/divider            */
PREINT  = 456;     /* prescaler: 1 tick per second           */
PREFRAC = 25024;
CCR = 0x01;        /* enable (start) the RTC                 */

/* if RTC_EXT_ENABLED: probe the DS1307/3231 */
rtc_battery = 0;
if (rtc_ext_probe()) {
    if (rtc_ext_read(&ext) && rtc_time_plausible(&ext)) {
        rtc_set_time(ext.hour, ext.min, ext.sec);  /* each setter also mirrors */
        rtc_set_date(ext.dom,  ext.month, ext.year);    /* back to the chip    */
        rtc_set_dow(ext.dow);
        rtc_battery = 1;
        return;                /* real time restored - no default */
    }
}

/* no usable battery time: default ONLY if the RTC is completely unset */
if (rtc_onchip_unset()) {
    rtc_set_date(1, 1, 2024);
    rtc_set_time(12, 0, 0);
}
```

Two deliberate rules here (both load-bearing, see `CONTRIBUTING.md`):
- The default is applied **only when `rtc_onchip_unset()`** (tested via
  `YEAR == 0` — no valid time has year 0, and the counter starts at zero on
  fresh silicon). Older code reset the clock to 12:00 on *every* boot.
- The external restore is only accepted after a **plausibility check**
  (`rtc_time_plausible()`): sec ≤ 59, hour ≤ 23, year 2000–2099, etc. A dead
  coin cell or a never-set chip can leave the external RTC at all zeros or
  garbage BCD; restoring that would be worse than a default.

### `rtc_get()` — tear-free read

```c
do {
    c0 = CTIME0;
    c1 = CTIME1;
} while (c0 != CTIME0);          /* retry only if a tick landed mid-read */
```

Then the bit fields are extracted into an `rtc_time`. `rtc_get_stamp()`
formats one snapshot as `"DD/MM/YYYY HH:MM:SS"` with plain digit arithmetic —
no `sprintf` anywhere in the firmware.

### The setters — pause / update / resume + mirror

```c
void rtc_set_time(u8 hh, u8 mm, u8 ss) {
    CCR &= ~0x01;    /* pause the clock while updating   */
    HOUR = hh;  MIN = mm;  SEC = ss;
    CCR |= 0x01;     /* resume                            */
    if (rtc_battery) rtc_ext_set_time(hh, mm, ss);   /* keep the battery copy in step */
}
```

Same shape for `rtc_set_date()` and `rtc_set_dow()`.

### The external RTC driver (DS1307 / DS3231)

- **Address `0xD0`** (write) / `0xD1` (read); all time fields are **BCD**.
- **Registers:** `0x00` sec (bit 7 = clock-halt/osc-stop), `0x01` min, `0x02`
  hour (bit 6 = 12/24 mode), `0x03` DOW (1 = SUN … 7 = SAT), `0x04` date,
  `0x05` month (bit 7 = century), `0x06` year (00–99).
- **Write:** START, `0xD0`, register pointer, data byte, STOP (via the same
  bounded I²C primitives as the EEPROM — see `i2c.md`).
- **Read:** set the pointer to seconds, then a repeated-START block read of
  the seven registers (ACK all but the last), then **re-read the seconds
  register** and retry the whole block (bounded) if the seconds changed — the
  DS1307 does not latch a block read, so a tick can tear it otherwise.
- Bits are masked on conversion: clock-halt off, hour forced to 24-hour mode,
  century bit cleared, and the 1–7 weekday is converted to the LPC2148's 0–6.
- On write, bit 7 of seconds is cleared so the oscillator keeps running
  (DS1307: clock-halt off; DS3231: clears the oscillator-stop flag).

## Hardware consequences

- `PREINT`/`PREFRAC` assume **PCLK = 15 MHz exactly**; change the clocking and
  the RTC gains/loses time at a rate you cannot see until the log timestamps
  drift.
- With the external RTC fitted, the log timestamps and idle clock genuinely
  survive power-off. Without it, the board boots at the 2024 default until the
  admin sets the clock — which is why the boot log records which clock mode is
  active (`rtc_battery_backed()`).
- Every on-chip read goes through CTIME0/1 (atomic) — no tearing, no disabled
  interrupts required. Every on-chip write goes through the pause/resume dance
  — no rollover race.
