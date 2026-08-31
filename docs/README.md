# Documentation

Everything you need to understand, build, and extend the SecureLocker
firmware. Start with **Getting Started** if you're new; jump to any section
below if you know what you're looking for.

## For absolute beginners

- **[getting-started.md](getting-started.md)** — "I've never touched an
  embedded project": what the hardware is, how the two-factor flow works, a
  guided tour of the code, and how to build/flash it.

## How the system works

- **[architecture.md](architecture.md)** — the module map (12 C files), the
  exact boot sequence, and the main-loop state machine.

### Behaviour-level flow docs (`docs/firmware/`)

Each page explains *what the system does* and points to the registers that
make it happen.

| Page | Covers |
|---|---|
| [firmware/main-flow.md](firmware/main-flow.md) | `main()`, the boot self-test, the classification ladder, the motor sequence, the lockout |
| [firmware/authentication.md](firmware/authentication.md) | Level-1/Level-2 flow, the dual timers, `password_match()`, the lockout |
| [firmware/bluetooth-hc05.md](firmware/bluetooth-hc05.md) | the UART1 receive ISR, the protocol, the burst-settle window, the layered POST |
| [firmware/admin-menu.md](firmware/admin-menu.md) | the EINT2 button, clock/alarm/password sub-menus, the alarm-crossing logic |
| [firmware/audit-log.md](firmware/audit-log.md) | the log format, what is (and is never) logged, the captured example |

## Register-level reference (`docs/registers/`)

> **The big one.** Every peripheral the firmware touches, documented at
> register level — the exact value the firmware writes and *why*. Start at
> [registers/README.md](registers/README.md), which has the "how to read a
> register entry" template and the load-bearing values table.

| Page | Covers |
|---|---|
| [registers/clocking.md](registers/clocking.md) | PLL0, VPBDIV, MAM — 12 MHz → 60 MHz core / 15 MHz PCLK |
| [registers/gpio.md](registers/gpio.md) | PINSEL0/1/2, IO0/IO1 — every pin in the project |
| [registers/uart.md](registers/uart.md) | UART0/UART1 — line control, baud divisor, FIFOs, loopback |
| [registers/i2c.md](registers/i2c.md) | I2C0 — EEPROM + external RTC on one bus |
| [registers/timers.md](registers/timers.md) | Timer0 (delays) + Timer1 (`millis()`), wrap math |
| [registers/rtc.md](registers/rtc.md) | the on-chip RTC, tear-free reads, the external DS1307/3231 |
| [registers/interrupts.md](registers/interrupts.md) | the VIC, UART1 receive, the admin-button EINT2 |
| [registers/memory-map.md](registers/memory-map.md) | flash/RAM layout + the EEPROM password map |

## Hardware

- **[hardware/connections.md](hardware/connections.md)** — full pin-by-pin
  wiring, including a pin-level circuit diagram.
- **[hardware/bill-of-materials.md](hardware/bill-of-materials.md)** — every
  part with values, quantities and purpose.

## Testing & verification

- **[testing/compile-verification.md](testing/compile-verification.md)** — how
  the repo is compile-verified with `arm-none-eabi-gcc` (no Keil needed); this
  is exactly what CI runs.
- **[testing/bench-test-procedure.md](testing/bench-test-procedure.md)** — the
  first power-on sequence: wiring check, POST, login, and what a good log looks
  like.

## Repo-level documents

- [README](../README.md) — the project front page.
- [CONTRIBUTING](../CONTRIBUTING.md) — fork guide, PR workflow, code style, and
  the load-bearing rules.
- [SECURITY](../SECURITY.md) — the security model and its honest limitations.
- [CHANGELOG](../CHANGELOG.md) — what changed in each round.
