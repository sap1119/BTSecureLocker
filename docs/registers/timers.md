# Timer0 / Timer1 — Delays and the Free-Running `millis()` Time Base

## Peripheral intro

The LPC2148 has four 32-bit timers. This project uses exactly two, with two
**non-overlapping jobs** (the split is a load-bearing rule — see
`CONTRIBUTING.md`):

| Timer | Job | Nature |
|---|---|---|
| **Timer0** | `delay_ms()` / `delay_us()` | Reset and restarted by **every** call. Never relied on to keep counting across calls. |
| **Timer1** | the `millis()` time base | Started **once** by `timebase_init()`, then left alone forever. `millis()` just reads `T1TC`. |

Both run in **timer mode** (count PCLK cycles) with a prescaler chosen so the
counter ticks **once per ms** (or per µs for the microsecond delay). PCLK is
the 15 MHz peripheral clock produced by the PLL/VPBDIV setup — the prescaler
arithmetic is calibrated against it.

```
PCLK 15 MHz → prescaler ÷15000 → T0TC/T1TC ticks once per 1 ms
           → prescaler ÷15     → T0TC ticks once per 1 µs   (delay_us only)
```

## Registers used (T0 base `0xE0004000`, T1 base `0xE0008000`)

| Offset | Register | Name |
|---|---|---|
| +0x04 | `TnTCR` | Timer Control Register |
| +0x08 | `TnTC` | Timer Counter (the "tick count") |
| +0x0C | `TnPR` | Prescale Register |
| +0x10 | `TnPC` | Prescale Counter |
| +0x70 | `TnCTCR` | Count Control Register |

### TnCTCR — Count Control Register (`+0x70`)

**Firmware value: `0x00`** → **timer mode**: the timer counts PCLK cycles.
(The other mode would be "counter mode", counting rising/falling edges on a
pin — not used here.)

### TnTCR — Timer Control Register (`+0x04`)

| Bit | Mask | Meaning |
|---|---|---|
| 0 | `0x01` | CEN — counter **enable** (1 = counting) |
| 1 | `0x02` | CR — counter **reset** (1 = counter + prescaler reset) |

**Firmware sequence** used by every timer setup:

```
T0TCR = 0x02;   /* reset the counter and prescale counter */
T0TCR = 0x01;   /* start counting                         */
...             /* (delay loop / forever for T1)          */
T0TCR = 0x00;   /* stop — delay_ms()/delay_us() only      */
```

### TnPR — Prescale Register (`+0x0C`)

The counter ticks once every `TnPR + 1` PCLK cycles. Firmware values:

| Call | `TnPR` | Tick period | Used for |
|---|---|---|---|
| `delay_ms()` | `15000 - 1` = **14999** | 15000 / 15 MHz = **1 ms** | all millisecond delays, keypad scan cadence |
| `delay_us()` | `15 - 1` = **14** | 15 / 15 MHz = **1 µs** | I²C bus-free time, short strobes |
| `timebase_init()` (T1) | `15000 - 1` = **14999** | **1 ms** | the `millis()` time base |

### TnTC — Timer Counter (`+0x08`)

The actual count. `delay_ms()` spins `while (T0TC < ms);`. `millis()` is
`(u32)T1TC`. No match register and no interrupt are used — T1 is only ever
**read**, which makes it the cheapest possible time base.

## The firmware's sequences (`delay.c`)

### `delay_ms(ms)` / `delay_us(us)` — the block-then-release pattern

```c
T0CTCR = 0x00;       /* timer mode (count PCLK)                */
T0PR   = ms? 14999 : 14;   /* 14999 for ms, 14 for us          */
T0TC   = 0x00;       /* zero the counter                       */
T0TCR  = 0x02;       /* reset counter + prescaler              */
T0TCR  = 0x01;       /* start                                 */
while (T0TC < ms);   /* busy-wait for the requested interval   */
T0TCR  = 0x00;       /* stop - Timer0 is released for the next caller */
```

### `timebase_init()` — start Timer1 and never touch it again

```c
T1CTCR = 0x00;       /* timer mode                              */
T1PR   = 14999;      /* T1TC increments once per millisecond    */
T1TC   = 0x00;       /* start from zero                         */
T1TCR  = 0x02;       /* reset counter + prescaler               */
T1TCR  = 0x01;       /* start - and never stop                  */
```

Called **first** in `SystemInit_SecureLocker()` — but only after the PLL/VPBDIV
code has settled PCLK at 15 MHz, since the prescaler is calibrated for that
clock.

### `millis()` and the wrap-around math

`millis()` returns `(u32)T1TC`. T1TC is 32-bit, so the count **wraps every
2³² ms ≈ 49.7 days**. All timeouts therefore compare with `elapsed_since()`,
never with `<`/`>` directly:

```c
u32 elapsed_since(u32 start_ms) { return (u32)(millis() - start_ms); }
```

The subtraction is deliberately unsigned: if `start_ms = 0xFFFFFF00` and the
counter has wrapped to `0x00000010`, then `(0x10 - 0xFFFFFF00)` evaluates to
`0x110` = 272 ms — the true interval. A signed subtraction or a `now < start`
test would be wrong once every 49.7 days.

## Where the time base is used

- `keypad_getkey_timeout(ms, ...)` — bounded key wait, `0` = "do not wait".
- The Level-2 entry timers (`L2_TOTAL_TIMEOUT_MS`, `L2_INTERKEY_TIMEOUT_MS`)
  and their live countdowns (caller polls `keypad_scan()`).
- `bluetooth_settle()` — quiet-window detection capped by
  `BT_SETTLE_MAX_MS`.
- The 30-second post-unlock clock display (`POST_UNLOCK_RTC_DISPLAY_MS`) and
  the system lockout (`LOCK_DURATION_MS`).
- The bounded waits in the UART/I²C drivers call `delay_ms()`/`delay_us()`
  (e.g. the 10 ms EEPROM write-cycle delay).

## Hardware consequences

- The two-timer split cannot race: nothing but `timebase_init()` ever writes
  T1 registers, and nothing ever expects T0's count to survive a delay call.
- Timer mode + prescaler is calibrated to **15 MHz PCLK exactly**. If the PLL
  or VPBDIV ever changes, `delay_ms()` and `millis()` silently drift together
  and *every* timeout in the system changes.
- Because T1 uses no match register or interrupt, the time base costs nothing
  in jitter or latency — it never takes the CPU away from the main loop.
