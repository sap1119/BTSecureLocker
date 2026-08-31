# Getting Started — a Beginner's Tour

> "I've never touched an embedded project. Where do I even start?"

You're in the right place. This page assumes **nothing** — it explains what the
hardware is, what the software does, and walks you through the code top to
bottom. When you're done you'll be able to follow any other page in `docs/`.

---

## 1. What is this thing, actually?

A **microcontroller** is a small computer on one chip: a CPU, memory, and
input/output pins that you can read or drive with voltage. This project uses
the **LPC2148**, a 32-bit ARM7 chip. The "firmware" is the program that runs
directly on it — there is no operating system, so the code *is* the whole
product.

The firmware's job, in one sentence:

> **Read a password sent by phone over Bluetooth, read another password typed
> on a keypad, and if both are right, spin a motor to open a lock — all while
> writing a timestamped log of what happened to a PC.**

That's it. Everything in this repo is those few jobs, done carefully.

## 2. The physical world (hardware)

| Part | Its role in the system |
|---|---|
| **LPC2148** | The brain. Reads inputs, runs the logic, drives outputs. |
| **HC-05** | A Bluetooth-to-serial module. Receives the phone's password and hands it to the chip over a serial link (UART1). |
| **4×4 keypad** | 16 keys wired in a grid. The chip scans rows/columns to see which key is pressed. |
| **16×2 LCD** | Shows prompts ("WAIT BT PWD"), the masked keypad digits, live countdowns, the real-time clock. |
| **AT24C256 EEPROM** | A small non-volatile memory chip. Stores the two passwords so they survive power-off. |
| **DS1307/DS3231 RTC** *(optional)* | A battery-backed clock chip. Keeps the *time* correct through power-off. |
| **L293D + DC motor** | The actuator. The H-bridge chip lets the chip spin the motor both directions to open/close the lock. |
| Buzzer, tamper switch, admin button, MAX232 | Alerts, enclosure-intrusion detection, the settings menu button, and the PC serial-log interface. |

The full part list with values is in
[docs/hardware/bill-of-materials.md](hardware/bill-of-materials.md), and the
complete pin-by-pin wiring (with a drawn schematic) is in
[docs/hardware/connections.md](hardware/connections.md).

## 3. The two-factor flow (what the user experiences)

```
Phone app ──"1234#"──▶ LPC2148 ──"5678"──▶ LPC2148 ──▶ motor opens lock
                      (Level 1: check   (Level 2: check
                       Bluetooth pwd)     keypad pwd)
```

Two passwords for a reason: a phone alone can't open it, and a stranger at the
keypad alone can't open it. Both factors must succeed in one session. Every
step is written to the audit log so you can replay exactly what happened.

## 4. A guided tour of the code

The source lives in `firmware/` and is deliberately **flat** (12 C files + 14
headers) so the Keil project's relative paths keep working. The three files to
read first:

### `firmware/projectmain.c` — the "main" file
Everything starts here:
- `SystemInit_SecureLocker()` — turns on the chip's clock (12 MHz crystal →
  60 MHz core), then initialises *every* peripheral (LCD, UARTs, keypad, RTC…).
- `boot_self_test()` — on power-up, checks the EEPROM and Bluetooth link are
  actually wired, showing wiring help on the LCD if not.
- `main()`'s `while(1)` loop — the state machine: wait for Bluetooth password
  → check it → prompt for keypad password → check it → open the lock.

### `firmware/security.c` — the security core
- `password_match()` — the strict password comparison (exact length + every
  character checked, no early exit).
- `log_event()` / `log_event2()` — timestamped audit-log lines.
- `tamper_poll()` / `check_tamper_and_alert()` — the intrusion switch.

### `firmware/bluetooth.c` — the wireless factor
- `UART1_ISR` — the interrupt that receives each character from the phone.
- The classification that rejects `1234#123456789` (trailing junk) and
  over-long payloads.
- The layered boot self-test for the Bluetooth link.

Then, in any order: `keypad.c` (matrix scan), `lcd.c` (display), `eeprom.c`
(I2C memory), `rtc.c` (the clock), `menu.c` (the admin menu), `uart.c`,
`delay.c` (timers), `motor.c`, `buzzer.c`.

> **The single most important file for understanding *why*:** `firmware/defines.h`.
> Every tuning value in the system — password length, timeouts, baud rates,
> EEPROM addresses, LCD layout — is one constant with a comment explaining it.

## 5. Building and flashing it

There are two ways to "build":

**The real build (Keil µVision)** — this is what actually produces a runnable
chip image:
1. Install [Keil µVision](https://www.keil.com/download/product/).
2. Open `firmware/majorproject12.uvproj`.
3. Press **Build (F7)** → you get `majorproject12.hex`.
4. Flash with **Flash Magic** (a free tool) over a USB-serial adapter: LPC2148
   in ISP mode, 9600 baud, 12 MHz crystal.

**The compile-check (any machine, no Keil)** — a full syntax/type/link check
using a free ARM GNU toolchain. It doesn't produce a flashable image, but it
*proves the code is correct* and is what CI runs:
```bash
cd firmware
for f in *.c; do arm-none-eabi-gcc -c -O1 -Wall -Wextra -std=c89 \
    -I. -Ivendor -D__irq= "$f" -o /tmp/"${f%.c}.o"; done
```
If that loop prints nothing, all 12 modules are syntactically and type-correct.
See [docs/testing/compile-verification.md](testing/compile-verification.md).

## 6. First power-on

1. Power the board. The LCD shows a **boot self-test**: `I2C EEPROM OK`, then
   the Bluetooth link result.
2. Open a serial terminal on the PC (PuTTY/Tera Term) at **9600 baud, 8-N-1**
   on the adapter connected to P0.0. You'll see the live audit log.
3. From your phone, pair with the HC-05 and send `1234#`.
4. The LCD says `LEVEL1 OK / ENTER L2`. Type `5678` on the keypad (masked as
   `**` on screen).
5. The motor opens the lock, holds ~5 s, closes. The log records the whole
   session.

The step-by-step procedure is [docs/testing/bench-test-procedure.md](testing/bench-test-procedure.md).

> **Default passwords:** Bluetooth `1234`, keypad `5678`. Change them from the
> admin menu — press the admin button (P0.7) → option 3.

## 7. Where to go next

You've got the mental model. Now pick your depth:

| You want to understand… | Read |
|---|---|
| how the whole system is structured | [docs/architecture.md](architecture.md) |
| the exact register values behind the clock | [docs/registers/clocking.md](registers/clocking.md) |
| how the authentication flow resists attacks | [docs/firmware/authentication.md](firmware/authentication.md) |
| how the Bluetooth receiver works | [docs/firmware/bluetooth-hc05.md](firmware/bluetooth-hc05.md) |
| the complete pin map | [docs/registers/gpio.md](registers/gpio.md) |
| what the audit log records | [docs/firmware/audit-log.md](firmware/audit-log.md) |
| the security model + honest limits | [SECURITY.md](../SECURITY.md) |
| contributing back | [CONTRIBUTING.md](../CONTRIBUTING.md) |

The full index is [docs/README.md](README.md).
