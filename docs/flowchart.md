# Visual Flowchart — The Entire Project, Drawn

Every behaviour of the locker, drawn as flowcharts: from "the user sends a
Bluetooth password" all the way to "the motor opens and the clock shows on the
LCD". This is the picture version of [architecture.md](architecture.md) and
[firmware/main-flow.md](firmware/main-flow.md) — read those for the words,
read this for the shapes.

> These charts are drawn from the actual source. Nothing is invented: every
> timeout value, LCD line and decision below is what `projectmain.c`,
> `bluetooth.c`, `keypad.c`, `security.c`, `menu.c` and `motor.c` really do.

## How to read the charts

```
[ PROCESS / ACTION ]     a box of work the firmware does
< decision ? >           a branch — two or more labelled exits
────▶ / ▼  /  ◀────────  flow arrows (down, right, and back-loop)
[ EEPROM @0x0010 ]       a data store (the AT24C256 addresses)
<< LCD: text >>          exactly what the 16×2 LCD shows
LOG: "text"              exactly what the UART0 audit log prints
```

---

## Chart 0 — The whole system in one picture (the big view)

```
                 ┌────────────┐      Bluetooth      ┌────────────┐
                 │   USER     │    ──HC-05 (10 m)──▶│   PHONE    │
                 │  (person)  │                     │   (app)    │
                 └────────────┘                     └─────┬──────┘
                                                          │ sends "1234#" over the air
                                                          ▼
                                                    ┌────────────┐
                                                    │   HC-05    │  ══► UART1 (P0.8/P0.9)
                                                    │  BT module │      receives the bytes
                                                    └────────────┘
                                                                     ┌──────────────────────┐
                 ┌────────────┐      4×4 keypad                     │                      │
                 │   USER     │    ── "5678" ───────▶    LPC2148 ────│   "the brain"        │
                 │  (person)  │    matrix scan         (60 MHz)      │   2-factor judge     │
                 └────────────┘                       P1.16–P1.23    │                      │
                                                                     └──────────┬───────────┘
                                                                    BOTH OK  │   (Chart 5 → 6)
                                                                              ▼
                                                              ┌───────────────────────────┐
                                                              │  MOTOR (L293D + DC motor)│
                                                              │  forward 500 ms ──► open  │
                                                              │  hold 5 s                 │
                                                              │  reverse 500 ms ──► close │
                                                              └─────────────┬─────────────┘
                                                                            │ after a success
                                            ┌───────────────────────────────┴──────────────────┐
                                            ▼                                                ▼
                              ┌────────────────────────────┐                 ┌────────────────────────────┐
                              │  RTC clock on the LCD      │                 │  AUDIT LOG  →  PC terminal  │
                              │  (30 s post-unlock window, │                 │  UART0 → MAX232 → DB9      │
                              │  then back to the prompt)  │                 │  every event, timestamped  │
                              └────────────────────────────┘                 └────────────────────────────┘

   also in the picture (monitored at ALL times):
     · TAMPER switch (P0.4)  ──► "enclosure opened" → alarm + log
     · ADMIN button (P0.7/EINT2) ──► opens the admin menu (Chart 10)
     · ALARM (menu-set time) ──► rings the buzzer when crossed
```

---

## Chart 1 — Boot sequence (power-on → main loop)

```
   POWER ON / RESET
        │
        ▼
┌───────────────────────────────────────────────┐
│ SystemInit_SecureLocker()                     │
│   · MAM off                                   │
│   · PLL: 12 MHz ──► 60 MHz (PLL0CFG=0x24)     │
│   · VPBDIV=0 ──► PCLK = 15 MHz                │
│   · PLL lock spin  (the ONE unbounded wait)   │
│   · MAM on                                    │
│   · timebase_init()      ← Timer1 FIRST       │
│   · lcd / uart0 / uart1 / i2c / rtc /         │
│     keypad / buzzer / motor / security /      │
│     admin-int  init                           │
└────────────────────┬──────────────────────────┘
                     ▼
┌───────────────────────────────────────────────┐
│ Splash screen + banner                        │
│   << Bluetooth >>   /   << Secure System >>   │
│   LOG: "Bluetooth Secure Locker Started"      │
└────────────────────┬──────────────────────────┘
                     ▼
┌───────────────────────────────────────────────┐
│ boot_self_test()   ────────────────►  Chart 2 │
│   EEPROM probe (HARD requirement)             │
│   Bluetooth link loopback                     │
└────────────────────┬──────────────────────────┘
                     ▼  (only if the POST PASSED)
┌───────────────────────────────────────────────┐
│ ensure_default_passwords()                    │
│   EEPROM has "LKR1" marker?                   │
│     NO ──► write defaults  L1=1234  L2=5678   │
│     YES ─► (leave the stored passwords alone) │
└────────────────────┬──────────────────────────┘
                     ▼
┌───────────────────────────────────────────────┐
│ LOG: "System booted"                          │
│ LOG: "RTC: ..."  ← honestly says which clock  │
│      mode is active (external / on-chip only) │
└────────────────────┬──────────────────────────┘
                     ▼
┌───────────────────────────────────────────────┐
│ while (1)  — the main loop        ►  Chart 3  │
└───────────────────────────────────────────────┘
```

