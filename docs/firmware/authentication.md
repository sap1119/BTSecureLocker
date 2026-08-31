# Authentication — the Two-Factor Flow and Its Failure Handling

This is the security-critical page: exactly how a user gets in, and exactly
what the firmware does when they don't.

## The two factors

| Factor | Channel | Credential | Default |
|---|---|---|---|
| **Level-1** | Bluetooth (HC-05 over UART1) | 4-digit password + `#` terminator, sent from a phone app | `1234` |
| **Level-2** | Physical 4×4 matrix keypad | 4-digit password, masked `*` on the LCD | `5678` |

Both passwords live in the AT24C256 EEPROM (plaintext, behind the `"LKR1"`
magic marker — a documented limitation, see [`SECURITY.md`](../../SECURITY.md))
and are compared with `password_match()`.

## `password_match()` — the single approved comparison

`security.c` defines **the** comparison used everywhere (Level-1, Level-2, and
the admin password-change screen). Its contract (also in `security.h`):

1. **Length gate.** Rejects anything that is not *exactly* `expected_len`
   (4) characters — checked *before* the value loop so a short entry can
   never be read past its terminator, and a long entry can never be accepted
   on the strength of its first 4 characters.
2. **Full character-by-character comparison, no early exit.** Each pair is
   XOR'd and the differences OR'd together; *every* character is always
   examined, so execution time does not reveal where the first mismatch was.

```c
for (i = 0; i < expected_len; i++)
    diff |= (u8)(entered[i] ^ stored[i]);
return (u8)(diff == 0U);
```

This is what closes the old `strcmp` bypass: a bare `strcmp(bt_cmd, l1_pwd)`
plus a receiver that dropped everything after `#` happily matched
`"1234#123456789"` against `"1234"`. Now the length rule is enforced *at the
point of comparison* and the receiver additionally flags trailing junk, so
neither layer is relied on alone.

## Level-1 — the Bluetooth check

The receiver (see [bluetooth-hc05.md](bluetooth-hc05.md)) accumulates bytes
until `#` and records three fault conditions. The main loop classifies:

| Status | Example | Verdict |
|---|---|---|
| `BT_RX_OVERFLOW` | 40 chars, no `#` | **denied** + failed attempt |
| `BT_RX_TRAILING` | `1234#123456789` | **denied** + failed attempt |
| `BT_RX_EMPTY` | a bare `#` | ignored (no password was offered) |
| clean | exactly `1234#` | compared against the stored L1 |

The EEPROM read of the stored password is fault-guarded: `eeprom_clear_fault()`
before, `eeprom_bus_fault()` after. If the I²C bus died at runtime, the system
shows **"EEPROM FAULT / CHECK WIRING"** — a hardware fault, deliberately **not**
a failed attempt, because the user did nothing wrong.

On a match the LCD shows **"LEVEL1 OK / ENTER L2"** and the keypad prompt
appears. On a mismatch: **"ACCESS DENIED"** + the specific reason logged +
`register_failed_attempt()`.

## Level-2 — the keypad prompt and its dual timers

`read_keypad_password()` is the most careful screen in the firmware, because it
runs for up to 3 minutes with Level-1 *already satisfied*. Its screen:

```
        col: 0123456789012345
      row 0: KEYPAD PWD:  60s     ← seconds until the NEXT character expires
      row 1: **      TOT:175s     ← masked digits + 3-minute total left
```

### The two limits (both measured against `millis()`, both live-counted)

| Limit | Value | Purpose |
|---|---|---|
| `L2_INTERKEY_TIMEOUT_MS` | 1 min | waits for the **next** keypress; **restarted by any key** — digits, `*` backspace, `#` clear, even unused A–D — because its job is detecting "the user walked away", and any key proves somebody is there |
| `L2_TOTAL_TIMEOUT_MS` | 3 min | hard ceiling on the whole entry from the moment the prompt appears; **never extended**. This is what stops the restarting per-character timer from being abused to hold an authenticated session open by tapping a key every 59 s |

Whichever expires first ends the entry, and the two cases are reported
separately to the audit log (`L2_ENTRY_IDLE_OUT` vs `L2_ENTRY_TOTAL_OUT`) so
"stopped typing" and "took too long overall" are distinguishable.

