# Changelog

All notable changes to this project are documented here. The firmware follows
four recorded work rounds that map to the faculty review milestones of the
college project. Dates are the day the round was completed.

The authoritative, line-by-line engineering record lives in the original work
file `memory.txt` in the backup folder (`SecureLocker_Commented_v3/`); this
Changelog is the readable summary for the repository.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html)
loosely — the firmware has a single continuously-maintained build, so version
numbers track milestones rather than releases.

## [Unreleased]

### Added
- Repository restructure into an industry-grade GitHub layout: beginner
  README, register-level documentation (`docs/registers/`), module/flow docs
  (`docs/firmware/`), hardware + testing docs, CONTRIBUTING/fork guide,
  LICENSE (MIT), SECURITY policy, CI workflow.
- Vendored NXP device header (`firmware/vendor/LPC214x.h`) so the firmware
  compile-checks on any machine without Keil installed (and in CI).

### Changed
- **No firmware behaviour changed in this release.** This is packaging and
  documentation only. The source is byte-identical to Round 4.

## [0.4.0] - 2026-08-31 — Round 4

### Added
- **Battery-backed external RTC (DS1307 / DS3231).** `rtc.c` now probes I2C
  address `0xD0` at boot (`RTC_EXT_ENABLED`, default on). If a plausible time
  is read back it is loaded into the on-chip RTC — the working copy every
  reader uses — and every `rtc_set_time/date/dow()` write is mirrored to it.
  The external RTC ticks from its own 32.768 kHz crystal on the coin cell, so
  the time genuinely **survives a full power-off**.
- **30-second post-unlock clock window.** After a successful L1+L2 unlock the
  idle LCD shows the live clock for `POST_UNLOCK_RTC_DISPLAY_MS` (30 s), then
  returns to the "WAIT BT PWD / SEND PWD THEN #" prompt.

### Fixed
- **The clock came back at 12:00 PM after every power-off.** Two root causes:
  (a) `main()` unconditionally clobbered the RTC to `01-01-2024 12:00:00` on
  every boot — now removed, the default is applied **only when the on-chip RTC
  is completely unset**; (b) the LPC2148's on-chip RTC is PCLK-derived with no
  VBAT/RTCX pins and physically cannot tick while unpowered — solved by the
  external RTC. The boot log honestly reports which mode the system is in
  (`rtc_battery_backed()`).

### Hardware note
- Fitting a DS1307 or DS3231 on I2C0 (SCL P0.2 / SDA P0.3, same bus as the
  AT24C256 at `0xA0` — no conflict) is all that is needed; the firmware
  auto-detects it. Without the chip, the firmware stops destroying the RTC at
  boot (resets keep time) but a full power-off still loses the time.

## [0.3.0] - 2026-08-28 — Round 3

### Added
- **Full admin-menu audit logging** (the faculty approval condition): the
  interrupt-button menu (clock / password / alarm / set) is now monitored end
  to end on the RTC-timestamped UART0 audit log — entry, each of the four
  top-level options, `D` (exit), the CLK Time/Date/Day writes **with the exact
  value written**, the Alarm set/toggle/reset (with the value), and the
  Password L1/L2 selection plus outcome (tagged `(L1)` / `(L2)`).
- New `log_event2(msg, arg)` in `security.c` for appending a string value to a
  log line (no `printf` in this codebase).
- Deliberately **action-level, not keystroke-level**: passwords and keypad
  digits are never logged (masking discipline preserved).

### Fixed
- Four `//` comments in `defines.h` that silently broke the documented strict
  C89 compile-verify for every translation unit — converted to `/* */`.

## [0.2.0] - 2026-08-27 — Round 2

### Added
- **Two Level-2 timers with a live LCD countdown.** `L2_TOTAL_TIMEOUT_MS`
  (3 min) is a hard ceiling on the whole keypad entry; `L2_INTERKEY_TIMEOUT_MS`
  (1 min) restarts on **any** keypress and times out a user who walks away.
  Countdown in seconds lives in LCD columns 12–15; row 0 shows the smaller of
  the two remaining budgets, row 1 the total left.
- **A real millisecond time base** — Timer1 is a free-running 1 ms counter
  (`timebase_init()` / `millis()` / `elapsed_since()`). Every timeout in the
  project now measures against it, and the unsigned-subtraction wrap math is
  correct across the 49.7-day counter rollover.
