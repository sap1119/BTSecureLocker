# CONNECTIONS — Bluetooth-Based Secure Locker with Access Logging

**Hardware connection reference for the Vector's Advanced Development Board for ARM7 (LPC2148), Series 1.1**

| Field | Value |
|---|---|
| Board | Vector's Advanced Development Board for ARM7 — Series 1.1 (vectorindia.org) |
| MCU | NXP/Philips LPC2148, ARM7TDMI-S, LQFP64 |
| Clocks | 12 MHz crystal → CCLK 60 MHz (PLL M=5, P=2), PCLK 15 MHz (VPBDIV=0) |
| Firmware this document matches | `SecureLocker_Commented_v3/SecureLocker_Commented/` — Keil project `majorproject12.uvproj`, round-4 (external RTC + 30 s post-unlock clock) |
| Document date | 2026-08-31 (round-4 revision — adds the optional DS1307/DS3231 external RTC) |
| Reference photo | `vector_board.jpeg` |

---

## ⚠️ 0. READ THIS FIRST — TWO CONFLICTING PIN MAPS EXIST

There are **two different versions of this project on this machine, and they use different pins.**
Wiring one and flashing the other gives a board that powers up and does nothing.

| Function | **THIS document** (`SecureLocker_Commented_v3`, the patched firmware) | Older folder (`Bluetooth-Based Secure Locker with Access Logging/PERIPHERALS.md`) |
|---|---|---|
| LCD | **P0.16–P0.21, 4-bit** (RS, EN, D4–D7) | P1.16–P1.26, 8-bit (+ RW) |
| Keypad rows | **P1.16–P1.19** | P0.4–P0.7 |
| Keypad columns | **P1.20–P1.23** | P0.12–P0.15 |
| Buzzer | **P1.26** | P0.17 |
| Motor IN1 / IN2 | **P1.24 / P1.25** | P0.18 / P0.19 |
| Motor ENABLE pin | **none — tie L293D pin 1 to +5V** | P0.20 |
| Admin button | **P0.7 → EINT2** | P0.16 → EINT0 |
| Tamper switch | **P0.4** | not documented |
| I2C EEPROM | P0.2 / P0.3 (same) | P0.2 / P0.3 |
| HC-05 | P0.8 / P0.9 (same) | P0.8 / P0.9 |
| UART0 log | P0.0 / P0.1 (same) | P0.0 / P0.1 |

**Wire according to THIS document if you are flashing the firmware in `SecureLocker_Commented_v3`** — that is the version with the boot self-test, the Level-2 dual timers and the on-screen countdown. Every pin in this document was read directly out of that source tree, not copied from any other document:

- `lcd_defines.h` → LCD pin masks
- `keypad.c` → `ROW_MASK` / `COL_MASK`
- `motor.c` → `MOTOR_IN1` / `MOTOR_IN2`
- `buzzer.c` → `BUZZER_PIN`
- `security.c` → `TAMPER_PIN`
- `menu.c` → `admin_int_init()` (P0.7 → EINT2)
- `eeprom.c` → `i2c_init()` (P0.2 / P0.3) and `EEPROM_ID` 0xA0
- `uart.c` → UART0 P0.0/P0.1, UART1 P0.8/P0.9
- `defines.h` → `BT_KEY_PIN_BIT` (optional P0.6)

---

## 1. MASTER PIN ASSIGNMENT

| LPC2148 pin | Direction | Signal | Connects to | On-board or external |
|---|---|---|---|---|
| P0.0 | Out | TXD0 | MAX232 → DB9 UART0 → PC terminal | On-board |
| P0.1 | In | RXD0 | MAX232 ← DB9 UART0 | On-board |
| P0.2 | Bidir (open-drain) | SCL0 | AT24C256 pin 6 (SCL) + DS1307/DS3231 SCL — shared bus | **External** |
| P0.3 | Bidir (open-drain) | SDA0 | AT24C256 pin 5 (SDA) + DS1307/DS3231 SDA — shared bus | **External** |
| P0.4 | In | Tamper switch | Enclosure switch → GND when opened | **External** (or board ACTIVE-LOW switch) |
| P0.5 | — | *free* | — | — |
| P0.6 | Out | HC-05 KEY/EN | **OPTIONAL**, only if `BT_KEY_CTRL_ENABLED` = 1 | **External** |
| P0.7 | In | Admin button → EINT2 | Push button → GND (falling edge) | On-board ACTIVE-LOW switch |
| P0.8 | Out | TXD1 | HC-05 **RXD** | **External** |
| P0.9 | In | RXD1 | HC-05 **TXD** | **External** |
| P0.10–P0.15 | — | *free* | — | — |
| P0.16 | Out | LCD RS | LCD pin 4 (RS) | On-board LCD header |
| P0.17 | Out | LCD EN | LCD pin 6 (EN) | On-board LCD header |
| P0.18 | Out | LCD D4 | LCD pin 11 (D4) | On-board LCD header |
| P0.19 | Out | LCD D5 | LCD pin 12 (D5) | On-board LCD header |
| P0.20 | Out | LCD D6 | LCD pin 13 (D6) | On-board LCD header |
| P0.21 | Out | LCD D7 | LCD pin 14 (D7) | On-board LCD header |
| P1.16 | Out | Keypad ROW 1 | Keypad row header R1 | On-board keypad |
| P1.17 | Out | Keypad ROW 2 | Keypad row header R2 | On-board keypad |
| P1.18 | Out | Keypad ROW 3 | Keypad row header R3 | On-board keypad |
| P1.19 | Out | Keypad ROW 4 | Keypad row header R4 | On-board keypad |
| P1.20 | In | Keypad COL 1 | Keypad column header C1 | On-board keypad |
| P1.21 | In | Keypad COL 2 | Keypad column header C2 | On-board keypad |
| P1.22 | In | Keypad COL 3 | Keypad column header C3 | On-board keypad |
| P1.23 | In | Keypad COL 4 | Keypad column header C4 | On-board keypad |
| P1.24 | Out | Motor IN1 | L293D pin 2 (1A) | **External** |
| P1.25 | Out | Motor IN2 | L293D pin 7 (2A) | **External** |
| P1.26 | Out | Buzzer | Buzzer drive (active HIGH) | On-board buzzer |

Everything else on the expansion headers (7-segment, ADC pot, LED banks, spare P0/P1 pins) is **unused by this firmware — leave it unwired.**

### Total wire count

| Group | Jumper wires |
|---|---|
| LCD | 6 signal (+ power/contrast, see §4) |
| Keypad | 8 |
| Buzzer | 1 |
| Admin button | 1 |
| Tamper switch | 1 (+ GND) |
| HC-05 | 4 (5 with the optional KEY wire) |
| AT24C256 EEPROM | 8 including power and pull-ups |
| DS1307/DS3231 RTC | 4–6 (shares SDA/SCL + pull-ups with the EEPROM; +2 for a DS1307 crystal) |
| L293D + motor | 8 including power |
| **Total** | **≈ 44 connections** |

---

## 2. BOARD ZONE MAP (as seen in `vector_board.jpeg`)

