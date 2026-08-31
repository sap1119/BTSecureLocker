# 📋 Submission Notes

## Project Title
**Bluetooth-Based Secure Locker with Access Logging**

| Field | Detail |
|-------|--------|
| **Student name** | PAWAR SATHYANARAYANA |
| **Roll / Enrolment no.** | V25HE8-P2 |
| **Course / Subject** | Embedded Systems / Microcontrollers (ARM7) |
| **Guide / Faculty** | SHASHANK , CHANDRAMOULI |
| **Microcontroller** | NXP **LPC2148** (ARM7TDMI-S, 32-bit) @ 60 MHz core / 15 MHz PCLK |
| **Language / Tools** | Embedded C (strict C89) · Keil µVision (Arm Compiler) · Flash Magic · arm-none-eabi-gcc (CI) |
| **Repository** | https://github.com/sap1119/BTSecureLocker |
| **Date of submission** | 31 / 08 / 2026 |

---

## 1. Abstract

This project implements a **two-factor authenticated electronic locker** on the
LPC2148 microcontroller. Access requires **two independent credentials**:

1. **Level 1 — Bluetooth password** sent wirelessly from an Android phone
   (HC-05 module over UART1, interrupt-driven reception). Only `#` terminates
   the command; trailing bytes after `#` are rejected.
2. **Level 2 — Keypad PIN** entered physically on a 4×4 matrix keypad
   (P1.16–P1.23), masked on the LCD as `****`.

Only when **both** match the stored values does a DC gear motor (via an L293D
H-bridge) open the locker, hold it open for 5 seconds, and auto-close it.
Every event — successful unlocks, failed attempts, timeouts, tamper alerts,
lockouts, and admin-menu changes — is **timestamped by the real-time clock**
and streamed to a **PC terminal over UART0 (MAX232 → DB9)** at 9600 baud.
Both passwords are stored **persistently in a non-volatile AT24C256 I²C
EEPROM** (survives power-off). The system is protected by a boot self-test
(POST), tamper detection that runs during *every* phase, a 3-strike lockout,
constant-time password comparison, and an audited on-device admin menu.

---

## 2. Objectives

- Demonstrate **dual-factor (2FA) authentication** on a bare-metal ARM7 system.
- Integrate **multiple peripherals** — LCD, 4×4 keypad, Bluetooth UART,
  I²C EEPROM, RTC, DC motor, buzzer, MAX232 serial — in one coordinated
  application.
- Implement **persistent, timestamped access logging** and **non-volatile
  password storage** that survives power loss.
- Harden the authentication path: password-bypass defence, constant-time
  compare, dual timeout countdowns, lockout, and **tamper detection during
  every state** (including password entry and lockout).
- Provide a **boot self-test** that verifies the EEPROM and Bluetooth link are
  actually wired, and reports wiring faults on the LCD instead of failing
  silently.
- Provide an **audited, time-limited admin menu** (clock, alarm, password).

---

## 3. Key Features Implemented

