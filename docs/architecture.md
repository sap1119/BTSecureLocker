# Architecture — How the Firmware Is Put Together

This page is the map. It tells you what each source file owns, how the chip is
brought up, and how the main loop's state machine works. Everything here was
read out of the actual source in `firmware/` — no behaviour is invented.

If you want to go *deeper*, each peripheral has a register-level page under
[`registers/`](registers/README.md).

## The module map

The firmware is 12 small C files plus headers, kept **flat** in `firmware/`
on purpose: the Keil project (`majorproject12.uvproj`) references each source
as a relative `.\file.c` path, so moving files into `src/`+`include/` would
break the working build. The structure is explained *here* instead.

| File | Owns | Talks to |
|---|---|---|
| `projectmain.c` | `main()`, PLL/clock bring-up, the main-loop state machine, Level-2 keypad entry, lockout, motor sequence | everything |
| `delay.c` | `delay_ms/us()` (Timer0), `millis()`/`elapsed_since()` (Timer1) | timers |
| `lcd.c` | 16×2 HD44780, 4-bit mode on P0.16–21 | P0 |
| `keypad.c` | 4×4 matrix scan on P1.16–23, debounce, release-wait | P1 |
| `uart.c` | UART0 (audit console) + UART1 (HC-05) line format, baud, FIFOs | UART0/1 |
| `bluetooth.c` | UART1 receive ISR, ring buffer, burst-settle, loopback self-test | UART1, VIC |
| `eeprom.c` | I2C0 bus + AT24C256 driver, probe + self-test | I2C0 |
| `rtc.c` | on-chip RTC + optional DS1307/3231 mirror, tear-free reads | RTC, I2C0 |
| `security.c` | tamper switch (P0.4), `password_match()`, audit-log `log_event()` | GPIO, UART0, RTC |
| `menu.c` | admin menu (EINT2 button), clock/alarm/password sub-menus | EINT2, LCD, keypad |
| `motor.c` | L293D H-bridge, IN1/IN2 on P1.24/25, forward/reverse/stop | P1 |
| `buzzer.c` | buzzer on P1.26, `on`/`off`/`alert` | P1 |

Shared constants live in **`defines.h`** (passwords, EEPROM map, Bluetooth
timing, lockout limits, LCD layout) and **`lcd_defines.h`** (LCD pin masks).
`types.h` supplies the `u8/u16/u32/s8/s16/s32` aliases. The only vendored
file is `vendor/LPC214x.h`, the NXP device header, so the repo compiles
without Keil.

## Boot sequence

Everything starts in `main()` (`projectmain.c`):

```
main()
│
├─ SystemInit_SecureLocker()          # 1. hardware bring-up
│    MAM off → PLL0 disabled → PLL0CFG=0x24 → PLL on → wait PLOCK
│    → PLL connect → VPBDIV=0 → MAM on            (60 MHz core / 15 MHz PCLK)
│    → timebase_init()                 # Timer1 ms time base FIRST
│    → lcd_init, uart0_init(9600), bluetooth_init(9600),
│      i2c_init, rtc_init, keypad_init, buzzer_init,
│      motor_init, security_init, admin_int_init
│
├─ LCD splash "Bluetooth / Secure System", UART0 banner
│
├─ boot_self_test()                   # 2. verify the hardware the locker
│    • EEPROM: eeprom_selftest() — HARD requirement; parks & retries if absent
│    • Bluetooth: UART1 internal loopback (deterministic) + AT probe (bonus)
│    • tamper switch polled even while parked on a fault screen
│
├─ ensure_default_passwords()         # 3. EEPROM provisioned only AFTER verified
│    "LKR1" marker missing? → write defaults (L1=1234, L2=5678)
│
├─ log "System booted" + RTC mode line  # 4. report clock state honestly
│
└─ while(1) → main loop state machine  # 5. below
```

The order is deliberate and load-bearing:

1. **`timebase_init()` runs first** among the drivers, because *every* timeout
   and the self-test itself measure elapsed time against Timer1, and its
   prescaler assumes the 15 MHz PCLK the PLL lines just settled.
