# Bench Test Procedure — first power-on, step by step

The full wiring reference is [Connections](../hardware/connections.md); the
build steps are in [Getting Started §5](../getting-started.md). This page is
the **what should happen when you first power it up** and how to tell a good
board from a bad one. The numbered steps mirror Connections §15, with the
expected LCD line and the audit-log line for each.

> ⚠ **Honest caveat.** Nothing in this project has been bench-tested. This
> procedure is written from the firmware's behaviour (each expectation below is
> what the code actually prints/does), but the board has never been assembled,
> so treat it as a plan, not a record.

## Before you begin

- **Power off before every wiring change.**
- Work through the [Build checklist (Connections §14)](../hardware/connections.md)
  at least once: common ground, LCD, keypad rows/columns, EEPROM pull-ups, HC-05
  not on a DB9, L293D pin 1 to +5 V.
- Have the firmware flashed (`majorproject12.uvproj` → `.hex`, via ISP + Flash
  Magic) **or** be ready to flash it at step 2.

---

## The sequence

### 1. Power only — no external modules

PWR LED on, LCD backlight on. Adjust the contrast pot until character blocks
appear. If the backlight is on but nothing shows, the pot is at an extreme.

### 2. Flash the firmware

Hold **ISP**, tap **RST**, release ISP → the chip is in ISP mode. Program
`majorproject12.hex` with Flash Magic at **9600 baud, 12 MHz crystal**.

### 3. Open the terminal first

Connect the **UART0 DB9** to the PC and open PuTTY/Tera Term at **9600 8N1**
*before* resetting, so you capture the whole boot self-test. UART0 is the
on-board MAX232 — nothing to wire.

### 4. Reset

Expect the banner `Bluetooth / Secure System`, then `SELF TEST...`, then the
audit-log line `System booted`.

### 5. EEPROM stage of the POST

Expect `I2C EEPROM OK`. If it parks on `I2C EEPROM NOT CONFIGURED`, check in
this order: the **two 4.7 kΩ pull-ups**, SDA/SCL **not swapped**, **WP → GND**,
VCC → **3.3 V**, and **common ground**. The screen clears by itself the moment
the EEPROM answers — no power cycle needed. (This is a hard requirement: both
passwords live in the EEPROM, so the firmware will not run without it.)

### 6. Bluetooth stage of the POST

Expect `BT LINK OK / HC-05 IN DATA`. **This is a pass, not a fault** — with the
HC-05's KEY pin unwired the module is permanently in data mode and can't answer
an `AT` probe, so the firmware reports honestly that it cannot interrogate it.

`BT UART1 NOT CONFIGURED` is a real fault: the **MCU-side** UART1 failed its
internal loopback test, which is independent of the module. Check PCLK, the
9600 divisor, and that nothing else is jumpered onto P0.8/P0.9 (the on-board
MAX232's UART1 channel is the usual suspect — Connections §8 Warning 2).

### 7. Keypad

Press keys during the Level-2 prompt and confirm one `*` appears per press.
Scrambled digits mean rows and columns are swapped or rotated (Connections §5).

### 8. Level-1 (Bluetooth password)

Pair the phone (default PIN `1234` or `0000`), then send `1234#`. Expect
`LEVEL1 OK / ENTER L2` and the log line `Level-1 Bluetooth password matched`.
If it never matches, check the HC-05 is at 9600, TXD/RXD are not swapped, and
there's no MAX232 contention on P0.8/P0.9.

### 9. The Level-2 countdown

At the keypad prompt, the top-right corner counts down from 60. Press one digit:
it must **snap back to 60** while `TOT:` (the 3-minute total) keeps falling.
Compare against a stopwatch — it should track within a second.

### 10. Level-2 (keypad password)

Type `5678`. Expect `ACCESS GRANTED`, the motor to pulse open, `LOCKER OPEN`
for 5 s, then close. The log shows `Access granted, opening locker` →
`Locker opened` → `Locker closed`, then the clock display for 30 s.

### 11. Timeouts

- Send `1234#` then do nothing: at **60 s** expect `NO KEY 60 SEC` (the
  per-character timer).
- Send `1234#` then tap `A` every ~50 s: at **180 s** expect `L2 TIMEOUT 3MIN`
  (the total-time ceiling — never extended).

Both return the system to `SEND BT PWD`. See
[authentication.md](../firmware/authentication.md) for why there are two timers.

### 12. Lockout

Three consecutive wrong passwords → `SYSTEM LOCKED` with a **live 30-second
countdown**, then back to normal. The log records
`System locked: 3 consecutive failed attempts` and
`System unlocked after the lockout period`.

### 13. Tamper

Press the tamper switch: `TAMPER ALERT` + buzzer + a log line `Tamper detected`.
Do it **during a Level-2 entry** too — the password screen must come back with
your digits intact (the tamper poll is deliberately kept live during entry).

### 14. Admin menu

Press the admin button (P0.7): the menu appears (`1=CLK 2=Alarm / 3=Pwd 4=Set`).
Leaving **any** screen idle for its timeout must return to normal operation on
its own — the top-level is 15 s, every sub-screen 1 minute. See
[admin-menu.md](../firmware/admin-menu.md).

### 15. RTC / power-off test (only if the external RTC is fitted)

In the admin menu set the clock, kill the power, wait a minute or two, power
back on. The clock must show the correct **advanced** time, and the log must
print `RTC: battery-backed external RTC found - real time restored`.

- Without the chip: expect `RTC: no external RTC - on-chip clock cannot keep
  time across a power-off` and the 12:00 default after a power-off. (A mere
  *reset*, with power never lost, now keeps the time even with no external RTC.)
- If a chip is wired but the log says no external RTC: SDA/SCL swapped, no VCC,
  dead coin cell, or the chip is not at address 0x68. Both the DS1307 and DS3231
  have a fixed address — there are no address-select pins.

---

## What a good log looks like

A healthy boot + one successful session:

```
[..] System booted
[..] RTC: battery-backed external RTC found - real time restored
[..] I2C EEPROM OK
[..] BT UART1 OK (loopback)
[..] HC-05 cannot be interrogated (KEY pin not wired) - awaiting data
[..] Bluetooth authentication request received
[..] Level-1 Bluetooth password matched
[..] Level-2 keypad password matched
[..] Access granted, opening locker
[..] Locker opened
[..] Locker closed
```

See [audit-log.md](../firmware/audit-log.md) for the format and a real captured
example with its warnings.

## Troubleshooting

Every failure mode with its fix is in [Connections §16](../hardware/connections.md)
— the three most common:

| Symptom | Likely cause | Fix |
|---|---|---|
| Parks on `I2C EEPROM NOT CONFIGURED` | missing pull-ups, SDA/SCL swapped, WP high, no common ground | Connections §9 |
| `BT UART1 NOT CONFIGURED` | MAX232 UART1 channel jumpered onto P0.8/P0.9 | remove the jumper / leave the DB9 unconnected |
| Motor never turns | L293D pin 1 (1,2EN) not tied to +5 V | tie it HIGH — this firmware has no ENABLE output |

## Related

- The register-level reasons behind the POST: [main-flow.md](../firmware/main-flow.md)
- How to re-run the compile check: [compile-verification.md](compile-verification.md)
