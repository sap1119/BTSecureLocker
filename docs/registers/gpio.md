# GPIO — Pin Function Selection and the Port Registers

## Peripheral intro

Every LPC2148 pin has two or three functions: a default **GPIO** role plus one
or more **alternate** functions (UART, I²C, external interrupt, etc.). The
**PINSEL** registers pick which function each pin is in. Once a pin is GPIO,
the **IO0**/**IO1** register sets drive it.

| Register family | Port | Addresses |
|---|---|---|
| `PINSEL0` / `PINSEL1` / `PINSEL2` | function select | `0xE002C000` / `0xE002C004` / `0xE002C014` |
| `IO0PIN` `IO0SET` `IO0DIR` `IO0CLR` | Port 0 data | `0xE0028000` / `04` / `08` / `0C` |
| `IO1PIN` `IO1SET` `IO1DIR` `IO1CLR` | Port 1 data | `0xE0028010` / `14` / `18` / `1C` |

The pin's alternate function is chosen by 2 bits in PINSELn. Common encodings:
`00` = GPIO, `01` = alt function 1, `10` = alt function 2, `11` = alt function 3.
Only the `01` / `11` encodings actually used by this project are listed here.

> **The one read-modify-write rule:** PINSELn registers are written with
> **read-modify-write** (`&= ~mask` then `|= value`), never a blind `=` —
> except `PINSEL2`, where all three drivers use `PINSEL2 &= ~0x0000000CUL`,
> which clears only the two bits they need and leaves the register's reserved
> bits untouched. A blind `PINSEL2 = 0` (the old code) also worked on this
> part but is the kind of write that quietly breaks on another chip.

## Registers used

### PINSEL0 (`0xE002C000`) — Port 0 pins 0–15

Every field is 2 bits wide, one per pin. The firmware programs:

| Bits | Pin | `01` = | Firmware value | Source |
|---|---|---|---|---|
| [1:0] | P0.0 | TXD0 | `|= 0x00000005` (whole field cleared first) | `uart.c` |
| [3:2] | P0.1 | RXD0 | (same write) | `uart.c` |
| [5:4] | P0.2 | SCL0 | `|= (1UL<<4)` | `eeprom.c` |
| [7:6] | P0.3 | SDA0 | `|= (1UL<<6)` | `eeprom.c` |
| [9:8] | P0.4 | — (GPIO) | `&= ~(3UL<<8)` → tamper input | `security.c` |
| [15:14] | P0.7 | EINT2 | `|= (3UL<<14)` → admin button | `menu.c` |
| [17:16] | P0.8 | TXD1 | `|= (1UL<<16)` | `uart.c` |
| [19:18] | P0.9 | RXD1 | `|= (1UL<<18)` | `uart.c` |

Notes:
- P0.0/P0.1: `PINSEL0 &= ~0x0000000F; PINSEL0 |= 0x00000005;` — P0.0=0b01
  (TXD0), P0.1=0b01 (RXD0).
- P0.8/P0.9: `PINSEL0 &= ~(0x000F0000); PINSEL0 |= (1UL<<16)|(1UL<<18);`.
- P0.6 is a **free GPIO** used only by the *optional* `BT_KEY_CTRL_ENABLED`
  mode (`PINSEL0 &= ~(3UL << 12)` then driven as an output) — compiled in only
  when that flag is on. With the default flag it stays unused.

### PINSEL1 (`0xE002C004`) — Port 0 pins 16–31

| Bits | Pin | Firmware value | Source |
|---|---|---|---|
| [11:0] | P0.16–P0.21 (LCD) | `&= ~0x00000FFF` → all GPIO | `lcd.c` |

The LCD uses six consecutive Port-0 pins purely as GPIO (no alternate
function): **P0.16 = RS, P0.17 = EN, P0.18–21 = D4–D7** (see `lcd_defines.h`).

### PINSEL2 (`0xE002C014`) — Port 1 function groups

| Bit | Group | `0` = | `1` = |
|---|---|---|---|
| 2 | P1.26–P1.31 | GPIO | JTAG/Debug port |
| 3 | P1.16–P1.25 | GPIO | Trace port |

All three drivers that touch Port 1 pins run the same **read-modify-write**:
`PINSEL2 &= ~0x0000000CUL`. That clears **both** bits, so P1.16–31 are all
plain GPIO. This matters because:
- the **keypad** lives on P1.16–23 (rows/cols),
- the **motor** on P1.24–25,
- the **buzzer** on P1.26.

Clearing bit 2 disables the JTAG/Debug pin group — worth knowing if you ever
try to debug over JTAG (the keypad shares those pins).

### IO0DIR / IO1DIR — Data Direction (`...DIR` at `+0x08` / `+0x18`)

`1` = output, `0` = input. Bit position = pin number.

| Set by | Value | Effect |
|---|---|---|
| `lcd.c` | `IO0DIR \|= LCD_RS\|LCD_EN\|LCD_DATA_MSK` (P0.16,17,18–21) | LCD control/data = outputs |
| `security.c` | `IO0DIR &= ~(1UL<<4)` | P0.4 (tamper) = input |
| `keypad.c` | `IO1DIR \|= 0x0FUL<<16; IO1DIR &= ~(0x0FUL<<20)` | Rows P1.16–19 = outputs, cols P1.20–23 = inputs |
| `motor.c` | `IO1DIR \|= (1UL<<24)\|(1UL<<25)` | Motor IN1/IN2 = outputs |
| `buzzer.c` | `IO1DIR \|= (1UL<<26)` | Buzzer = output |

### IO0PIN / IO1PIN — Pin state (`...PIN` at `+0x00` / `+0x10`)

Read returns the current pin levels. Used for **inputs**:

- Tamper: `if (IO0PIN & (1UL<<4)) return 0; else return 1;` — the switch is
  **active LOW** (external pull-up; pressed = pin LOW = tampered).
- Keypad columns: `if (!(IO1PIN & (1UL << (20 + c))))` → column read LOW =
  key pressed.

### IO0SET / IO0CLR / IO1SET / IO1CLR — Set / Clear (`+0x04` / `+0x0C`)

Writing a `1` to a bit in **SET** drives that pin high; in **CLR** drives it
low. These are atomic single-pin writes (no read-modify-write needed). Every
output in the project uses them:

- **LCD** nibble strobe: `IO0SET = LCD_EN; ... IO0CLR = LCD_EN;`
  (`lcd_enable_pulse()`), and data on D4–D7 via `IO0CLR = LCD_DATA_MSK;`
  `IO0SET = (nib & 0x0F) << 18;`.
- **Keypad** rows idle HIGH, one row pulled LOW per scan:
  `IO1SET = ROW_MASK; IO1CLR = (1UL << (16 + r));`.
- **Motor** direction: `IO1SET/IO1CLR` on IN1/IN2 (forward = IN1=1,IN2=0).
- **Buzzer**: `IO1SET`/`IO1CLR` on bit 26.

## The firmware's complete pin assignment (net map)

| Pin | Function | Direction | Driver |
|---|---|---|---|
| P0.0 | TXD0 → PC RX | out | `uart.c` |
| P0.1 | RXD0 ← PC TX | in | `uart.c` |
| P0.2 | SCL0 (EEPROM + ext. RTC) | I²C open-drain | `eeprom.c` |
| P0.3 | SDA0 (EEPROM + ext. RTC) | I²C open-drain | `eeprom.c` |
| P0.4 | Tamper switch (active LOW) | in | `security.c` |
| P0.6 | *(optional)* HC-05 KEY/EN | out | `bluetooth.c` (`BT_KEY_CTRL_ENABLED`) |
| P0.7 | Admin button → EINT2 | in (interrupt) | `menu.c` |
| P0.8 | TXD1 → HC-05 RXD | out | `uart.c` / `bluetooth.c` |
| P0.9 | RXD1 ← HC-05 TXD | in (interrupt) | `uart.c` / `bluetooth.c` |
| P0.16 | LCD RS | out | `lcd.c` |
| P0.17 | LCD EN | out | `lcd.c` |
| P0.18–21 | LCD D4–D7 | out | `lcd.c` |
| P1.16–19 | Keypad rows | out | `keypad.c` |
| P1.20–23 | Keypad columns | in | `keypad.c` |
| P1.24 | Motor IN1 (L293D) | out | `motor.c` |
| P1.25 | Motor IN2 (L293D) | out | `motor.c` |
| P1.26 | Buzzer | out | `buzzer.c` |

## Hardware consequences

- The two UARTs and the I²C bus are usable only because the right alternate
  functions were selected in PINSEL0 — every one of them defaults to GPIO at
  reset.
- `PINSEL2 &= ~0x0C` disables the JTAG/Trace pin groups. The board cannot be
  debugged over JTAG while the keypad/motor/buzzer use those pins — that is
  the trade for a minimal pin count.
- All Port-1 GPIO writes go through the atomic SET/CLR registers, so a scan
  sequence (keypad rows) or a direction switch (motor) never races with an
  interrupt.