2. **`boot_self_test()` runs before `ensure_default_passwords()`.** Without the
   EEPROM verified first, a missing device reads back as garbage, fails the
   `"LKR1"` magic check, and the firmware would "write defaults" into a device
   that is not there — then deny every password forever. See
   [main-flow.md](firmware/main-flow.md#the-post-and-why-it-prefixes-everything).
3. **The only unbounded wait in the whole firmware** is the PLL lock spin
   `while (!(PLL0STAT & (1<<10)));`. Every other wait is bounded. That one is
   deliberate: if the 12 MHz crystal is dead the PLL never locks, and running
   at the wrong frequency would silently invalidate *every* derived constant
   (UART divisors, timer prescalers, RTC prescaler, I²C clock). See
   [clocking.md](registers/clocking.md).

## The main-loop state machine

The locker is a single-threaded foreground loop with two interrupt-driven
inputs (UART1 receive and the admin button). Roughly:

```
while (1)
│
├─ admin_flag set?            → admin_menu()            (EINT2 pressed)
├─ check_tamper_and_alert()   → poll tamper switch; LCD+buzzer on a fresh edge
├─ check_alarm()              → ring buzzer if the alarm time was crossed
├─ (if not alarm_triggered) DisplayStandby()
│     idle clock window?  → live HH:MM:SS / DD/MM/YYYY DOW
│     else                → "WAIT BT PWD / SEND PWD THEN #"
│
├─ WAIT for a Bluetooth command:
│     loop while !bluetooth_available()
│       refresh live clock if in the post-unlock window
│       keep polling tamper + alarm + admin button every 100 ms
│
├─ bluetooth_settle()         → let the burst finish (trailing-junk check)
├─ bluetooth_read_command()  → snapshot + clear (IRQ masked during copy)
│
├─ CLASSIFY the attempt:
│     BT_RX_OVERFLOW  → deny, count a failed attempt
│     BT_RX_TRAILING  → deny, count a failed attempt   ("1234#1234...")
│     BT_RX_EMPTY     → ignore (a bare '#')
│     clean           → go to Level-1 check
│
├─ LEVEL-1: compare bt_cmd vs stored L1 (password_match, EEPROM fault-guarded)
│     wrong → "ACCESS DENIED", register_failed_attempt() → maybe lockout
│     right → "LEVEL1 OK / ENTER L2"
│
├─ LEVEL-2: read_keypad_password()    (dual timers + live countdowns)
│     idle-timeout (1 min) → abandon → back to Level-1 lock (NOT a failure)
│     total-timeout (3 min)→ abandon → back to Level-1 lock (NOT a failure)
│     done → compare vs stored L2 (password_match, fault-guarded)
│        wrong → deny + register_failed_attempt()
│        right → fail_count=0 → open_locker_sequence()
│
├─ open_locker_sequence()     → motor forward 500 ms → hold 5 s → reverse 500 ms
│     → first_access_done=1 → post-unlock clock window (30 s) starts
│
└─ bluetooth_clear()          → every attempt starts from a fresh Level-1
```

**The state machine has no "logged-in" state.** After a successful open the
system returns to the top of the loop waiting for a *fresh* Bluetooth command.
Level-1 and Level-2 are never cached across attempts.

## The security posture, in one paragraph

Each failed authentication (a wrong or malformed Level-1 **or** a wrong
Level-2) goes through `register_failed_attempt()`. Three consecutive failures
trigger `system_lockout()`: the receive buffer is discarded (so a password sent
during the lockout can't sneak through the instant it ends), the LCD shows
"SYSTEM LOCKED" with a live countdown, the tamper switch stays monitored, and
after 30 seconds the counter resets. Timeouts are **not** failures — walking
away from the keypad is not a guess — so they don't count toward the lockout.
The full flow is documented in [authentication.md](firmware/authentication.md).

## The two interrupt sources

| Source | VIC slot | ISR does | Main loop does |
|---|---|---|---|
| UART1 receive | 1 | drains FIFO into `bt_buffer`, sets `bt_rx_ready`, flags overflow/trailing | `bluetooth_read_command()` |
| EINT2 (admin button) | 2 | sets `admin_flag`, clears `EXTINT` | `admin_menu()` |

Both ISRs only raise flags and clear the pending source; all real work happens
in the foreground. Register-level details: [interrupts.md](registers/interrupts.md).

## Where each peripheral is configured (pin map)

The complete net map (every pin, function, and driver) is in
[gpio.md](registers/gpio.md). The one-line version:

- **LCD** P0.16–21 · **UART0** P0.0/1 · **I2C0** P0.2/3 · **tamper** P0.4 ·
  **admin/EINT2** P0.7 · **UART1/HC-05** P0.8/9
- **Keypad** P1.16–23 · **motor** P1.24/25 · **buzzer** P1.26
- **BT_KEY (optional)** P0.6, only when `BT_KEY_CTRL_ENABLED`

## Build & verification

- The build is a Keil uVision project (`majorproject12.uvproj`, target
  "Target 1", output `majorproject12.hex`).
- The repo also compiles without Keil: see
  [compile-verification.md](testing/compile-verification.md) for the exact
  `arm-none-eabi-gcc` command the CI workflow runs.
- Nothing here has ever been bench-tested on real hardware — where a value is
  hardware-dependent (e.g. `MOTOR_ROTATE_MS`), the code comment says so and
  tells you how to tune it.