| # | Feature | Description |
|---|---------|-------------|
| 1 | Dual-factor authentication | Bluetooth password **and** keypad PIN, each validated independently — one factor alone opens nothing |
| 2 | Boot self-test (POST) | EEPROM read/write probe (0x5A/0xA5 patterns) + Bluetooth UART1 internal loopback; fault screens with wiring help; tamper switch still polled while parked |
| 3 | 16×2 LCD interface | HD44780 in **4-bit mode** on P0.16–P0.21; prompts, masked digits, live countdowns, clock, fault screens |
| 4 | Interrupt-driven Bluetooth RX | UART1 ISR + bounded ring buffer; `#` terminates; overflow and trailing-junk classified |
| 5 | Password-bypass defence | `1234#123456789` is **rejected**; the compare examines every character |
| 6 | Constant-time compare | `password_match()` — exact length + XOR, no early exit (timing can't leak how much matched) |
| 7 | Non-volatile storage | AT24C256 I²C EEPROM @ 0xA0: `LKR1` magic + L1@0x0010 + L2@0x0020; defaults only written after the POST passes |
| 8 | Real-time timestamping | On-chip RTC → `DD/MM/YYYY HH:MM:SS` on every log line; optional external DS1307/DS3231 for power-off survival |
| 9 | Level-2 dual timers | 3-minute total ceiling + 1-minute per-character, both with **live seconds countdown**; timeouts ≠ failed attempts |
| 10 | Tamper detection | NC switch on P0.4, polled during **every** phase; alarm + log on intrusion |
| 11 | Lockout | 3 wrong attempts → 30 s lockout with live countdown; Bluetooth buffer flushed **before and after** |
| 12 | Motorised lock | L293D H-bridge: forward 500 ms → hold 5 s → reverse 500 ms; re-entrancy guard |
| 13 | Buzzer alerts | distinct tamper and alarm patterns |
| 14 | Audited admin menu (EINT2) | clock/alarm/password changes logged with the exact new value; digits never logged |
| 15 | PC logging | UART0 @ 9600 → MAX232 → DB9 → PC terminal (captured session in `firmware/examples/1.TXT`) |
| 16 | CI verification | GitHub Actions compile-checks all 12 modules on every push (arm-none-eabi-gcc, strict C89, zero warnings) |

---

## 4. Hardware Used

LPC2148 (ARM7TDMI-S, 60 MHz) · 16×2 HD44780 LCD (4-bit, P0.16–P0.21) ·
4×4 matrix keypad (P1.16–P1.23) · HC-05 Bluetooth module (UART1, 9600 baud) ·
AT24C256 I²C EEPROM (0xA0, password storage) · on-chip RTC (+ optional
battery-backed **DS1307/DS3231**, I²C 0xD0) · L293D H-bridge + DC gear motor ·
active buzzer (P1.26) · tamper switch (P0.4, NC) · admin push-button
(P0.7 → **EINT2**) · MAX232 + DB9 (UART0 audit log) · 12 MHz crystal (PLL →
60 MHz core / 15 MHz PCLK) · regulated 5 V / 3.3 V supplies + separate 6–12 V
motor supply.

*Full pin-by-pin wiring and the pin-level circuit diagram are in
[`docs/hardware/connections.md`](docs/hardware/connections.md); the parts list
is in [`docs/hardware/bill-of-materials.md`](docs/hardware/bill-of-materials.md).*

---

## 5. Software Architecture

Modular bare-metal design — 12 small C files + 14 headers kept **flat** in
`firmware/` so the Keil project (`majorproject12.uvproj`, relative paths) builds
unmodified. A main-loop state machine in `projectmain.c` coordinates the
drivers:

```
defines.h (config) → delay → lcd → keypad → uart → bluetooth →
i2c → eeprom → rtc → security → menu → motor → buzzer → projectmain (main)
```

- **Clock:** 12 MHz crystal → PLL (`PLL0CFG=0x24`, ×6, ÷4) → **CCLK = 60 MHz**;
  `VPBDIV=0` → **PCLK = 15 MHz**; MAM enabled.
- **Boot sequence:** `SystemInit_SecureLocker()` → LCD splash → `boot_self_test()`
  (EEPROM + Bluetooth) → `ensure_default_passwords()` → main loop.
- **Security core:** every password check goes through `password_match()`
  (constant-time); EEPROM reads are fault-guarded (an I²C bus fault shows a
  hardware screen and never counts toward the lockout).
- **Timing:** Timer1 = free-running `millis()` timebase that every timeout
  measures against; Timer0 = short delays.
- **Interrupts:** VIC — UART1 receive (IRQ 7) and admin button EINT2 (IRQ 16);
  ISRs only raise flags, all work is in the foreground.
- **Register access:** the vendored NXP device header `vendor/LPC214x.h` — no
  vendor-pack dependency, and it makes CI builds work without Keil.
- **Two ways to build:** the real project is **Keil µVision**
  (`firmware/majorproject12.uvproj` → `majorproject12.hex`); the repo is also
  **compile-verified with arm-none-eabi-gcc** (exactly what CI runs).

---

## 6. How to Build & Run

The repository includes a **pre-built, ready-to-flash `firmware/majorproject12.hex`**,
so the project can be demonstrated even without compiling.

**Option 1 — Keil µVision (recommended):**
open `firmware/majorproject12.uvproj` → press **Build (F7)** →
`majorproject12.hex` is produced. Nothing to set up.

**Option 2 — no-Keil compile-check (any machine):**
```bash
cd firmware
for f in *.c; do arm-none-eabi-gcc -c -O1 -Wall -Wextra -std=c89 \
    -I. -Ivendor -D__irq= "$f" -o /tmp/"${f%.c}.o" || exit 1; done
```

**Flashing:** Flash Magic → LPC2148 → 12 MHz crystal → ISP mode (hold ISP,
tap RST, release ISP) → 9600 baud → select `majorproject12.hex` → Start.
(The LPC2148 boot ROM requires a valid ARM exception-vector checksum at 0x14 —
sum of the 8 vector words = 0 — which Flash Magic validates.)

**Default credentials (first boot):** Bluetooth `1234`, keypad PIN `5678`.
Both are changeable from the admin menu (press the admin button).

---

## 7. Verification Done

| Check | Result |
|-------|--------|
| Compilation (all 12 modules) | **0 errors, 0 warnings** — `arm-none-eabi-gcc -O1 -Wall -Wextra -std=c89` |
| Relocatable link + symbol check | only `strcmp` / `__aeabi_uidiv` unresolved (libc / compiler runtime) |
| CI | GitHub Actions runs the same compile-check on every push |
| Firmware size | small — well within the LPC2148's 512 KB flash / 32 KB RAM |
| Hardware — core system | **bench-tested on the physical board**: POST, two-factor entry, motor open/close, lockout, tamper, admin menu, alarm, UART0 log, on-chip RTC |
| Hardware — external RTC | **not** tested — the DS1307/DS3231 chip + coin cell must be physically fitted (firmware auto-detects it) |
| Future-advancements | design roadmap only — not implemented (see §9) |

> Full first-power-on sequence: [`docs/testing/bench-test-procedure.md`](docs/testing/bench-test-procedure.md).

---

## 8. Repository / Deliverables Checklist

- [x] `firmware/` — all 12 C modules + 14 headers + `Startup.s` (flat, Keil-relative)
- [x] `firmware/majorproject12.uvproj` + `.sct` — the Keil project and memory layout
- [x] `firmware/majorproject12.hex` — pre-built, flashable firmware
- [x] `firmware/vendor/LPC214x.h` — vendored NXP device header (builds without Keil)
- [x] `firmware/examples/1.TXT` — captured audit-log session
- [x] `docs/` — getting-started, architecture, **flowchart**, registers (×9), firmware (×5), hardware (×3), testing (×2), future-advancements
- [x] `images/vector_board.jpeg` — photo of the built board
- [x] `.github/workflows/build.yml` — CI compile-check
- [x] `README.md` — full project documentation
- [x] `LICENSE` (MIT) · `CONTRIBUTING.md` · `SECURITY.md` · `CODE_OF_CONDUCT.md` · `CHANGELOG.md`

---

## 9. Limitations & Future Scope

**Limitations**
- Passwords are 4-digit numeric; Bluetooth is transmitted **in the clear** (the
  LPC2148 has no crypto hardware) — a physical-security teaching project, not a
  bank vault (see [`SECURITY.md`](SECURITY.md)).
- The external battery-backed RTC and the future-advancements features are the
  only untested items — the RTC needs the chip + coin cell physically fitted.
- Motor pulse timing (`MOTOR_ROTATE_MS`) must be tuned to the gearbox.

**Future scope** (full roadmap: [`docs/future-advancements.md`](docs/future-advancements.md))
- Port to an **ESP32** — built-in Bluetooth *and* WiFi (replaces the HC-05),
  hardware AES, camera support.
- **One-time passwords (TOTP)** to replace the fixed Bluetooth password.
- **Biometrics:** fingerprint (R305/AS608 optical) and **face unlock**
  (ESP32-CAM), always keeping the keypad as a fallback.
- **IoT:** cloud access-log dashboard, MQTT/TLS, remote alerts.
- **Physical hardening:** reed-switch door status, PIR motion, solenoid latch,
  RFID/NFC badge.

---

## 10. Declaration

I declare that this project is my own work, carried out as part of my coursework.
External datasheets, tools and references used are credited in the project's
documentation (`docs/`, [`CONTRIBUTING.md`](CONTRIBUTING.md), [`SECURITY.md`](SECURITY.md)).

**Signature:** PAWAR SATHYANARAYANA   **Date:** 31 / 08 / 2026
