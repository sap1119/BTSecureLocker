# Bill of Materials — SecureLocker

Every part the system needs, with the value, quantity, purpose and whether it
is already on the Vector development board or must be supplied externally.

> ⚠ **Honest caveat.** The core system has been bench-tested on the physical
> board. The one exception in this parts list is the **external RTC** — the
> DS1307/DS3231 + coin cell is *not* bench-tested here because the chip must be
> physically fitted (the firmware auto-detects it). The future-advancements
> roadmap is design only. The values below come from the schematic and the
> firmware source (pull-ups, timings, voltages were read out of `defines.h` /
> `connections.md`, not guessed); [Connections §17](../hardware/connections.md)
> lists the items that still deserve a visual/continuity check on your board.

## Quick summary

| Module | On-board or external | Total extra parts you must buy |
|---|---|---|
| LPC2148 + board | on-board | none |
| 16x2 LCD | on-board | none |
| 4x4 keypad | on-board | none |
| Buzzer | on-board | none (transistor optional) |
| Admin + tamper switches | on-board (switches); tamper contact external | 1 lever/reed switch + 10 kΩ |
| MAX232 + 2 DB9 | on-board | none |
| **HC-05 Bluetooth** | **external** | module + 4 jumpers (5 with KEY) |
| **AT24C256 EEPROM** | **external** | 8-pin DIP + breadboard + **2 × 4.7 kΩ** |
| **DS1307/DS3231 RTC** (optional) | **external** | DIP/breakout + coin cell (+ crystal for DS1307) |
| **L293D motor driver** | **external** | 16-pin DIP + breadboard |
| **DC gear motor** | **external** | 5–12 V motor |
| Motor supply | external | separate 6–12 V supply / 9 V battery |

The only parts **already on the board** are: the LPC2148 and its support
(crystal, reset/ISP, regulators, USB, MAX232, DB9s), the 16x2 LCD with contrast
pot, the 4x4 keypad, the buzzer, and the switch/LED banks.

## Full parts table

Designators match the schematic sheets in [Connections §18](../hardware/connections.md).

| Designator | Part | Value / part number | Qty | Where | Purpose |
|---|---|---|---|---|---|
| U1 | LPC2148 | NXP/Philips, ARM7TDMI-S, LQFP64 | 1 | on-board | the MCU — runs the firmware |
| X1 | Crystal | 12 MHz | 1 | on-board | PLL reference → CCLK 60 MHz |
| U2 | LCD | HD44780-compatible, 16x2 | 1 | on-board | prompts, masked keypad digits, countdowns, clock |
| R6 | Contrast pot | yellow trimmer, ~10 kΩ | 1 | on-board | LCD V0 — adjust for characters |
| U3 | Keypad | 4x4 matrix, 16 tact switches | 1 | on-board | Level-2 password entry + admin menu |
| U4 | Buzzer | active, 5 V | 1 | on-board | alarm + tamper alert |
| U9 | MAX232 | ADM232L | 1 | on-board | RS-232 level shifter → DB9 UART0 (audit log) |
| J6 | DB9 | RS-232 female ×2 | 2 | on-board | UART0 → PC log; UART1 (unused — see [§8](../hardware/connections.md)) |
| SW1 | Admin button | ACTIVE-LOW switch (SW5–SW8 bank) | 1 | on-board | P0.7 / EINT2 → admin menu |
| Q1 | NPN transistor | 2N3904 | 1 (opt.) | external | drive a loud buzzer (base resistor R4) |
| U5 | Bluetooth module | HC-05 (breakout) | 1 | **external** | phone → LPC2148 over UART1 |
| U6 | EEPROM | AT24C256, 8-pin DIP | 1 | **external** | stores both passwords (survives power-off) |
| U7 | RTC (optional) | **DS3231** (preferred) or DS1307 | 1 | **external** | battery-backed clock across power-off |
| X2 | Crystal | 32.768 kHz | 1 | **external** | DS1307 only — the DS3231 has it built in |
| U8 | Motor driver | L293D, 16-pin DIP | 1 | **external** | H-bridge → open/close the lock |
| M1 | DC gear motor | 5–12 V, geared | 1 | **external** | the locker latch |
| BT1 | Coin cell | Semos / CR2032 | 1 | **external** | powers the RTC while the board is off |
| SW2 | Tamper switch | lever micro-switch or reed switch, **NC** | 1 | **external** | enclosure-intrusion detection |
| R1, R2 | I2C pull-ups | **4.7 kΩ** | 2 | **external** | SCL0/SDA0 — mandatory, shared EEPROM + RTC |
| R3 | Tamper pull-up | **10 kΩ** | 1 | **external** | holds P0.4 HIGH when the enclosure is shut |
| R4 | Buzzer base resistor | **1 kΩ** | 1 (opt.) | external | between P1.26 and Q1 base |
| R5 | LCD backlight resistor | **220 Ω** | 1 (may be on-board) | external/on-board | current limit for the LCD LED backlight |
| C1 | Motor snubber | **100 nF** ceramic | 1 | **external** | across M1 terminals — suppresses brush noise |
| C2, C3 | Decoupling caps | **100 nF** | 2 | **external** | LPC2148 VDD — fit one per power pin |

### Jumpers / consumables

| Item | Qty | Notes |
|---|---|---|
| Female–female jumper wires | ≈ 20 | HC-05 (4–5), EEPROM (6), RTC (4), L293D (4), tamper (1) |
| Breadboard | 1–2 | EEPROM, RTC, L293D |
| 8-pin DIP sockets | 2–3 | EEPROM, RTC (DS1307), L293D (16-pin) |
| RS-232 cable / USB-RS232 adapter | 1 | UART0 → PC audit log |
| Terminal wire / hookup | small reel | power rails + common ground |

## Values that must match the firmware

These are not suggestions — the firmware was written against them. All live in
[`firmware/defines.h`](../../firmware/defines.h) and are listed here for the
builders:

| Value | Setting | Why |
|---|---|---|
| EEPROM address | slave **0xA0** (A0/A1/A2 all to GND) | `EEPROM_ID` in `eeprom.c` |
| EEPROM + RTC bus speed | **~100 kHz** (`I2C0SCLH = I2C0SCLL = 75`) | I2C0 at PCLK 15 MHz |
| EEPROM + RTC VCC | **+3.3 V** (pull-ups to 3.3 V too) | the POST message tells you to check `VCC=3V3` |
| RTC address | **0x68** (write byte 0xD0), fixed — no address pins | shared bus, no conflict with 0xA0 |
| HC-05 UART | **9600 baud, 8N1** (`BT_DATA_BAUD`) | factory default on most HC-05s |
| HC-05 VCC | **+5 V** (breakout has its own 3.3 V regulator) | — |
| Motor supply | **separate +6…12 V**, not the board 5 V | start surge resets the MCU otherwise |
| L293D pin 1 (1,2EN) | tie to **+5 V** | this firmware has no ENABLE output |
| Motor pulse | `MOTOR_ROTATE_MS` (default 500 ms) | tune for your gearbox |

## Where each requirement is documented

- Exact pin-by-pin wiring and schematic: [Connections](../hardware/connections.md)
- Why the EEPROM is mandatory: [main-flow.md POST](../firmware/main-flow.md)
- Why the RTC is optional but the only way time survives power-off:
  [rtc.md](../registers/rtc.md) and [Connections §10](../hardware/connections.md)
- The security model's honest limits: [SECURITY.md](../../SECURITY.md)