```
┌───────────────────────────────────────────────────────────────────────────┐
│  VECTOR'S ADVANCED DEVELOPMENT BOARD FOR ARM7 (LPC2148) — SERIES 1.1     │
│                                                                           │
│  ┌──────────────────────┐   ┌──────────────────────────────────────────┐  │
│  │ 16x2 LCD (mounted)   │   │  4x4 KEYPAD  (16 red tact switches)      │  │
│  │ + yellow contrast pot│   │  1 2 3 A                                  │  │
│  │ Header: D0..D7       │   │  4 5 6 B   Headers: R1..R4  and  C1..C4  │  │
│  │ Header: RS, EN       │   │  7 8 9 C                                  │  │
│  └──────────────────────┘   │  * 0 # D                                  │  │
│                              └──────────────────────────────────────────┘  │
│  ┌────────────────────────┐  ┌──────────────────┐  ┌───────────────────┐  │
│  │ SEGMENT (2x 7-seg)     │  │ ADC (yellow pot, │  │ BUZZER (black     │  │
│  │  NOT USED              │  │  R5/R6) NOT USED │  │  cylinder) USED   │  │
│  └────────────────────────┘  └──────────────────┘  └───────────────────┘  │
│                                                                           │
│  ┌──────────────────────┐   ┌──────────────────────────────────────────┐  │
│  │ ACTIVE LOW           │   │            LPC2148 MCU                    │  │
│  │  LED5..LED8          │   │                                           │  │
│  │  SW5..SW8  <── USE   │   │  Top header:    P0.15 ......... P0.0      │  │
│  │  (admin + tamper)    │   │  Bottom header: P0.31 ......... P0.16     │  │
│  ├──────────────────────┤   │  Right header:  P1.31 ......... P1.16     │  │
│  │ ACTIVE HIGH          │   │                                           │  │
│  │  LED1..LED4          │   │  12 MHz crystal on-board                  │  │
│  │  SW1..SW4  NOT USED  │   └──────────────────────────────────────────┘  │
│  └──────────────────────┘                                                 │
│                                                                           │
│  ┌───────────┐ ┌──────────┐  ┌──────────────────────────────────────────┐ │
│  │ POWER     │ │ RST  ISP │  │ MAX232 (ADM232L) + Tx/Rx/GND header      │ │
│  │ USB, ON/  │ │ buttons  │  │ ┌──────────┐          ┌──────────┐       │ │
│  │ OFF, LED  │ │ 5V/3.3V  │  │ │DB9 UART1 │          │DB9 UART0 │       │ │
│  │ regulator │ │ header   │  │ └──────────┘          └──────────┘       │ │
│  └───────────┘ └──────────┘  └──────────────────────────────────────────┘ │
└───────────────────────────────────────────────────────────────────────────┘
```

### On-board vs external

**Already on the board — only needs jumper wires from the CPU header:**
16x2 LCD (with contrast pot), 4x4 keypad, buzzer, MAX232 + two DB9 connectors, RST button, ISP button, USB power + ON/OFF switch + regulators, 12 MHz crystal, LED/switch banks.

**You must supply externally:**

| Module | Extra parts needed |
|---|---|
| **HC-05 Bluetooth** | module + 4 female–female jumpers |
| **AT24C256 EEPROM** | 8-pin DIP + breadboard + **2 × 4.7 kΩ** pull-ups |
| **DS1307/DS3231 RTC** (optional) | 8-pin DIP / 16-pin breakout + breadboard + Semos coin cell (+ 32.768 kHz crystal for the DS1307 only) |
| **L293D motor driver** | 16-pin DIP + breadboard |
| **DC gear motor** | 5–12 V motor for the latch |
| **Motor power supply** | separate 6–12 V supply / 9 V battery |
| **Tamper switch** | lever micro-switch or reed switch + 10 kΩ pull-up |

**There is no I2C EEPROM, no RTC, no motor driver and no Bluetooth module on this board** — those are the external modules (the RTC is optional; see §10).

---

## 3. POWER

```
USB Type-B  ──►  POWER SUPPLY section  ──►  ON/OFF switch  ──►  PWR LED lights
                        │
                        ├──► +5V  rail  (LCD VDD, buzzer, HC-05 VCC, L293D pin 16)
                        ├──► +3.3V rail (LPC2148 VDD, EEPROM VCC, I2C pull-ups)
                        └──► GND  rail  (common to everything, including the
                                         external breadboard AND the motor supply)
```

Rules:

1. **One common ground.** The board GND, the breadboard GND and the motor supply GND (L293D pins 4, 5, 12, 13) must all be tied together. A missing common ground is the single most frequent cause of "the EEPROM self-test fails" and "Bluetooth receives garbage".
2. **The motor gets its own supply** (6–12 V to L293D pin 8). Never run the motor from the board's 5 V rail — the current surge when the motor starts drops the rail and resets the LPC2148 mid-operation.
3. **3.3 V for the EEPROM and the I2C pull-ups**, matching what the firmware's POST message tells you to check (`VCC=3V3`).
4. The HC-05 breakout takes **5 V on VCC** (it has its own on-board 3.3 V regulator) but its **data pins are 3.3 V logic** — see §8.
5. The external RTC's coin cell is a **separate supply, independent of USB** — it is what keeps the clock ticking while the board is off. USB power does not charge it; fit a fresh cell in the RTC's VBAT holder.

---

## 4. 16x2 LCD — 4-BIT MODE (on-board)

The firmware drives the LCD in **4-bit mode**: only D4–D7 are used, D0–D3 are not connected at all.

| LCD pin | Name | Connect to |
|---|---|---|
| 1 | VSS | GND |
| 2 | VDD | +5V |
| 3 | V0 / VEE | Contrast pot wiper (the yellow trimmer beside the LCD) |
| 4 | RS | **P0.16** |
| 5 | R/W | **GND** (write-only — the MCU never reads the LCD) |
| 6 | EN | **P0.17** |
| 7–10 | D0–D3 | **leave unconnected** (4-bit mode) |
| 11 | D4 | **P0.18** |
| 12 | D5 | **P0.19** |
| 13 | D6 | **P0.20** |
| 14 | D7 | **P0.21** |
| 15 | A / LED+ | +5V (via 220 Ω if the board has no series resistor) |
| 16 | K / LED− | GND |

```
      LPC2148 (P0 header, bottom row)          LCD section header
      ─────────────────────────────────        ──────────────────
      P0.16 ──────────────────────────────────►  RS
      P0.17 ──────────────────────────────────►  EN
      P0.18 ──────────────────────────────────►  D4
      P0.19 ──────────────────────────────────►  D5
      P0.20 ──────────────────────────────────►  D6
      P0.21 ──────────────────────────────────►  D7
                                        GND ───►  R/W
```

Notes:

- The board's LCD header is silkscreened **D0…D7** plus a separate **RS, EN** pair. Use the **D4, D5, D6, D7** positions of that header — *not* D0–D3.
- If the board already ties the LCD's R/W to GND on the PCB, skip that wire.
- **Blank screen but the backlight is on** → the contrast pot is at one end. Turn it until the character blocks appear, then back off.
- `lcd_init()` sets `PINSEL1 &= ~0x00000FFF`, which forces P0.16–P0.21 to plain GPIO. No jumper on the board may connect these pins to any other peripheral.

---

## 5. 4x4 MATRIX KEYPAD (on-board)

The firmware holds all four rows HIGH, pulls **one row LOW** at a time, and reads the four columns; a pressed key shows up as a **LOW column**.

| Keypad header | LPC2148 pin | Direction |
|---|---|---|
| ROW 1 | **P1.16** | Output |
| ROW 2 | **P1.17** | Output |
| ROW 3 | **P1.18** | Output |
| ROW 4 | **P1.19** | Output |
| COL 1 | **P1.20** | Input |
| COL 2 | **P1.21** | Input |
| COL 3 | **P1.22** | Input |
| COL 4 | **P1.23** | Input |

Key layout the firmware expects (`key_map` in `keypad.c`, row-major):

```
        COL1   COL2   COL3   COL4
        P1.20  P1.21  P1.22  P1.23
ROW1  ┌──────┬──────┬──────┬──────┐
P1.16 │  1   │  2   │  3   │  A   │
ROW2  ├──────┼──────┼──────┼──────┤
P1.17 │  4   │  5   │  6   │  B   │
ROW3  ├──────┼──────┼──────┼──────┤
P1.18 │  7   │  8   │  9   │  C   │
ROW4  ├──────┼──────┼──────┼──────┤
P1.19 │  *   │  0   │  #   │  D   │
      └──────┴──────┴──────┴──────┘

Firmware meaning:  0-9 = digits      * = backspace
                   # = clear entry   A-D = ignored in password entry
                   In the admin menu: D = back/cancel, # = confirm
```

- **If the digits come out scrambled**, rows and columns are swapped or rotated. Swap the two 4-wire groups first, then rotate one group one position at a time.
- Column pull-ups: the LPC2148 has internal pull-ups on Port 1, which is normally enough. Add 10 kΩ to +3.3 V on each column if the keypad is on long wires or reads erratically.

---

## 6. BUZZER (on-board) — and a P1.26 warning that matters

| Buzzer | LPC2148 pin |
|---|---|
| Buzzer drive input | **P1.26** (active HIGH: HIGH = sound) |
| Buzzer − | GND |