---

## Chart 2 — The boot self-test (POST)

```
   boot_self_test()
        │
        ▼
┌─────────────────────────────────────────────┐
│ 1. EEPROM self-test (eeprom_selftest)       │
│    · probe for an ACK at I2C 0xA0           │
│    · write 0x5A to scratch @0x0040, read    │
│    · write 0xA5, read                       │
│      (a stuck bus reads 0xFF/0x00 and       │
│       can't pass BOTH patterns)             │
│    · restore the original byte              │
└──────────────────────┬──────────────────────┘
                PASS   │           FAIL
                        ▼
                  ┌──────────────────────────────────────────┐
                  │  << I2C EEPROM NOT / CONFIGURED >>       │
                  │  (wiring help on the LCD)                │
                  │  wait POST_RETRY_MS = 2 s                │
                  │  ⚠ tamper switch STILL polled here       │
                  └──────────────────┬───────────────────────┘
                                     │ retry loop (re-tests every 2 s,
                                     ▼ until the EEPROM answers —
               (no power cycle needed — plug it in and it recovers)
                                     ▲
        continue                     │
        │                            │
        ▼                            │
┌─────────────────────────────┐      │
│ 2. Bluetooth self-test      │      │
│    (bluetooth_selftest)     │      │
│    · UART1 INTERNAL LOOPBACK│      │
│      (U1MCR = 0x10) — no    │      │
│      module needed          │      │
└───────────┬─────────────────┘      │
            │                        │
        ┌───┴─── 4-state result ─────┴───► (any FAULT parks + retries
        │                                    like the EEPROM screen)
        ▼
  ┌─────────────────────┬──────────────────────┬─────────────────────┐
  ▼                     ▼                      ▼                     ▼
BT_POST_UART_FAIL   BT_POST_MODULE_FAIL   BT_POST_LINK_OK      BT_POST_MODULE_OK
 (0) UART1 loopback  (1) only if KEY      (2) link PROVEN,      (3) module actually
     broken → FAULT      wired; AT silent      module in data        answered "AT"
                         → FAULT               mode → **PASS**       → **PASS** (strongest)

        ▼                          ▼                       ▼
  park + retry               park + retry          return PASS
                                                  ──► ensure_default_passwords()
```

---

## Chart 3 — The main loop (idle → wait for Bluetooth)