`L2_IDLE_TIMER_ARMED_AT_START` decides whether the 1-minute idle limit applies
from the moment the prompt appears (default: yes — the stricter reading) or
only after the first keypress. Either way the first keypress always arms it,
and the countdown shown is the limit that will actually bite first.

### Editing rules

- `0`–`9` → append the digit, drawn as `*`
- `*` → backspace the previous digit (from buffer and LCD)
- `#` → clear the whole entry
- `A`–`D` → ignored, and they do **not** consume a digit position

### Things that never stop during the entry

- **Tamper monitoring.** `tamper_poll()` runs on every scan iteration; a fresh
  tamper event briefly shows "TAMPER ALERT" and the prompt is restored exactly
  as it was, digits included. This window is the *worst* time to stop watching
  the enclosure, and the old code did exactly that.
- **The countdown redraws.** The LCD is only repainted when the displayed
  second actually changes (each `lcd_data()` costs ~2 ms), so the loop stays
  responsive to keypresses.
- **The matrix scan is polled directly** (`keypad_scan()`) rather than handing
  the whole budget to `keypad_getkey_timeout()`, which would freeze the
  countdown.

### On expiry

The entry is abandoned, the **Bluetooth receive buffer is flushed**, and the
system returns to the Level-1 locked state — the user must send the Bluetooth
password again before the keypad is offered a second time. A timeout is
deliberately **not** a failed attempt: nothing was guessed, so it doesn't
count toward the lockout.

## The keypad hardware behind it (`keypad.c`)

- Rows `P1.16–19` are outputs, idle **HIGH**; columns `P1.20–23` are inputs.
- Scan: pull one row LOW at a time, read the columns; a column reading LOW
  with a known row means a specific key (`key_map[4][4]`).
- Each press gets a 50 µs line-settle, a **bounded** wait-for-release
  (`KEY_RELEASE_MAX_MS` = 500 ms — a held/stuck key can never stall a
  timeout), and a 20 ms debounce.
- `keypad_getkey_timeout()` measures its deadline against `millis()`, so the
  scan cost is counted honestly. **`timeout_ms == 0` means "do not wait"** —
  a caller's own loop drives the scan (that's how the live countdowns work).

## Failure handling and the lockout

`register_failed_attempt()` is the single gate: increment, then if
`fail_count >= MAX_WRONG_ATTEMPTS` (3) call `system_lockout()`.

```
fail_count = 0            reset on every fully successful open
failed L1 (wrong/malformed)  → +1
failed L2 (wrong keypad)     → +1
L2 timeouts                  → 0 (not a guess)
EEPROM bus fault             → 0 (not a guess)
```

`system_lockout()`:

1. **Discards the Bluetooth buffer** — *and discards it again* at the end —
   so a password sent during the lockout cannot be acted on the instant the
   lockout ends.
2. Shows **"SYSTEM LOCKED"** with a live seconds countdown for
   `LOCK_DURATION_MS` (30 s), measured against `millis()` so tamper alerts
   can't silently stretch it (the old code counted 30 × 1 s sleeps, which a
   3-second tamper buzz turned into 33 seconds).
3. Keeps polling the tamper switch throughout, redrawing the lockout screen
   around alerts.
4. Resets `fail_count` on exit.

## Why the design resists the obvious attacks

| Attack | Defence |
|---|---|
| Send `1234#123456789` | `BT_RX_TRAILING` rejects it (and it counts as a failed attempt) |
| Send `1234#` twice in a burst | only the first `#` is a terminator; bytes after it while a command is pending set `BT_RX_TRAILING` |
| Flood with a 40-char string | `BT_RX_OVERFLOW` rejects it |
| "Password + `#` + padding" timing | the settle window makes trailing-junk detection deterministic, not timing-luck |
| Brute-force by holding the session open | the 3-minute ceiling can't be extended; timeouts don't buy another attempt |
| Brute-force quickly | 3 wrong attempts → 30 s lockout, with a live countdown |
| Timing the compare | `password_match()` always examines all 4 characters |
| Walk away mid-entry | both limits expire with a live warning; session returns to Level-1 lock |