**P1.26 is also the LPC2148's RTCK (JTAG) pin, and P1.20 is TRACESYNC.** On the LPC2148:

- If **P1.26 (RTCK) is held LOW while RESET is LOW**, pins P1.31–P1.26 come up as the **Debug port**.
- If **P1.20 (TRACESYNC) is held LOW while RESET is LOW**, pins P1.25–P1.16 come up as the **Trace port**.

Either condition would take away the buzzer, the motor pins and the whole keypad. So:

1. **Do not fit pull-down resistors on P1.26 or P1.20**, and do not let the buzzer driver or keypad wiring hold either pin low at reset. The LPC2148's internal pull-ups keep them HIGH if you leave them alone.
2. **Do not hold a keypad key down while powering up or pressing RST.**
3. This firmware defends itself anyway: `keypad_init()`, `motor_init()` and `buzzer_init()` each execute `PINSEL2 &= ~0x0000000CUL`, which clears the GPIO/DEBUG and GPIO/TRACE bits and returns those pins to GPIO after reset. The side effect is that **JTAG debugging over those pins is disabled while this firmware runs** — expected, and unavoidable, because the keypad shares them.

If the buzzer is loud enough to draw more current than a GPIO pin can supply, drive it through an NPN transistor (base resistor 1 kΩ to P1.26, emitter to GND, buzzer between +5 V and collector). **Do not add a base pull-down to GND** — see point 1.

---

## 7. ADMIN BUTTON (P0.7 / EINT2) AND TAMPER SWITCH (P0.4)

Both are **active LOW**. Use the board's **ACTIVE LOW** switch bank (SW5–SW8), which already connects the pin to GND when pressed and has an on-board pull-up.

### Admin button → P0.7

| Signal | Wire |
|---|---|
| ACTIVE LOW switch output (e.g. SW5) | **P0.7** |

`admin_int_init()` sets `PINSEL0 |= (3 << 14)`, making P0.7 the **EINT2** input, edge-sensitive, **falling edge**. One press raises `admin_flag`; the main loop then opens the admin menu (CLK setting / Alarm / Password change).

### Tamper switch → P0.4

| P0.4 level | Firmware reads it as |
|---|---|
| **HIGH** | Enclosure closed — normal |
| **LOW** | Enclosure open — **TAMPER ALERT** logged + buzzer |

Wiring, two options:

1. **Demo / lab:** wire a second ACTIVE LOW switch (e.g. SW6) to P0.4. **Pressing it = tamper**, which is the easiest way to demonstrate the alert.
2. **Real enclosure:** use the **NC (normally-closed) contacts** of a lever micro-switch, wired between P0.4 and GND, mounted so the *closed lid presses the lever*:
   - lid closed → lever pressed → NC opens → 10 kΩ pull-up holds P0.4 HIGH → normal
   - lid opened → lever releases → NC closes → P0.4 pulled to GND → tamper

```
        +3.3V
          │
        [10k]          (omit if using a board ACTIVE-LOW switch,
          │             which already has its own pull-up)
  P0.4 ───┤
          │
        ─/ ─  tamper contact: CLOSED when the enclosure is OPEN
          │
         GND
```

The tamper switch is now polled during the Level-2 keypad entry, during the boot self-test retry loops and during a lockout, so it stays live in exactly the situations where it used to be ignored.

---

## 8. HC-05 BLUETOOTH MODULE (external) — READ THE WARNINGS

| HC-05 pin | Connect to | Note |
|---|---|---|
| VCC | **+5V** | Breakout has its own 3.3 V regulator |
| GND | **GND** | Must be the board's common ground |
| **TXD** | **P0.9** (RXD1) | Module transmits → MCU receives |
| **RXD** | **P0.8** (TXD1) | MCU transmits → module receives |
| KEY / EN | **P0.6** | **OPTIONAL** — only if `BT_KEY_CTRL_ENABLED` = 1 in `defines.h` |
| STATE | not used | — |

```
   HC-05                        LPC2148 (P0 header, top row)
   ─────                        ───────────────────────────
   VCC  ──────── +5V
   GND  ──────── GND
   TXD  ─────────────────────►  P0.9   (RXD1)
   RXD  ◄─────────────────────  P0.8   (TXD1)
   KEY  ◄───── optional ───────  P0.6
```

### ⚠️ Warning 1 — never connect the HC-05 to a DB9 connector

The two DB9 sockets on this board are on the **RS-232 side of the MAX232**, swinging roughly ±12 V. The HC-05 is a 3.3 V TTL device. **Connecting it to a DB9 destroys it instantly.** The HC-05 goes only to the P0.8 / P0.9 pins on the CPU expansion header.

### ⚠️ Warning 2 — UART1 contention with the on-board MAX232

The board's second DB9 is wired to UART1 through the MAX232. **Before connecting the HC-05, confirm whether the MAX232's UART1 channel is permanently wired to P0.8/P0.9 on your board** (check the silkscreen and follow the traces, or measure continuity from the MAX232 pins to the P0.8/P0.9 header with the board powered off).

- If UART1 reaches the MAX232 **through a jumper/header**, remove that jumper before wiring the HC-05.
- If it is **hard-wired**, the MAX232 receiver output and the HC-05's TXD will both drive P0.9. That is a bus conflict: the level you read is whichever driver wins, and the Bluetooth password becomes unreliable. In that case use UART1 for the HC-05 **only**, and leave the UART1 DB9 cable disconnected — an unconnected RS-232 input keeps the MAX232 receiver output idle, which is usually enough. If it still misbehaves, lift the MAX232 pin.

The audit log on **UART0 / the other DB9 is unaffected** and can stay connected to the PC.

### Baud rate and behaviour

- The firmware programs UART1 for **9600 baud, 8N1** (`BT_DATA_BAUD`). Your HC-05 must be configured for 9600 (factory default on most modules).
- Phone app protocol: send the 4 digits then `#`, e.g. `1234#`. Trailing CR/LF from the app is ignored. Anything after the `#` (e.g. `1234#123456789`) is now **rejected**, not accepted.

### About the boot message "HC-05 IN DATA"

`BT LINK OK / HC-05 IN DATA` at boot is a **PASS**, not a fault. The firmware proves the UART1 link with an internal loopback test and reports honestly that it cannot interrogate the module: with **KEY/EN unwired the HC-05 is permanently in data mode and never answers `AT`** — it transmits those characters over the air instead. Only `BT UART1 NOT CONFIGURED` (loopback failed) is a real fault in the default wiring.

To get a definitive module-present/absent answer, add the single **KEY → P0.6** wire and set `BT_KEY_CTRL_ENABLED` to 1 in `defines.h`. The POST will then force command mode at 38400 baud, require an `OK`, and report `HC-05 BT NOT CONFIGURED` if the module really is missing.

Either way, the first time real data arrives the log prints
`HC-05 link confirmed: data received from the Bluetooth module`.

---

## 9. AT24C256 I2C EEPROM (external) — password storage

8-pin DIP, on a breadboard. **The locker cannot run without it** — the boot self-test blocks on the "I2C EEPROM NOT CONFIGURED" screen until it responds, because both passwords live here.

| EEPROM pin | Name | Connect to | Why |
|---|---|---|---|
| 1 | A0 | **GND** | Device address bit → gives slave address 0xA0 |
| 2 | A1 | **GND** | same |
| 3 | A2 | **GND** | same |
| 4 | GND | **GND** | — |
| 5 | SDA | **P0.3** (SDA0) | + 4.7 kΩ pull-up to +3.3 V |
| 6 | SCL | **P0.2** (SCL0) | + 4.7 kΩ pull-up to +3.3 V |
| 7 | WP | **GND** | Write protect OFF — **must be GND or passwords can never be saved** |
| 8 | VCC | **+3.3V** | — |

