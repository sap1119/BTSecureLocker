# Admin Menu — the EINT2 Button, Its Sub-Menus, and the Alarm

The admin menu was merged in from a separate "EnviroTime" project and
rewritten to call SecureLocker's own drivers. It is entered with a physical
push-button on P0.7 (EINT2). All real work happens in the foreground: the ISR
just sets `admin_flag`.

## The button and the ISR

`admin_int_init()` (see [interrupts.md](../registers/interrupts.md) for the
register details):

- P0.7 → EINT2 alternate function (`PINSEL0 |= 3<<14`)
- **falling-edge** triggered (`EXTMODE |= 1<<2`, `EXTPOLAR &= ~(1<<2)`) — the
  button pulls the line low; a *level*-sensitive config would retrigger for as
  long as it is held
- VIC source 16, vectored slot 2

```c
__irq void EINT2_ISR(void)
{
    admin_flag = 1;
    EXTINT = (1UL << ADMIN_EINT_BIT);   /* clear the pending edge */
    VICVectAddr = 0;                     /* end-of-interrupt       */
}
```

The main loop polls `admin_flag` at the top of every pass and calls
`admin_menu()`, which immediately re-clears it.

## The top-level menu

```
Line 1: 1=CLK  2=Alarm
Line 2: 3=Pwd  4=Set
```

| Key | Action |
|---|---|
| `1` | **CLK Setting** → 1=Time · 2=Date · 3=Day |
| `2` | **Alarm** → 1=Set · 2=On/Off · 3/B=Reset |
| `3` | **Password** → choose L1 (Bluetooth) or L2 (keypad), then old/new/confirm |
| `4` | **Set** — save & return to normal operation |
| `D` | cancel / go back one level |
| `*` | backspace while entering a value |
| `#` | confirm the value being entered |

If no key is pressed for `MENU_TIMEOUT_MS` (15 s) on the top-level screen, the
menu exits by itself (ATM-style idle timeout).

## Every sub-screen bounds its own wait

The 15-second top-level timeout covers **only** that one screen. That was the
old bug: the moment the admin pressed `1` or `2`, control moved into a
sub-screen where no timeout applied, and an admin who walked away left the
locker sitting on a settings screen forever — unable to accept a Bluetooth
password, with the tamper switch not being polled and the LCD stranded.

The fix: `menu_wait_key()` wraps `keypad_getkey_timeout()`, and every sub-menu
uses it with its own budget:

| Screen(s) | Budget |
|---|---|
| password fields (old/new/confirm, L1/L2 selection) | `ADMIN_PWD_TIMEOUT_MS` (1 min each, live countdown) |
| CLK/Alarm navigation and value-entry screens | `MENU_INPUT_TIMEOUT_MS` (1 min) |

A screen that times out returns 0, which is **propagated all the way up**: one
abandoned screen unwinds the *whole* menu at once (rather than dropping back
one level), so an abandoned admin session always releases the LCD and returns
the locker to normal operation. A timeout is also logged distinctly from a
deliberate `D` cancel.

## The password-change screen

`edit_passwords()` is the security-relevant part of the menu:

1. Choose `1` (L1) or `2` (L2).
2. Read the **current** password (masked `*`), verify with `password_match()`.
3. Read the **new** password, then a **confirmation**.
4. On match, write to EEPROM and **immediately read back and compare** before
   declaring success — with one retry, because a bus glitch during a write
   should be recoverable, not silently corrupting the stored password:

```c
eeprom_write_str(addr, newp, PWD_LEN);
eeprom_read_str(addr, readback, PWD_LEN);
if (password_match(readback, newp, PWD_LEN)) { "PWD UPDATED"; }
else { retry once; if still bad → "EEPROM WRITE FAIL"; }
```

All three fields go through `read_menu_password()`, which:
- accepts only `0`–`9` (a stray key can't be stored as a digit),
- supports `*` backspace and `#` clear,
- ignores A–D,
- shows a **live seconds countdown** in the top-right corner,
- times out as a whole field after `ADMIN_PWD_TIMEOUT_MS`.

The old code read passwords with a bare `keypad_getkey()` loop that accepted
*any* key as a digit — the most likely cause of the long run of "Password
change failed: wrong old password" entries in the captured log.

Every significant action is timestamped on the audit log, e.g.:

```
[..] Password change opened from the admin menu
[..] Password change: Level-1 (Bluetooth) password selected
[..] Password updated and verified in EEPROM (L1)
```

## The CLK sub-menu

`clk_setting()` → Time / Date / Day, each editing one RTC field through the
`rtc_set_*()` setters — which **pause the RTC around the update** (`CCR &=
~1` … `CCR |= 1`). The old code assigned straight to the HOUR/MIN/SEC
registers with the clock running, which can race a rollover. Each change is
logged with the exact value written (via `fmt_hms` / `fmt_dmy`, no `sprintf`):

```
[..] Admin set clock time to 12:07:30
[..] Admin set clock date to 05/09/2026
[..] Admin set day of week to 5
```

`input_value()` collects digit-by-digit values, validates against the caller's
range, shows an "Out of Range!" retry, supports backspace, and refuses to grow
a 4-digit year into 5 digits (the old `value < 9999` guard overflowed the
on-screen field).

## The alarm — set, toggle, reset, and the crossing logic

The alarm state lives in `menu.c` (`alarm_hour`, `alarm_min`, `alarm_sec`,
`alarm_enabled`, `alarm_triggered`). The sub-menu lets the admin set the time,
toggle it on/off, or reset it (clearing the time and disabling).

`check_alarm()` is called from the main loop every pass. Its cleverness is in
**how** it detects "time reached":

> The old code required an *exact* `HH:MM:SS` match, which only works if the
> main loop happens to look during the one second the alarm is due. It
> regularly doesn't: `open_locker_sequence()` blocks ~6 s, the Level-2 entry up
> to 3 minutes, a lockout 30 s. If the due second passed inside any of those,
> the alarm was missed completely — the one job it had.

The new logic converts each read to seconds-since-midnight and compares against
the **previous** call's value: if the alarm time lies anywhere in the interval
that just elapsed, it fires — however long that interval was. A two-branch test
also handles time running backwards (midnight, or the admin setting the clock
back):

```c
if (alarm_last_secs <= cur)
    fired = (target > alarm_last_secs) && (target <= cur);      /* normal day   */
else
    fired = (target > alarm_last_secs) || (target <= cur);      /* wrapped day  */
```

Once fired, `alarm_triggered` latches and the buzzer stays on until the admin
stops it from the Alarm sub-menu. The main loop deliberately **skips
`DisplayStandby()` while `alarm_triggered`** so the "** ALARM!! ** /
Admin=Stop" banner isn't painted over. `alarm_rearm()` (called after any alarm
change) resets the latch and takes a fresh baseline so arming can never be
misread as having just crossed the new time.

## The audit trail

The menu writes a *complete* session to the log — entry, each selection, each
value written, and both exit kinds (idle timeout vs deliberate):

```
[..] Admin menu opened (admin button pressed)
[..] Admin menu: option 2 = Alarm selected
[..] Admin set alarm time to 07:30:00
[..] Admin menu exit: sub-screen timeout        (or "Admin menu exit")
```

See [audit-log.md](audit-log.md) for the log format and the captured example.