- **Layered HC-05 boot test.** The unreliable "AT" probe was replaced by a
  UART1 **internal loopback test** (`U1MCR` LMS, four patterns 0x55/0xAA/0x0F/
  0xF0) that deterministically proves the pin/baud/format/FIFO/receive path.
  New 4-state result code: `BT_POST_UART_FAIL`, `BT_POST_MODULE_FAIL`,
  `BT_POST_LINK_OK`, `BT_POST_MODULE_OK`. Optional definitive module test via
  `BT_KEY_CTRL_ENABLED` (wire HC-05 KEY to P0.6).

### Fixed
- **HC-05 false "not configured" at every boot.** An unanswerable AT probe in
  data-mode wiring accused a healthy board of a fault. The loopback test proves
  what *can* be proven; the honest limit (an unconnected RXD1 is
  indistinguishable from an idle module) is documented.
- **Every remaining unbounded wait** was bounded: 5 keypad waits in menu.c's
  CLK/Alarm branches, `uart0_tx()/uart1_tx()` THRE spins, the UART1 ISR FIFO
  drain, and the I2C STOP wait. The only unbounded wait left is the deliberate
  PLL-lock spin.
- **`keypad_getkey()` deleted.** `keypad_getkey_timeout(0, …)` now means
  *"do not wait"* instead of *"wait forever"*; no keypad call can block forever.
- RTC reads could tear across a rollover (reporting an hour/day wrong) — now
  tear-free via `rtc_get()` on the atomic `CTIME0`/`CTIME1` registers.
- The admin menu wrote RTC registers with the clock running — now goes through
  the pausing `rtc_set_time/date/dow()`.
- Tamper detection was blind during the Level-2 entry / POST retries /
  lockout — new `tamper_poll()` (logs only) makes it visible everywhere.
- The alarm could be missed entirely when the main loop blocked — now
  **crossing-based**, it fires if the due time fell anywhere in the elapsed
  interval.
- Runtime EEPROM bus faults (wire works loose mid-run) now produce a
  `HARDWARE FAULT` log + `EEPROM FAULT / CHECK WIRING` screen instead of
  silently denying every password.
- Alarm banner was erased the instant it appeared; stale Bluetooth input after
  a Level-2 attempt could auto-satisfy Level-1; `input_value()` could overflow
  its display field; `system_lockout()` showed no progress and drifted (now an
  exact `millis()`-measured countdown).

## [0.1.0] - 2026-08-26 — Round 1

### Added
- **Boot POST** (`boot_self_test()`): probes the I2C EEPROM (presence via
  `I2C0STAT == 0x18`, then a non-destructive 0x5A/0xA5 read/write verify of the
  scratch byte at `0x0040`) and the Bluetooth HC-05 link. Failures show a
  wiring-guidance screen on the LCD + UART0, retried every 2 s; the EEPROM case
  blocks, the HC-05 case has a keypad override.
- **Level-2 keypad timeout** (3 min for the whole entry, counted via a shared
  decaying budget through `keypad_getkey_timeout()`).

### Fixed
- **The `1234#123456789` password bypass.** Root cause: the UART1 ISR silently
  discarded everything after `#`, so the firmware graded only the first 4
  characters. Fixed in three independent layers: (1) the ISR classifies the
  payload (`BT_RX_OK / _EMPTY / _OVERFLOW / _TRAILING`), (2) a settle window
  guarantees every byte is classified before judging, (3) `password_match()`
  enforces an exact length + full XOR compare with **no early exit** (constant
  time). Over-length payloads now log "exceeded the RX buffer size from BT
  level".
- Plaintext credentials were printed on the audit console — removed.
- Dead duplicate lockout implementation deleted; the live one now reads its
  threshold/duration from `defines.h`.
- Admin password fields accepted any key with no editing — new
  `read_menu_password()` (digits only, `*` backspace, `#` clear, per-field
  60 s timeout).
- Idle LCD hardcoded `SEND 1234#` — now the prompt `SEND PWD THEN #`.
- The "1:L1 2:L2" password selection prompt waited forever — bounded.
- `keypad_scan()` blocked on key release — bounded (`KEY_RELEASE_MAX_MS`).
- Every I2C wait made bounded (`I2C_WAIT_LIMIT`) so a shorted/busy bus can no
  longer hang boot forever.

## [0.0.1] - 2026-04 — Baseline

### Added
- Original firmware: two-factor locker (Bluetooth Level-1 + keypad Level-2),
  AT24C256 EEPROM passwords, L293D motor driver, 16×2 LCD, 4×4 keypad, buzzer,
  tamper switch, admin menu (merged from a separate "EnviroTime" project),
  RTC-timestamped UART0 audit log, on-chip RTC wall clock.