```
   while (1)  — every pass:
        │
        ▼
   ┌─────────────────────────────────────────────┐
   │ admin_flag set ?   (EINT2 button pressed)   │
   └──────────────┬──────────────────────────────┘
            YES    │   NO
             ▼     ▼
   ┌────────────────────────┐
   │ admin_menu()  ► Chart 10│
   └────────────────────────┘
             ▼
   ┌─────────────────────────────────────────────┐
   │ check_tamper_and_alert()   (idle path)      │
   │   P0.4 LOW ──► "TAMPER ALERT" + buzzer + log│
   └────────────────────┬────────────────────────┘
                        ▼
   ┌─────────────────────────────────────────────┐
   │ check_alarm()   (crossing-based, buzzer)    │
   │   alarm_last_secs refreshed on every call   │
   └────────────────────┬────────────────────────┘
                        ▼
   alarm banner up ? ── YES ──► (skip repainting the standby screen)
        │ NO
        ▼
   ┌─────────────────────────────────────────────┐
   │ DisplayStandby()                            │
   │   post-unlock clock window (30 s) active?   │
   │     YES ──► live HH:MM:SS / DD/MM/YYYY DOW  │
   │     NO  ──► << WAIT BT PWD / SEND PWD THEN #│
   └────────────────────┬────────────────────────┘
                        ▼
   ┌─────────────────────────────────────────────┐
   │ WAIT for a Bluetooth command                │
   │   while (!bluetooth_available())            │
   │     · poll tamper / alarm / admin button    │
   │       every ~100 ms                         │
   │     · refresh the live clock IN PLACE       │
   │       (no flicker)                          │
   │     · if the 30 s window ends mid-wait,     │
   │       swap the frozen clock back to the     │
   │       "WAIT BT PWD" prompt                  │
   │     · bail out if the admin button fires    │
   └────────────────────┬────────────────────────┘
                        ▼
   ┌─────────────────────────────────────────────┐
   │ bluetooth_settle()                          │
   │   wait for the incoming burst to END, so    │
   │   trailing junk is CLASSIFIED, not guessed  │
   └────────────────────┬────────────────────────┘
                        ▼
   bluetooth_read_command()   (snapshot + clear,   )
                              (IRQ masked during copy)
                        │
                        ▼
                 classify  ► Chart 4
```

---

## Chart 4 — The Bluetooth classification ladder

```
   bluetooth_read_command()
        │
        ▼
   status = ?
        │
        ├── BT_RX_OVERFLOW   payload > 31 chars
        │       │
        │       ▼
        │   << ACCESS DENIED: exceeded the RX buffer size >>
        │   register_failed_attempt()  ► Chart 8
        │
        ├── BT_RX_TRAILING   extra chars AFTER the '#'   (e.g. "1234#123456789")
        │       │
        │       ▼
        │   << ACCESS DENIED: extra characters after the # >>
        │   register_failed_attempt()  ► Chart 8
        │
        ├── BT_RX_EMPTY   a bare '#' with no digits
        │       │
        │       ▼
        │   IGNORED — not an attempt (back to the wait)
        │
        └── CLEAN   exactly "1234#"
                │
                ▼
          Level-1 compare  ► Chart 5
```

> Why the ladder matters: every wrong branch is counted as **one failed
> attempt** (→ lockout at 3), but an empty command is *not* an attempt — you
> can't lock yourself out by sending a stray `#`.

---

## Chart 5 — Level-1 (Bluetooth password) check

```
   LEVEL-1 CHECK
        │
        ▼
   read stored L1 password from EEPROM @0x0010
        │
        ├── I2C bus fault?
        │       │
        │       ▼
        │   << EEPROM FAULT / CHECK WIRING >>
        │   (hardware screen — does NOT count toward the lockout)
        │
        ▼
   password_match(bt_command, stored_L1)
   ── exact length AND every character, XOR-compared
      (constant-time — never reveals HOW MUCH matched)
        │
      WRONG │                 │ RIGHT
            ▼                 ▼
   << ACCESS DENIED >>     << LEVEL1 OK / ENTER L2 >>
   LOG: "Level-1 Bluetooth          │
        password wrong"             ▼
   register_failed_attempt()   Level-2 keypad entry  ► Chart 6
        │
        ▼
   fail_count >= 3 ? ── YES ──► system_lockout()  ► Chart 8
        │ NO
        ▼
   back to idle  (bluetooth_clear — every attempt
                  starts from a fresh Level-1)
```

---

## Chart 6 — Level-2 (keypad) check with the dual timers

