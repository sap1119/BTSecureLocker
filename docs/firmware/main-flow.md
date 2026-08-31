# Firmware Flow — `main()`, the POST, and the Main Loop

A walk through `projectmain.c`, the file that owns `main()`. It is the
sequel to the [architecture map](../architecture.md): this page follows the
code in file order.

## The boot banner

```c
SystemInit_SecureLocker();          /* PLL, MAM, every peripheral driver  */
lcd: "Bluetooth" / "Secure System"
UART0: "===================================="
       " Bluetooth Secure Locker Started"
       "===================================="
```

Then `boot_self_test()` runs, then `ensure_default_passwords()`, then the
system logs `"System booted"` and one `"RTC: ..."` line telling you honestly
which clock mode is active (battery-backed external RTC restored, or on-chip
only). See the register-level [clocking.md](../registers/clocking.md) for the
PLL/MAM part and [rtc.md](../registers/rtc.md) for the RTC part.

## The POST, and why it prefixes everything

`boot_self_test()` exists because the old code called `i2c_init()` and
`bluetooth_init()` and *assumed* the devices were there — but those functions
only configure the LPC2148's own pins. With no EEPROM connected, every read
returned garbage, the `"LKR1"` magic marker never matched, and the firmware
"wrote default passwords" into a device that was not there, then denied every
password forever with no explanation.

The fix is two-layered:

1. **The POST runs before `ensure_default_passwords()`.** Defaults are only
   ever written to an EEPROM that has been *verified read/write capable*.
2. **The POST itself distinguishes the two modules' failure modes.**

### Layer 1 — I2C EEPROM: a hard requirement

`eeprom_selftest()` first probes for an address acknowledge at `0xA0`, then
writes two complementary patterns (`0x5A` / `0xA5`) to the scratch byte and
reads them back — a bus stuck high or low can't "pass" both bitwise-inverse
patterns (see [i2c.md](../registers/i2c.md)).

If it fails, the LCD parks on **"I2C EEPROM NOT CONFIGURED"** and re-tests
every `POST_RETRY_MS` (2 s). Simply reseating the module recovers without a
power cycle. The tamper switch is polled inside the retry loop so the
enclosure is never unwatched, even parked on a fault screen.

### Layer 2 — Bluetooth: a 4-state answer, not a bool

The HC-05 can only be partly tested honestly (the module's KEY/EN pin is
unwired in this project, so it is always in data mode and never answers "AT").
`bluetooth_selftest()` therefore returns one of four codes:

| Code | Meaning | Boot result |
|---|---|---|
| `BT_POST_UART_FAIL` | UART1 **internal loopback** failed — the MCU-side link is genuinely broken | Fault: parks and retries |
| `BT_POST_MODULE_FAIL` | *(only with the optional KEY wire)* module silent in command mode | Fault: parks and retries |
| `BT_POST_LINK_OK` | UART1 proven good; module not interrogable — a **pass** | Continues |
| `BT_POST_MODULE_OK` | module answered the AT probe | Continues (strongest) |

The loopback test is the deterministic part: `U1MCR = 0x10` wires the UART1
transmitter to its own receiver *inside the block*, so it proves pin
selection, baud divisor, frame format, FIFOs and the receive path **without
depending on the module at all**. The old POST sent "AT" and treated silence
as a missing module — which is exactly what a healthy, correctly-wired module
in data mode does, so it falsely accused the locker at every boot. That false
alarm is what this design eliminates. See
[bluetooth-hc05.md](bluetooth-hc05.md) for the full story.

## The main loop in detail

After boot, `while (1)` runs forever. Each pass:

1. **Admin button?** `if (admin_flag) admin_menu();` — the menu is entered
   immediately, before anything else.
2. **Tamper + alarm checks** — `check_tamper_and_alert()` and `check_alarm()`.
3. **Standby screen** — `DisplayStandby()`, skipped while the alarm banner is
   up so the alarm isn't instantly painted over.
4. **Wait for Bluetooth** — the inner `while (!bluetooth_available())` loop:
   - keeps polling tamper/alarm/admin,
   - refreshes the live clock *in place* (no flicker) during the post-unlock
     clock window,
   - if the clock window ends mid-wait, swaps the frozen clock back to the
     "WAIT BT PWD" prompt,
   - bails out if the admin button is pressed.
5. **Settle + read** — `bluetooth_settle()` waits for the burst to end (so
   trailing junk is classified, not guessed at by timing), then
   `bluetooth_read_command()` copies the buffer + flags under a masked
   interrupt.
6. **Classify and act** — see below.

### The classification ladder (each wrong branch = one failed attempt)

| Status | What it means | Action |
|---|---|---|
| `BT_RX_OVERFLOW` | payload > 31 chars | deny "exceeded the RX buffer size", `register_failed_attempt()` |
| `BT_RX_TRAILING` | extra chars after `#` (e.g. `1234#123456789`) | deny "extra characters after the #", `register_failed_attempt()` |
| `BT_RX_EMPTY` | a bare `#` with no digits | ignored, not an attempt |
| clean | exactly `1234#` | go to Level-1 compare |

Each read of the stored password from EEPROM is bracketed by
`eeprom_clear_fault()` / `eeprom_bus_fault()`. If the I2C bus died *while the
locker is running* (wire worked loose, module pulled out), the system reports
a **"EEPROM FAULT"** hardware screen instead of silently denying everyone —
and it deliberately does **not** count toward the lockout, because the user
did nothing wrong.

### Level-1 → Level-2 → open

```
Level-1 matches  → "LEVEL1 OK / ENTER L2"
  Level-2 prompt → read_keypad_password()      (both countdowns live)
    idle-timeout → "NO KEY 60 SEC"  → back to Level-1 lock   (not a failure)
    total-timeout→ "L2 TIMEOUT 3MIN"→ back to Level-1 lock   (not a failure)
    4 digits     → compare vs stored L2
        wrong    → "Wrong Level-2 keypad password" + failed attempt
        right    → fail_count = 0
                  → open_locker_sequence()
                  → 30 s post-unlock clock window
```

`read_keypad_password()` is covered in depth in
[authentication.md](authentication.md).

## The motor sequence and the post-unlock clock

`open_locker_sequence()` has a re-entrancy guard (`locker_busy`) so only one
forward + one reverse pulse can ever be in flight. Its timing:

```
motor_stop();  motor_forward();  delay(MOTOR_ROTATE_MS=500);  motor_stop();  → "Locker opened"
"LOCKER OPEN / WAIT..."  delay(LOCKER_OPEN_HOLD_MS=5000)
delay(MOTOR_SETTLE_MS=200)              ← full stop before reversing
motor_reverse(); delay(MOTOR_ROTATE_MS); motor_stop();         → "Locker closed"
```

After a successful cycle, `first_access_done = 1` and the idle screen shows
the live real-time clock for `POST_UNLOCK_RTC_DISPLAY_MS` (30 s), then reverts
to the password prompt. Before the first successful access the idle screen
never shows the clock — it stays on "WAIT BT PWD".

## The lockout

`register_failed_attempt()` increments `fail_count` and, at 3
(`MAX_WRONG_ATTEMPTS`), calls `system_lockout()`:

- the Bluetooth receive buffer is **discarded** first and again afterwards,
  so a password sent during the lockout can't act the instant it ends,
- "SYSTEM LOCKED" + live countdown for `LOCK_DURATION_MS` (30 s), measured
  against `millis()` so tamper alerts can't stretch it,
- the tamper switch stays monitored throughout,
- on exit `fail_count = 0`.

Full attack/response details: [authentication.md](authentication.md) and
[`SECURITY.md`](../../SECURITY.md).
