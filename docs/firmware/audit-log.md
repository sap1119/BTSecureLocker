# Audit Log — the Timestamped Trail Out of UART0

Every significant event — boot, tamper, password results, locker open/close,
admin actions — is timestamped by the on-chip RTC and streamed out over UART0
as a simple text log for a PC to capture. This page explains the format, how
it's produced, and what the captured example in `firmware/examples/1.TXT`
actually shows.

## The format

`security.c` defines two writers, both using `rtc_get_stamp()` for the
timestamp:

```
[DD/MM/YYYY HH:MM:SS] <message>\r\n
```

```c
void log_event(const char *msg)          /* "system booted", "Tamper detected" */
void log_event2(const char *msg, const char *arg)  /* msg + one string argument */
```

`log_event2` is what lets the admin menu put the exact value it just wrote
into the log — e.g. `log_event2("Admin set clock time to ", "12:07:30")` —
without pulling a `printf` into the firmware (there is **no** `printf`/`sprintf`
anywhere; every timestamp is assembled digit-by-digit in `rtc_get_stamp()`).

The timestamp comes from one **tear-free** `rtc_get()` snapshot (see
[rtc.md](../registers/rtc.md)), so a log line can never straddle a clock
rollover. Each line ends with `\r\n` so any Windows/PuTTY terminal renders it
cleanly.

## What never goes into the log

- **Passwords.** Neither the received Bluetooth password nor the stored EEPROM
  password is ever printed. (A debug block that *did* print both was removed
  after it leaked both credentials into the captured log — see below.)
- **Keypad digits.** The Level-2 entry masks everything as `*`.
- The specific reason for a denial *is* logged (e.g. "extra characters after
  the # terminator"), because that's diagnostic value without a secret.

## The event vocabulary

| Event | Example line |
|---|---|
| boot | `System booted` |
| clock mode | `RTC: battery-backed external RTC found - real time restored` / `RTC: no external RTC - on-chip clock cannot keep time across a power-off` |
| POST | `POST FAIL: I2C EEPROM is not configured / not connected` (plus per-module lines) |
| HC-05 presence (first data) | `HC-05 link confirmed: data received from the Bluetooth module` |
| Level-1 | `Bluetooth authentication request received` → `Level-1 Bluetooth password matched` or a specific denial |
| Level-2 | `Level-2 keypad password matched` / `Wrong Level-2 keypad password` / the two abandon reasons |
| motor | `Access granted, opening locker` / `Locker opened` / `Locker closed` |
| lockout | `System locked: 3 consecutive failed attempts` / `System unlocked after the lockout period` |
| tamper | `Tamper detected` |
| admin | the full menu session (entry, each selection, each value written, exit kind) |
| EEPROM fault (runtime) | `HARDWARE FAULT: I2C EEPROM did not respond while reading the Level-1 password` |

## What the captured example (`firmware/examples/1.TXT`) shows

The file is a genuine session capture from a PC terminal. Three things about
it are worth knowing:

**1. It contains an old password leak.** Lines like
`DEBUG: received='1234' stored L1='1234'` appear in the capture. That debug
block printed *both* the received and stored passwords in plain text. It has
been removed from the current source — the log now reports the same diagnostic
value (denial reason, matched/not) without ever printing a credential. The
captured file is kept as an example **and as evidence of that bug fix**.

**2. It shows the real "walked-away" gap.** The gap between
`Level-1 Bluetooth password matched` and `Level-2 keypad password matched` in
the first session is **15 minutes 55 seconds** — Level-1 satisfied, nobody at
the keypad. That is exactly the hole the dual Level-2 timers close
(1 minute per character / 3 minute total, both live-counted — see
[authentication.md](authentication.md)).

**3. It shows a long run of failed password changes.** A stretch of
`Password change failed: wrong old password` entries (then a success) is the
fingerprint of the old admin prompt accepting any key as a digit — fixed in
`read_menu_password()` (see [admin-menu.md](admin-menu.md)).

## Capturing your own log

Connect a USB-to-serial adapter to **P0.0 (LPC2148 TXD0)** at **9600 baud,
8-N-1**, open a terminal (PuTTY/Tera Term), and power the board. Every boot
prints the banner then the running audit trail.

## Related

- The audit-log hardware path: UART0 line control, baud divisor, THRE wait —
  [uart.md](../registers/uart.md).
- Timestamping: tear-free RTC reads and the external battery-backed clock —
  [rtc.md](../registers/rtc.md).
- Which events are recorded and why — the module map in
  [architecture.md](../architecture.md) and the `log_event*` call sites in
  each firmware-flow page.