```
   LEVEL-2: read_keypad_password()
        │
        ▼
   arm TWO timers at once:
     · L2_TOTAL_TIMEOUT_MS   = 3 min   (hard ceiling)
     · L2_INTERKEY_TIMEOUT_MS= 1 min   (per character — restarts
                                         on ANY keypress)
        │
        ▼
   ╔═ LOOP ═════════════════════════════════════════════════╗
   ║  1. redraw the screen with a LIVE countdown            ║
   ║       row 0: << KEYPAD PWD: 45s >>   (time to timeout)  ║
   ║       row 1: << ****      TOT:172s >> (total left)      ║
   ║  2. keypad_scan() poll every KEY_POLL_MS                ║
   ║       (the caller polls — a single blocking call        ║
   ║        would freeze the countdown)                      ║
   ║                                                         ║
   ║  key = ?                                                ║
   ║   ├── '0'..'9' ──► append digit (masked as '*'),        ║
   ║   │                 restart the 1-min timer             ║
   ║   ├── '*'       ──► BACKSPACE (remove the last digit)   ║
   ║   ├── '#'       ──► CLEAR the whole entry               ║
   ║   ├── 'A'..'D'  ──► ignored in password entry           ║
   ║   ├── no key for 1 min ──► << NO KEY 60 SEC >>          ║
   ║   │              (NOT a failure)  ──► back to Level-1   ║
   ║   ├── 3 min total elapsed ──► << L2 TIMEOUT 3MIN >>     ║
   ║   │              (NOT a failure)  ──► back to Level-1   ║
   ║   └── 4 digits entered ──► EXIT the loop with the entry ║
   ╚═════════════════════════════════════════════════════════╝
        │
        ▼
   read stored L2 password from EEPROM @0x0020 (fault-guarded)
        │
        ▼
   password_match(entry, stored_L2)
        │
      WRONG │                 │ RIGHT
            ▼                 ▼
   << Wrong Level-2      fail_count = 0
      keypad password >>       │
   register_failed_attempt()   ▼
        │               open_locker_sequence()  ► Chart 7
        ▼
   fail_count >= 3 ? ── YES ──► system_lockout()  ► Chart 8
        │ NO
        ▼
   back to Level-1 (a fresh Bluetooth command is required)
```