```
                         +3.3V
                           │
                 ┌─────────┴─────────┐
              [4.7k]              [4.7k]
                 │                   │
   P0.2 (SCL0) ──┴───────────────────┼──────► EEPROM pin 6 (SCL)
   P0.3 (SDA0) ──────────────────────┴──────► EEPROM pin 5 (SDA)

   AT24C256 (8-pin DIP, notch to the left)
   ┌──────────────┐
   │ A0 1 ● 8 VCC │──── +3.3V
   │ A1 2    7 WP │──── GND      ← write protect OFF
   │ A2 3    6 SCL│──── P0.2
   │GND 4    5 SDA│──── P0.3
   └──────────────┘
     └── pins 1,2,3,4 all to GND
```

- **The 4.7 kΩ pull-ups are mandatory.** I2C is open-drain; with no pull-ups SCL can never rise and the bus reads as dead. (The firmware survives this now — every I2C wait is bounded by `I2C_WAIT_LIMIT`, so it reports the fault instead of hanging at boot with a blank screen — but the EEPROM still will not work.)
- Bus speed is ~100 kHz (`I2C0SCLH`/`I2C0SCLL` = 75 at PCLK 15 MHz).
- Memory map used by the firmware:

| Address | Contents |
|---|---|
| 0x0000 | 4-byte magic marker `LKR1` |
| 0x0010 | Level-1 (Bluetooth) password, 4 digits — factory default `1234` |
| 0x0020 | Level-2 (keypad) password, 4 digits — factory default `5678` |
| 0x0040 | Scratch byte, used only by the boot self-test (0x5A / 0xA5, non-destructive) |

The self-test writes 0x5A and 0xA5 to the scratch byte and reads both back, so a bus stuck high (0xFF) or low (0x00) cannot fake a pass. The original byte is restored.

---

## 10. DS1307 / DS3231 EXTERNAL RTC (optional — the only way the clock survives a power-off)