> The two timeout causes are logged **separately** ("no key for 60 s" vs "3 min
> ceiling") and neither counts toward the lockout — walking away from the
> keypad is not a guess.

---

## Chart 7 — The motor sequence + the 30-second post-unlock clock

```
   open_locker_sequence()     (re-entrancy guard: locker_busy,
                                so only ONE cycle can run at a time)
        │
        ▼
   motor_stop() → motor_forward() → delay MOTOR_ROTATE_MS = 500 ms
        │
        ▼
   motor_stop() → << LOCKER OPEN / WAIT... >>
        │
        ▼
   delay LOCKER_OPEN_HOLD_MS = 5 s
        │
        ▼
   delay MOTOR_SETTLE_MS = 200 ms      ← full stop before reversing
        │
        ▼
   motor_reverse() → delay 500 ms → motor_stop()  → << Locker closed >>
        │
        ▼
   first_access_done = 1
   arm the post-unlock window:  POST_UNLOCK_RTC_DISPLAY_MS = 30 s
        │
        ▼
   back to the main loop
        │
        ▼
   for the next 30 s, DisplayStandby() shows the LIVE real-time
   clock instead of the prompt  →  then it reverts to
   << WAIT BT PWD / SEND PWD THEN # >>
        │
        ▼
   (before the FIRST successful access the idle screen
    never shows the clock — it stays on the password prompt)
```

```
   motor truth table (motor.c):
        IN1(P1.24)   IN2(P1.25)   result
            1            0        FORWARD  → latch OPENS
            0            1        REVERSE  → latch CLOSES
            0            0        STOP
   (L293D pin 1 / 1,2EN is tied to +5 V — this firmware has no ENABLE output)
```

---

## Chart 8 — Failed attempts and the lockout

```
   register_failed_attempt()     (called on ANY wrong L1 or L2,
                                   or a malformed L1 command)
        │
        ▼
   fail_count++
        │
        ▼
   fail_count >= MAX_WRONG_ATTEMPTS (3) ?
        │
     NO │          YES
        ▼           ▼
   back to idle   system_lockout()
                       │
                       ▼
   ┌──────────────────────────────────────────────────────┐
   │ 1. flush the Bluetooth receive buffer   (BEFORE)     │
   │ 2. << SYSTEM LOCKED / WAIT 30s >>                    │
   │    live seconds countdown on the LCD                 │
   │ 3. tamper switch STILL monitored throughout          │
   │ 4. wait LOCK_DURATION_MS = 30 s, measured against    │
   │    millis() (tamper alerts can't stretch it)         │
   │ 5. flush the Bluetooth receive buffer   (AFTER)      │
   │    → a password sent DURING the lockout can't act    │
   │      the instant it ends                             │
   └──────────────────────┬───────────────────────────────┘
                          ▼
                   fail_count = 0  → back to idle
```

---

## Chart 9 — Tamper detection and the alarm

```
   TAMPER  (P0.4, active-LOW, NC contact, 10 kΩ pull-up)
        │
        ▼
   P0.4 = HIGH ?        (lid shut, contact open)
        │
     YES │       NO
        ▼       ▼
   normal   ENCLOSURE OPEN (contact closed → pin to GND)
               │
               ▼
        tamper_poll()  →  LOG the event, return the flag
               │
               ├── caller owns the screen?  (Level-2 entry,
               │     POST retries, lockout)
               │        ──► flag ONLY — the caller shows it,
               │             so the countdown isn't wiped
               │
               └── idle path: check_tamper_and_alert()
                    ──► << TAMPER ALERT >> + buzzer + log

   ALARM  (admin-menu HH:MM:SS)
        │
        ▼
   has the RTC time crossed the alarm time?
   (alarm_last_secs is refreshed on EVERY check_alarm() call,
    including the early-return path — crossing-based, not exact)
        │
     NO │       YES
        ▼       ▼
   idle    ring the buzzer + keep the alarm banner up
```

---

## Chart 10 — The admin menu (audited, timed-out screens)

```
   ADMIN button (P0.7 / EINT2, falling edge)
        │
        ▼
   VIC ISR: set admin_flag, clear EXTINT     (ISR does NO real work)
        │
        ▼
   main loop sees admin_flag → admin_menu()
        │
        ▼
   LOG: "Admin menu opened ..."              ← audited
        │
        ▼
   top-level selection:
        │
        ├── CLK ──► set Time / Date / Day
        │       LOG the exact new value (fmt_hms / fmt_dmy)
        │       mirror the write to the external RTC
        │
        ├── Alarm ──► set / toggle / reset
        │       LOG the HH:MM:SS
        │
        ├── Pwd ──► choose Level-1 (Bluetooth) or Level-2 (keypad)
        │       LOG the outcome, tagged (L1) / (L2)
        │       ⚠ digits are NEVER logged (masking discipline)
        │
        └── Set / D ──► back / system info
        │
        ▼
   every admin screen times out after 1 minute
   ──► back to normal operation (no unbounded waits)
```

---

## Chart 11 — The two interrupt side-roads (what happens behind the scenes)

```
   UART1 receive ISR (VIC slot 1)              EINT2 ISR (VIC slot 2)
   ─────────────────────────────────           ─────────────────────────
   byte arrives from the HC-05                  admin button pressed
        │                                              │
        ▼                                              ▼
   CR/LF check FIRST  ◄── load-bearing:        admin_flag = 1
   phone apps send CR/LF after '#', so they           │
   must be classified BEFORE the rx-ready             ▼
   test, or every good password is rejected     clear EXTINT (re-arm)
        │                                              │
        ▼                                              ▼
   drain the FIFO into the ring buffer          (main loop acts on
   set bt_rx_ready                                 admin_flag → Chart 10)
   flag overflow / trailing junk
```

> Both ISRs only raise flags and clear the interrupt source. All real work
> happens in the foreground main loop — that's what keeps the firmware simple
> and race-free.

---

## Where the charts live in the code

| Chart | Main source file(s) |
|---|---|
| 0 — system overview | every module |
| 1 — boot | `projectmain.c` (`main()`, `SystemInit_SecureLocker()`) |
| 2 — POST | `projectmain.c` (`boot_self_test()`), `eeprom.c`, `bluetooth.c` |
| 3 — main loop | `projectmain.c` (`DisplayStandby()`, BT wait loop) |
| 4 — classification | `bluetooth.c` / `projectmain.c` |
| 5 — Level-1 | `projectmain.c`, `security.c` (`password_match()`) |
| 6 — Level-2 | `projectmain.c` (`read_keypad_password()`), `keypad.c`, `defines.h` |
| 7 — motor + clock | `projectmain.c`, `motor.c`, `defines.h` |
| 8 — lockout | `projectmain.c` (`register_failed_attempt()`, `system_lockout()`) |
| 9 — tamper + alarm | `security.c`, `menu.c` |
| 10 — admin menu | `menu.c` |
| 11 — ISRs | `bluetooth.c`, `menu.c` |

For the words behind these shapes: [architecture.md](architecture.md),
[firmware/main-flow.md](firmware/main-flow.md),
[firmware/authentication.md](firmware/authentication.md).