An RTC IC on a breadboard, **sharing the SCL0/SDA0 bus with the EEPROM** (I2C address `0xD0` — no conflict with the EEPROM's `0xA0`).

**Why it exists.** The LPC2148's on-chip RTC is clocked from PCLK only and has **no RTCX1/RTCX2 crystal pins and no VBAT pin** — it freezes during a full power-off. The external RTC ticks from its own 32.768 kHz crystal on the coin cell while the board is dead, so after a power cycle the firmware restores the correct *current* time.

> ⚠️ **The EEPROM cannot do this job.** It can store a *snapshot* of the time, but it has no oscillator — read it back after a power-off and you get the stale moment it was written, not the current time. Only a real RTC chip keeps counting.

**Firmware side** — `RTC_EXT_ENABLED` (default **1**) in `defines.h`; `rtc.c`. At boot `rtc_init()` probes address `0xD0`; if a chip answers with a plausible time it is loaded into the on-chip RTC, and every clock write (admin menu CLK setting) is mirrored back to the chip. **It is auto-detected — wiring the chip up is all that is needed.** Set `RTC_EXT_ENABLED = 0` to disable it (then the on-chip RTC is the only clock and a power-off loses the time).

**Option A — DS1307 (8-pin DIP, needs an external 32.768 kHz crystal):**

| DS1307 pin | Name | Connect to | Why |
|---|---|---|---|
| 1 | X1 | **32.768 kHz crystal** | crystal terminal 1 |
| 2 | X2 | **32.768 kHz crystal** | crystal terminal 2 |
| 3 | VBAT | **Semos coin cell (+)** | powers the clock while the board is off (cell GND → GND) |
| 4 | GND | **GND** | — |
| 5 | SDA | **P0.3** (SDA0) | shares the EEPROM's pull-ups |
| 6 | SCL | **P0.2** (SCL0) | shares the EEPROM's pull-ups |
| 7 | SQW/OUT | leave open | square-wave output, unused |
| 8 | VCC | **+3.3V** | — |

**Option B — DS3231 (16-pin SOIC / breakout module, temperature-compensated, built-in crystal — no external crystal needed):**

| DS3231 pin | Name | Connect to |
|---|---|---|
| 1 | 32KHZ | leave open |
| 2 | VCC | **+3.3V** |
| 3 | INT/SQW | leave open |
| 4 | RST | +3.3V (already pulled up on most breakouts) |
| 5 | GND | **GND** |
| 6 | VBAT | **Semos coin cell (+)** (cell GND → GND) |
| 7 | SDA | **P0.3** (SDA0) |
| 8 | SCL | **P0.2** (SCL0) |
| 9–16 | N.C. | nothing |

```
                         +3.3V
                           │
                 ┌─────────┴─────────┐
              [4.7k]              [4.7k]        (the EEPROM's pull-ups are
                 │                   │           shared by the RTC)
   P0.2 (SCL0) ──┴───────────────────┼──────► EEPROM SCL  +  RTC SCL
   P0.3 (SDA0) ──────────────────────┴──────► EEPROM SDA  +  RTC SDA

   DS1307 (8-pin DIP, notch to the left)
   ┌──────────────┐
   │ X1 1 ● 8 VCC │──── +3.3V
   │ X2 2    7 SQW│──── (open)
   │VBAT 3    6 SCL│──── P0.2
   │GND 4    5 SDA│──── P0.3
   └──────────────┘
   VBAT ──── Semos coin cell (+)
```

Notes:

- **Both parts have a FIXED I2C address 0x68 (write byte 0xD0) — there are no address-select pins.** Only the EEPROM has A0–A2. You cannot re-address the RTC.
- **The coin cell is the whole point.** If VBAT is missing or the cell is dead, the external RTC also loses time on power-off, and the boot-time validation rejects the garbage and falls back to the 12:00 default. Use a fresh CR2032, or the Semos cell if it still holds charge.
- **The DS1307's oscillator starts on the first clock write.** Setting the clock in the admin menu writes the seconds register with the clock-halt bit cleared, which is what starts a brand-new DS1307 ticking. If a fresh chip reads garbage at boot, set the clock once.
- **A DS3231 is strongly preferred**: temperature-compensated (≈1 minute/year drift vs several seconds/month), no external crystal, and it works from a breakout module. If the choice is free, pick the DS3231.
- If your RTC breakout carries its own pull-ups, they simply join the EEPROM's 4.7 kΩ in parallel — still fine at ~100 kHz.
- On every boot the on-chip RTC is re-seeded from the external chip, so the displayed time always tracks it.

---

## 11. L293D + DC MOTOR (external) — the locker latch

| L293D pin | Name | Connect to |
|---|---|---|
| 1 | 1,2EN | **+5V** — tie HIGH permanently ⚠️ see note |
| 2 | 1A (input 1) | **P1.24** (Motor IN1) |
| 3 | 1Y (output 1) | DC motor terminal A |
| 4 | GND | GND |
| 5 | GND | GND |
| 6 | 2Y (output 2) | DC motor terminal B |
| 7 | 2A (input 2) | **P1.25** (Motor IN2) |
| 8 | VS / VCC2 | **Motor supply +6…12 V** (separate) |
| 9 | 3,4EN | not used — leave unconnected or tie GND |
| 12, 13 | GND | GND |
| 16 | VSS / VCC1 | **+5V** (logic supply) |

> ⚠️ **This firmware has no motor-ENABLE output.** It drives only IN1 and IN2. **L293D pin 1 must be tied to +5 V** or the motor will never turn. (The older document set used a third GPIO for ENABLE — that pin does not exist in this firmware.)

```
                    +5V        Motor supply (6-12V)
                     │              │
                  ┌──┴──────────────┴──┐
   P1.24 ────────►│2 (1A)      8 (VS) │
   P1.25 ────────►│7 (2A)     16 (VSS)│◄── +5V
     +5V ────────►│1 (1,2EN)          │
                  │3 (1Y) ────────────┼───► Motor terminal A
                  │6 (2Y) ────────────┼───► Motor terminal B
                  │4,5,12,13 GND ─────┼───► common GND
                  └───────────────────┘
                        L293D
```

Direction truth table (`motor.c`):

| IN1 (P1.24) | IN2 (P1.25) | Result |
|---|---|---|
| 1 | 0 | Forward — open the latch |
| 0 | 1 | Reverse — close the latch |
| 0 | 0 | Stop (both low) |

Sequence the firmware runs on a successful two-factor entry: forward pulse `MOTOR_ROTATE_MS` (500 ms) → stop → hold open `LOCKER_OPEN_HOLD_MS` (5 s) → settle `MOTOR_SETTLE_MS` (200 ms) → reverse pulse 500 ms → stop.

- **Tune `MOTOR_ROTATE_MS` in `defines.h` for your gearbox.** Start at 300–500 ms and increase until one pulse turns the latch about half a turn. Too long and the motor makes several full rotations.
- Solder a **100 nF ceramic capacitor across the motor terminals** to suppress brush noise. Motor noise coupling into the I2C or UART lines is a classic cause of random resets and EEPROM read failures.
- The L293D has internal clamp diodes, so no external flyback diodes are needed.
- **Common ground between the motor supply and the board is mandatory.**

---

## 12. UART0 → PC AUDIT LOG (on-board MAX232 + DB9)

| Setting | Value |
|---|---|
| Port | UART0, DB9 connector labelled **UART0** |
| Baud | **9600** |
| Format | 8 data bits, no parity, 1 stop bit, no flow control |
| Cable | RS-232 straight cable, or a USB-to-RS-232 adapter |
| Terminal | PuTTY / Tera Term / RealTerm — enable logging to a file to capture the audit trail |

P0.0 (TXD0) and P0.1 (RXD0) are already routed to the MAX232 on the PCB. Nothing to wire.

Every event is timestamped from the on-chip RTC: boot self-test results, each authentication attempt and its specific denial reason, Level-2 timeouts (stating which limit expired), tamper events, lockouts, admin actions, alarm triggers and EEPROM hardware faults. At boot the log also says which clock the system is running on — look for `RTC:` (e.g. `RTC: battery-backed external RTC found - real time restored`, or `RTC: no external RTC - on-chip clock cannot keep time across a power-off`).

---

## 13. WHAT THE LCD SHOWS (so you can confirm the wiring is right)

```
Boot:          SELF TEST...
               I2C EEPROM OK
               CHECK BT LINK
               BT LINK OK          <- normal, module in data mode
               HC-05 IN DATA

Idle:          WAIT BT PWD         (before the first successful access)
               SEND PWD THEN #
               ...after each successful open: a live clock for 30 s,
               then back to the password prompt

Level-1 OK:    LEVEL1 OK
               ENTER L2

Level-2 entry: KEYPAD PWD:  60s    <- seconds until timeout (top-right corner)
               **      TOT:175s    <- masked digits + total time left

Timeouts:      NO KEY 60 SEC   /  SEND BT PWD     (no keypress for 1 minute)
               L2 TIMEOUT 3MIN /  SEND BT PWD     (3-minute ceiling reached)

Denied:        ACCESS DENIED
Locked:        SYSTEM LOCKED   /  WAIT        30s (live countdown)
Granted:       ACCESS GRANTED → LOCKER OPEN → LOCKER CLOSE
Faults:        I2C EEPROM NOT / CONFIGURED
               BT UART1 NOT   / CONFIGURED
               EEPROM FAULT   / CHECK WIRING      (bus died while running)
Tamper:        TAMPER ALERT
```

---

## 14. BUILD CHECKLIST

Power off before every wiring change.

- [ ] Common ground: board GND ↔ breadboard GND ↔ motor supply GND
- [ ] LCD: RS→P0.16, EN→P0.17, D4→P0.18, D5→P0.19, D6→P0.20, D7→P0.21, R/W→GND, D0–D3 unconnected
- [ ] LCD power: VSS→GND, VDD→+5V, V0→contrast pot, backlight A/K
- [ ] Keypad rows: R1→P1.16, R2→P1.17, R3→P1.18, R4→P1.19
- [ ] Keypad columns: C1→P1.20, C2→P1.21, C3→P1.22, C4→P1.23
- [ ] Buzzer → P1.26, and **no pull-down** on P1.26 or P1.20
- [ ] Admin button (ACTIVE LOW bank) → P0.7
- [ ] Tamper switch → P0.4, pulled up, closes to GND when the enclosure opens
- [ ] EEPROM: A0/A1/A2→GND, GND→GND, SDA→P0.3, SCL→P0.2, WP→GND, VCC→+3.3V
- [ ] **Two 4.7 kΩ pull-ups** from SDA and SCL to +3.3 V
- [ ] (Optional) DS1307/DS3231 RTC: SDA→P0.3, SCL→P0.2, VCC→+3.3V, GND→GND, **fresh coin cell in VBAT**, crystal if DS1307 (§10)
- [ ] HC-05: VCC→+5V, GND→GND, TXD→P0.9, RXD→P0.8 — **not to any DB9**
- [ ] UART1/MAX232 contention checked (§8, Warning 2)
- [ ] L293D: pin 1→+5V, pin 2→P1.24, pin 7→P1.25, pin 16→+5V, pin 8→motor supply, pins 4/5/12/13→GND
- [ ] Motor across L293D pins 3 and 6, with a 100 nF capacitor
- [ ] Nothing wired to the 7-segment, ADC or LED sections
- [ ] No keypad key held down at power-up
- [ ] UART0 DB9 → PC, terminal open at 9600 8N1

---

## 15. FIRST POWER-ON TEST SEQUENCE

1. **Power only** — no external modules. PWR LED on, LCD backlight on. Adjust the contrast pot until blocks appear.
2. **Flash the firmware** (`majorproject12.uvproj` → `majorproject12.hex`) via ISP: hold ISP, tap RST, release ISP, then program with Flash Magic at 9600.
3. **Open the terminal** on UART0 at 9600 before resetting, so you capture the whole POST.
4. **Reset.** Expect `Bluetooth / Secure System`, then `SELF TEST...`.
5. **EEPROM stage** — expect `I2C EEPROM OK`. If it parks on `I2C EEPROM NOT CONFIGURED`, check in this order: the two 4.7 kΩ pull-ups, SDA/SCL not swapped, WP→GND, VCC→3.3 V, common ground. The screen clears by itself the moment the EEPROM answers — no power cycle needed.
6. **Bluetooth stage** — expect `BT LINK OK / HC-05 IN DATA`. `BT UART1 NOT CONFIGURED` means the MCU-side UART1 failed its internal loopback, which is independent of the module: check PCLK, the 9600 divisor and that nothing else is jumpered onto P0.8/P0.9.
7. **Keypad** — press keys during the Level-2 prompt and confirm one `*` appears per press. Scrambled digits mean rows/columns are swapped (§5).
8. **Level-1** — pair the phone (default PIN `1234` or `0000`) and send `1234#`. Expect `LEVEL1 OK / ENTER L2` and the log line `Level-1 Bluetooth password matched`.
9. **Countdown** — at the keypad prompt, watch the top-right corner count down from 60. Press one digit: it must snap back to 60 while `TOT:` keeps falling. Compare against a stopwatch — it should track within a second.
10. **Level-2** — type `5678`. Expect `ACCESS GRANTED`, the motor to pulse open, `LOCKER OPEN` for 5 s, then close.
11. **Timeouts** — send `1234#` and then do nothing: at 60 s expect `NO KEY 60 SEC`. Then send `1234#` and tap `A` every ~50 s: at 180 s expect `L2 TIMEOUT 3MIN`.
12. **Lockout** — three wrong passwords → `SYSTEM LOCKED` with a live 30-second countdown.
13. **Tamper** — press the tamper switch: `TAMPER ALERT` + buzzer + a log line. Do it during a Level-2 entry too; the password screen must come back with your digits intact.
14. **Admin** — press the admin button: the menu appears. Leaving any screen idle for 1 minute must return to normal operation on its own.
15. **RTC / power-off test (only if the external RTC is fitted)** — in the admin menu set the clock, kill the power, wait a minute or two, power back on. The clock must show the correct *advanced* time, and the log must print `RTC: battery-backed external RTC found`. Without the chip, expect `RTC: no external RTC` and the 12:00 default after a power-off (a mere reset, with power never lost, now keeps the time).

---

## 16. TROUBLESHOOTING

| Symptom | Likely cause | Fix |
|---|---|---|
| LCD backlight on, no characters | Contrast pot at an extreme | Turn the pot slowly |
| LCD shows garbage / half characters | D4–D7 order wrong, or wired to D0–D3 | Recheck §4 — D4→P0.18 … D7→P0.21 |
| LCD blank, nothing at all | R/W not grounded, or EN/RS swapped | Tie R/W to GND; RS→P0.16, EN→P0.17 |
| Parks on `I2C EEPROM NOT CONFIGURED` | Missing pull-ups, SDA/SCL swapped, WP high, no common ground | §9 |
| `EEPROM FAULT / CHECK WIRING` while running | I2C wire came loose, or motor noise | Reseat; add the 100 nF motor capacitor |
| Wrong keys / scrambled digits | Rows and columns swapped or rotated | §5 |
| A key repeats itself | Key held longer than `KEY_RELEASE_MAX_MS` | Normal; tap more briefly |
| Keypad dead, buzzer dead, motor dead | P1.20 or P1.26 held LOW at reset → trace/debug port | Remove pull-downs; don't hold a key at power-up (§6) |
| `BT UART1 NOT CONFIGURED` | MCU-side UART1 fault, or something else jumpered to P0.8/P0.9 | §8 Warning 2 |
| `BT LINK OK / HC-05 IN DATA` | **Not a fault** — normal with KEY unwired | Nothing to fix (§8) |
| Bluetooth password never accepted | HC-05 not at 9600, TXD/RXD swapped, or MAX232 contention | §8 |
| Bluetooth gives garbage characters | Baud mismatch or no common ground | Set 9600; tie grounds |
| `Level-1 DENIED: extra characters after the # terminator` | The app is sending more than `1234#` | Send exactly 4 digits + `#`; CR/LF alone is fine |
| Board resets when the motor runs | Motor powered from the board's 5 V | Give the motor its own 6–12 V supply |
| Motor never turns | L293D pin 1 (1,2EN) not tied to +5 V | Tie it HIGH — this firmware has no ENABLE output |
| Motor spins many turns | `MOTOR_ROTATE_MS` too large | Reduce it in `defines.h` |
| Motor hums, doesn't move | Motor supply too weak, or outputs on the wrong pins | Check pin 8 supply; motor across pins 3 and 6 |
| No text in the PC terminal | Wrong COM port or baud | 9600 8N1 on the **UART0** DB9 |
| Admin button does nothing | Wired to the ACTIVE HIGH bank | Use an ACTIVE LOW switch — EINT2 is falling-edge |
| Permanent `TAMPER ALERT` | Tamper contact closed with the lid shut, or P0.4 floating | Invert the contact; add the 10 kΩ pull-up |
| Clock back at 12:00 after a power-off | No external RTC fitted, or coin cell dead | Fit the DS1307/DS3231 + fresh cell (§10); a plain reset now keeps the time |
| Log says `RTC: no external RTC` but a chip is wired | SDA/SCL swapped, no VCC, dead cell, or chip not at 0x68/0xD0 | Recheck §10; both parts are fixed at 0x68 (write 0xD0) |
| Clock a few minutes slow/fast per month | DS1307 crystal drift (normal) | Acceptable, or use the DS3231 |

---

## 17. ITEMS TO VERIFY ON YOUR PHYSICAL BOARD

These could not be established from a photograph. Check them against the silkscreen or with a multimeter (board powered off, continuity mode) before you trust this document on those points:

1. **Exact pin order of the LCD header** — confirm which physical pin is D4 vs D7, and whether R/W is already grounded on the PCB.
2. **Exact pin order of the keypad row/column headers** — the silkscreen text is not fully legible in the photo. Identify rows vs columns by continuity: within a row, all four keys share one line.
3. **Whether the MAX232 is hard-wired to P0.8/P0.9 (UART1)** — the single most important item, see §8 Warning 2.
4. **The buzzer's drive arrangement** — direct pin drive or a transistor, and whether any pull-down is fitted on P1.26.
5. **The 5V / 3.3V selection header** near the ISP button — what it feeds, and where a clean +3.3 V is available for the EEPROM and its pull-ups.
6. **Whether the keypad columns already have on-board pull-ups.**
7. **Which ACTIVE LOW switch positions are free** for the admin and tamper inputs.
8. **Whether a coin-cell holder and a clean +3.3 V point exist** near the I2C bus for the external RTC (§10) — the board's Semos cell has to reach the RTC's VBAT.

Nothing in this document has been tested on hardware. The firmware it describes has been compile-verified only (all 12 translation units, zero warnings) — the pin assignments are exact because they were read out of the source, but the board-side header details above still need a visual/continuity check.

---

## 18. PIN-LEVEL CIRCUIT DIAGRAM (drawn pin-exact)

Drawn from `vector_board.jpeg` and the pin assignments in §1 (which were read out of the source, not copied). This is the complete circuit, at pin level, presented the way a schematic tool lays a design out:

- **One symbol per component**, with the component's *own* pin numbers (LCD pin 11, L293D pin 2, AT24C256 pin 6, …).
- **Net labels** `[NAME]` instead of long drawn wires. A net label that appears more than once — on any sheet — is the *same wire*. This is exactly how Proteus/ISIS holds board-level connections.
- A shared ground symbol `┴` and named power rails (`+5V`, `+3.3V`, motor rail, `VBAT`).
- Six sheets, one per functional group. **On-board** parts are the ones visible in the photo; everything else is the external breadboard.

> ⚠ **Honest note.** This is an ASCII rendering, so it cannot reproduce Proteus's graphics. But it *is* the net list a Proteus design would hold, pin for pin — §18.7 converts it to the plain-text net list you can type straight into ISIS/ARES. What the photo shows is marked **ON-BOARD**; everything else is what you wire up.

### 18.0 Component designators used on the sheets

| Designator | Part | Where |
|---|---|---|
| U1 | LPC2148 (LQFP64) | on-board |
| U2 | HD44780 16x2 LCD | on-board (mounted) |
| U3 | 4x4 matrix keypad | on-board (mounted) |
| U4 | Buzzer (active) | on-board (mounted) |
| U9 | MAX232 (ADM232L) → J6 DB9 UART0 | on-board |
| SW1 | Admin button — ACTIVE-LOW bank (SW5–SW8) | on-board |
| X1 | 12 MHz crystal (CCLK via PLL) | on-board |
| R6 | LCD contrast pot (yellow trimmer) | on-board |
| J6 | DB9 UART0 → PC audit log | on-board |
| U5 | HC-05 Bluetooth module | **external** |
| U6 | AT24C256 EEPROM (8-pin DIP) | **external** |
| U7 | DS1307 (8-pin DIP) or DS3231 RTC | **external** |
| U8 | L293D motor driver (16-pin DIP) | **external** |
| M1 | DC gear motor (the latch) | **external** |
| BT1 | Coin cell — Semos / CR2032 | **external** |
| SW2 | Tamper lever / reed switch (NC) | **external** |
| X2 | 32.768 kHz crystal (DS1307 only) | **external** |
| Q1 | NPN 2N3904 (buzzer driver, optional) | **external** |
| R1, R2 | 4.7 kΩ I2C pull-ups | **external** |
| R3 | 10 kΩ tamper pull-up | **external** |
| R4 | 1 kΩ buzzer base resistor (optional) | **external** |
| R5 | 220 Ω LCD backlight series resistor | external/on-board |
| C1 | 100 nF motor-noise snubber | **external** |
| C2, C3 | 100 nF VCC decoupling | **external** |

Drawing conventions:

```
●   junction on a pin (this is where the wire goes)   │ ─ ┬ ┴ ┤ ├ └ ┘ drawn wire
[X] net label — same label anywhere = same wire       ═╪═ wires cross, no joint
──┴─ GND     ground symbol        ──┬─ +5V / +3.3V    power rail symbol
SW  momentary push-button          ○  key contact
```

---

### SHEET 1/6 — SYSTEM OVERVIEW (how the sheets join at U1)

```
        ┌──────────────────────────────┐
        │  SHEET 2 — ON-BOARD GPIO     │◄── P0.16–P0.21  (LCD)
        │  LCD · keypad · buzzer ·     │◄── P1.16–P1.23  (keypad)
        │  admin · tamper              │◄── P1.26         (buzzer)
        └──────────────┬───────────────┘◄── P0.7 P0.4     (switches)
                       │
        ┌──────────────┴───────────────┐
        │  SHEET 3 — SERIAL BUSES      │◄── P0.8 P0.9     (UART1 → HC-05)
        │  HC-05 · EEPROM · RTC        │◄── P0.2 P0.3     (I2C0 → EEPROM + RTC)
        └──────────────┬───────────────┘
                       │
                 ┌─────┴──────┐
                 │   U1 LPC2148 │── P0.0 P0.1 ──► SHEET 4 (MAX232 → UART0 DB9)
                 │   (core)   │── VDD / VSS  ──► SHEET 6 (power rails)
                 │           │
                 └─────┬──────┘
                       │
        ┌──────────────┴───────────────┐
        │  SHEET 5 — MOTOR DRIVE       │◄── P1.24 P1.25  (L293D IN1 / IN2)
        │  L293D · DC gear motor       │
        └──────────────┬───────────────┘
                       │
        ┌──────────────┴───────────────┐
        │  SHEET 6 — POWER RAILS       │── +5V · +3.3V · GND to every sheet
        │  USB · motor supply · cell   │── VBAT ──► sheet 3 (RTC only)
        └──────────────────────────────┘
```

---

### SHEET 2/6 — ON-BOARD GPIO GROUP: LCD · KEYPAD · BUZZER · SWITCHES

```
 U1 LPC2148 (pins as listed in §1)
 ──────────────────────────────────
   P0.16 ●──[LCD_RS]      P1.16 ●──[KP_R1]      P1.20 ●──[KP_C1]
   P0.17 ●──[LCD_EN]      P1.17 ●──[KP_R2]      P1.21 ●──[KP_C2]
   P0.18 ●──[LCD_D4]      P1.18 ●──[KP_R3]      P1.22 ●──[KP_C3]
   P0.19 ●──[LCD_D5]      P1.19 ●──[KP_R4]      P1.23 ●──[KP_C4]
   P0.20 ●──[LCD_D6]      P1.26 ●──[BUZ]
   P0.21 ●──[LCD_D7]      P0.7  ●──[ADMIN]
                          P0.4  ●──[TAMPER]

 U2 — 16x2 LCD module, 4-BIT MODE        (numbers = the LCD's own pins)
 ─────────────────────────────────────────────────────────────────────
   ┌────────────────────────────────────────────────────────────┐
   │ 1 VSS ──────┴ GND                                           │
   │ 2 VDD ──────┬ +5V                                           │
   │ 3 V0  ──────[R6 contrast pot]──┬ +5V                        │
   │ 5 R/W ──────┴ GND (write-only)  └──── GND                   │
   │ 4 RS  ◄── [LCD_RS]    6 EN  ◄── [LCD_EN]                    │
   │ 7..10 D0–D3 ────── (open — 4-bit mode, do NOT connect)      │
   │ 11 D4 ◄── [LCD_D4]    12 D5 ◄── [LCD_D5]                    │
   │ 13 D6 ◄── [LCD_D6]    14 D7 ◄── [LCD_D7]                    │
   │ 15 A  ── [R5 220 Ω] ──┬ +5V   16 K ──────┴ GND              │
   └────────────────────────────────────────────────────────────┘

 U3 — 4x4 MATRIX KEYPAD                (each ┤ is a push-button:
 ──────────────────────────             pressing it shorts row ↔ column)
         [KP_C1]   [KP_C2]   [KP_C3]   [KP_C4]
            │         │         │         │
 [KP_R1] ───┤─────────┤─────────┤─────────┤──   R1 → keys  1  2  3  A
            │         │         │         │
 [KP_R2] ───┤─────────┤─────────┤─────────┤──   R2 → keys  4  5  6  B
            │         │         │         │
 [KP_R3] ───┤─────────┤─────────┤─────────┤──   R3 → keys  7  8  9  C
            │         │         │         │
 [KP_R4] ───┤─────────┤─────────┤─────────┤──   R4 → keys  *  0  #  D
            │         │         │         │

 U4 — BUZZER, active HIGH on P1.26
 ─────────────────────────────────
   Direct drive (quiet buzzer):
      [BUZ] ● ──────────────────► U4(+) ── U4(−) ──┴ GND

   Transistor drive (recommended for a loud buzzer — §6):
      [BUZ] ● ──[R4 1 kΩ]── Q1 B (2N3904)
                              Q1 E ──────┴ GND
                              Q1 C ── U4(−) ── U4(+) ──┬ +5V
      ⚠ NO pull-down on [BUZ] or on P1.20 — see §6 (RTCK / TRACESYNC).

 SW1 — ADMIN BUTTON → P0.7 / EINT2   (board ACTIVE-LOW bank, e.g. SW5)
 ─────────────────────────────────────────────────────────────────────
      [ADMIN] ● ────┬─── [on-board pull-up] ─── +3.3V
                    │
                  ──┤──●── SW1   (press = pin to GND, falling edge → menu)
                    │
                   GND

 SW2 — TAMPER SWITCH → P0.4          (real enclosure: NC lever switch)
 ─────────────────────────────────────────────────────────────────────
      [TAMPER] ● ────┬─── [R3 10 kΩ] ─── +3.3V
                     │
                   ──┤──●── SW2 NC    (CLOSED when the lid is OPEN;
                     │                 lid shut = lever pressed = opens)
                    GND
```

---

### SHEET 3/6 — SERIAL BUSES: UART1 (HC-05) · I2C (EEPROM + RTC)

```
 U5 — HC-05 Bluetooth module (external)
 ──────────────────────────────────────
      VCC ● ──── +5V         (module has its own 3.3 V regulator)
      GND ● ──── GND         (must be the board's COMMON ground)
      RXD ● ◄── [TXD1]       (from U1 P0.8)
      TXD ● ──► [RXD1]       (to   U1 P0.9)
      KEY ● ◄── [BT_KEY]     (OPTIONAL — only if BT_KEY_CTRL_ENABLED = 1)
   STATE ● ──── (open)
      ⚠ NEVER to a DB9 — the MAX232 swings ±12 V and will destroy it (§8).

 U6 — AT24C256 EEPROM, 8-pin DIP (external)
 ──────────────────────────────────────────
     8 VCC ● ──── +3.3V
     7 WP  ● ──── GND         (write-protect OFF — passwords must save)
     6 SCL ● ◄── [SCL0]
     5 SDA ● ◄── [SDA0]
     4 GND ● ──── GND
     3 A2  ● ──── GND         ┐
     2 A1  ● ──── GND         ├ slave address 0xA0  (all three low)
     1 A0  ● ──── GND         ┘

 U7 — RTC: DS1307 (8-pin DIP, shown) or DS3231 (16-pin breakout)
 ────────────────────────────────────────────────────────────────
    DS1307:                      DS3231:
     8 VCC ●── +3.3V              2 VCC ●── +3.3V
     7 SQW ●── (open)             3 INT/SQW ●── (open)
     6 SCL ●◄── [SCL0]            4 RST  ●── +3.3V (already pulled up)
     5 SDA ●◄── [SDA0]            8 SCL  ●◄── [SCL0]
     4 GND ●── GND                7 SDA  ●◄── [SDA0]
     3 VBAT●── BT1 (+)            5 GND  ●── GND
     2 X2  ●── [X2] 32.768 kHz    6 VBAT ●── BT1 (+)
     1 X1  ●── [X2] crystal       1, 9–16 ──── N.C. (X2 not needed —
        (X2 only for the DS1307)   crystal is inside the DS3231)

 I2C0 BUS — shared by U6 and U7, open-drain, one pull-up pair:
 ─────────────────────────────────────────────────────────────
      [SCL0] ●──[R1 4.7 kΩ]──┬ +3.3V
      [SDA0] ●──[R2 4.7 kΩ]──┘
      (both pull-ups live on the breadboard, feeding both devices)
      BT1 coin cell:  BT1(+) ──► U7 VBAT      BT1(−) ──┴ GND
```

---

### SHEET 4/6 — U1 CORE: POWER PINS · 12 MHz CRYSTAL · RESET/ISP · UART0

```
 U1 — LPC2148 (LQFP64), only the pins used by this firmware
 ───────────────────────────────────────────────────────────
   VDD (all pins)    ● ──── +3.3V     decouple each with [C2][C3] 100 nF
   VSS (all pins)    ● ──── GND
   XTAL1  ● ──── [X1] 12 MHz crystal ─┐   ON-BOARD — nothing to wire
   XTAL2  ● ──── [X1]                ─┘   (2 × ~22 pF load caps on-board)
   RST    ● ──── [RST button] + 10 kΩ → +3.3V      ON-BOARD
   P0.14  ● ──── [ISP button] + 10 kΩ → +3.3V      ON-BOARD (flash via ISP)
   P0.0   ● ──── [TXD0] ──► U9 MAX232 ──► J6 DB9 UART0 ──► PC    ON-BOARD
   P0.1   ● ──── [RXD0] ◄── U9 MAX232 ◄── J6 DB9 UART0           nothing
      (RTCX1 / RTCX2 / VBAT do not exist on this chip — its RTC is
       PCLK-only and freezes on power-off. The time survives ONLY
       through U7 on sheet 3; the Semos cell on the board feeds U7's
       VBAT, not the chip.)
```

---

### SHEET 5/6 — MOTOR DRIVE: U8 L293D + M1 DC GEAR MOTOR

```
 U8 — L293D (16-pin DIP) + M1 DC gear motor (the latch)
 ───────────────────────────────────────────────────────
      pin  1 (1,2EN) ● ──── +5V        ⚠ tie HIGH — this firmware has
      pin  2 (1A)     ● ◄── [MOT_IN1]     no ENABLE output (§11)
      pin  3 (1Y)     ● ──► M1 A ──┐
      pin  4 (GND)    ● ──── GND   ├── [C1 100 nF] across the motor
      pin  5 (GND)    ● ──── GND   └─────────────── (noise snubber)
      pin  6 (2Y)     ● ──► M1 B
      pin  7 (2A)     ● ◄── [MOT_IN2]    (from U1 P1.25)
      pin  8 (VS)     ● ──── motor supply +6..12 V  (SEPARATE supply)
      pin  9 (3,4EN)  ● ──── (open)
      pin 12 (GND)    ● ──── GND
      pin 13 (GND)    ● ──── GND
      pin 16 (VSS)    ● ──── +5V        (logic supply, board 5 V)

   Direction truth table (motor.c):  [MOT_IN1]=1,[MOT_IN2]=0 → open
                                      [MOT_IN1]=0,[MOT_IN2]=1 → close
   Common ground between the motor supply and the board is MANDATORY.
```

---

### SHEET 6/6 — POWER DISTRIBUTION (the five rails and what rides each)

```
 USB Type-B ──► on-board supply ──► ON/OFF switch ──► PWR LED
                      │
   +5V RAIL  ── board 5 V ── rides:  U2 LCD VDD + backlight A (via R5)
   │                                U4 buzzer (collector side, Q1 option)
   │                                U5 HC-05 VCC
   │                                U8 pin 16 (VSS)  +  pin 1 (1,2EN)
   │
   +3.3V RAIL ── board 3.3 V ── rides: U1 VDD (all)      U6 pin 8 (VCC)
   │                                 U7 VCC              R1, R2 (I2C pull-ups)
   │                                 R3 (tamper pull-up)
   │
   GND RAIL ── ONE common ground ── rides: EVERYTHING — board GND,
   │    breadboard GND and motor-supply GND MUST be tied together.
   │    A missing common ground = EEPROM POST fails + BT garbage.
   │
   MOTOR RAIL (+6..12 V, separate supply) ── rides: U8 pin 8 (VS) ONLY.
   │    Never the board 5 V rail (start surge resets the LPC2148).
   │
   VBAT (Semos / CR2032 coin cell) ── rides: U7 VBAT ONLY.
        Independent of USB — this is what keeps the clock ticking
        while the board is unpowered. Not charged by USB.
```

---

### 18.7 MASTER NET LIST (the definitive pin-connection table)

Every net in the schematic, with the exact pins on it. This is what you type into Proteus.

| Net | U1 LPC2148 | Also connects to |
|---|---|---|
| TXD0 | P0.0 | U9 MAX232 → J6 DB9 UART0 → PC (on-board) |
| RXD0 | P0.1 | U9 MAX232 ← J6 DB9 UART0 (on-board) |
| SCL0 | P0.2 | R1 4.7 kΩ→+3.3V · U6 pin 6 · U7 SCL |
| SDA0 | P0.3 | R2 4.7 kΩ→+3.3V · U6 pin 5 · U7 SDA |
| TAMPER | P0.4 | R3 10 kΩ→+3.3V · SW2 NC→GND |
| BT_KEY (opt.) | P0.6 | U5 KEY (only if `BT_KEY_CTRL_ENABLED`=1) |
| ADMIN | P0.7 / EINT2 | SW1 (ACTIVE-LOW → GND, on-board pull-up) |
| TXD1 | P0.8 | U5 RXD |
| RXD1 | P0.9 | U5 TXD |
| LCD_RS | P0.16 | U2 pin 4 |
| LCD_EN | P0.17 | U2 pin 6 |
| LCD_D4 | P0.18 | U2 pin 11 |
| LCD_D5 | P0.19 | U2 pin 12 |
| LCD_D6 | P0.20 | U2 pin 13 |
| LCD_D7 | P0.21 | U2 pin 14 |
| KP_R1–R4 | P1.16–P1.19 | U3 rows R1–R4 |
| KP_C1–C4 | P1.20–P1.23 | U3 columns C1–C4 |
| MOT_IN1 | P1.24 | U8 pin 2 (1A) |
| MOT_IN2 | P1.25 | U8 pin 7 (2A) |
| BUZ | P1.26 | U4 (direct, or via Q1 base) |
| VCC_5V | — | U2 pins 2, 15 (via R5) · U4 · U5 VCC · U8 pins 16, 1 |
| VCC_3V3 | U1 VDD (all) | U6 pin 8 · U7 VCC · R1 · R2 · R3 |
| GND | U1 VSS (all) | U2 pins 1, 5, 16 · U3 · U4 · U5 · U6 pins 1–4, 7 · U7 GND · U8 pins 4, 5, 12, 13 · BT1(−) · motor-supply GND |
| V_MOTOR | — | U8 pin 8 only (6–12 V separate supply) |
| VBAT | — | U7 VBAT · BT1(+) |
| M1A / M1B | — | U8 pin 3 ↔ M1 A ↔ C1 · U8 pin 6 ↔ M1 B ↔ C1 |
| XTAL (on-board) | XTAL1, XTAL2 | X1 12 MHz + 2×~22 pF (on-board, not wired) |
| RST | RST | RST button + 10 kΩ→+3.3V (on-board) |
| ISP | P0.14 | ISP button + 10 kΩ→+3.3V (on-board) |

**31 nets · 24 of them leave the MCU · ≈44 physical jumper wires** (same count as §1).

### 18.8 DESIGN-RULE CHECK

- **Every net above appears on a sheet and in §1**, and vice-versa — the schematic, the master table and the firmware source all agree. Nothing is drawn that the firmware does not drive, and nothing the firmware drives is left off the sheets.
- **RTCK / TRACESYNC warning carried onto the drawing** (sheet 2, buzzer): P1.26 and P1.20 must never be held LOW at reset or the keypad/motor/buzzer pins become the debug port. The net list has no pull-down on either.
- **UART1 contention carried onto the drawing** (sheet 3, HC-05): before wiring the HC-05, confirm whether the on-board MAX232's second channel is hard-wired to P0.8/P0.9 (§8 Warning 2). The net list shows only the HC-05 on TXD1/RXD1.
- **Motor supply carried onto the drawing as its own rail** (sheet 6): U8 pin 8 gets the separate supply; its GND joins the one common ground.
- **On-board power choice**: the 5V / 3.3V selection header near the ISP button feeds the rails above — confirm what it selects before trusting them (§17 items 4–8).
- To create a real Proteus project from this, place U1–U9, M1, Q1, the resistors/caps, the switches and BT1 in ISIS, then wire each sheet by its net labels; §18.7 is the finished net list. The ASCII here is a blueprint, not a substitute for a `.pdsprj` — nothing has been bench-tested.

---

*Companion file: `CONNECTIONS.txt` — same content except §18 (the schematic), which lives only in this `.md`.*
*Firmware change log: `memory.txt`.*
