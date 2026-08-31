# Component-Level Working — How Each Part Actually Works

The companion to [connections.md](connections.md) (which tells you *where* every
wire goes) and [bill-of-materials.md](bill-of-materials.md) (which tells you
*what* to buy). This page explains **how each component works on its own**: the
operating principle inside the chip/module, and what the firmware actually does
to make it do something useful.

For the register-level "what the CPU writes" details, each section links to the
relevant page under [`../registers/`](../registers/README.md).

---

## 0. Quick map

| Component | What it does here | The principle |
|---|---|---|
| LPC2148 MCU | the "brain" | ARM7TDMI-S core + peripherals (UART, I2C, timers, RTC) |
| 16x2 LCD | shows prompts, countdowns, clock | HD44780 controller, 4-bit parallel writes |
| 4x4 keypad | Level-2 password + menu navigation | matrix scanning: 16 keys on 8 pins |
| HC-05 Bluetooth | Level-1 password from the phone | UART-over-air serial bridge, data mode |
| AT24C256 EEPROM | stores both passwords | I2C serial EEPROM, non-volatile |
| DS1307/3231 RTC | keeps the clock across power-off | I2C clock chip ticking from a coin cell |
| L293D + DC motor | opens/closes the latch | H-bridge direction control |
| Buzzer | alarm + tamper alert | active buzzer, just needs DC |
| Tamper switch | intrusion detection | NC contact + pull-up, active-low read |
| Admin button | opens the admin menu | momentary switch on an edge-triggered interrupt |
| MAX232 + DB9 | audit log to a PC | RS-232 level shifter |

---

## 1. The LPC2148 — the brain

**What it is.** An NXP/Philips ARM7TDMI-S microcontroller in a 64-pin LQFP, on
the Vector development board. It runs at **60 MHz** (CCLK, from the on-board
12 MHz crystal through the PLL) with a **15 MHz** peripheral clock (PCLK).

**How it works.** It is a von-Neumann 32-bit RISC core fetching instructions
from its internal 512 KB flash and using 32 KB of SRAM. It has a **vector
interrupt controller (VIC)** that dispatches hardware interrupts to handler
routines, and a set of peripheral blocks — two UARTs, two I2C controllers, two
timers, an RTC, and up to ~50 GPIO pins — that the firmware configures by
writing to memory-mapped registers.

**What the firmware does with it.**

- Brings the clock up: PLL `0x24` (×6, ÷4) → 60 MHz, `VPBDIV=0` → 15 MHz PCLK,
  MAM (memory accelerator) turned on so flash fetches don't slow the core down.
  See [clocking.md](../registers/clocking.md).
- Configures every pin's *function* with the PINSEL registers (UART, I2C, GPIO,
  EINT2) and each pin's *direction* with IO0DIR/IO1DIR.
- Runs one big foreground `while(1)` state machine, with only two interrupt
  sources: UART1 receive and the admin button (EINT2).
- Every timeout in the project is measured against Timer1 (`millis()`), a
  free-running millisecond time base.

**Why a plain 60 MHz chip is enough for this project.** The "computing" the
locker does is trivial (compare 4-byte passwords, drive pins, format text for
the LCD). What matters is *reliability and real-time control* of the small
peripherals — which is exactly what a bare-metal ARM7 excels at.

---

## 2. The 16×2 LCD (HD44780) — how text gets on the screen

**What it is.** A 16-character × 2-line alphanumeric LCD module whose brains are
the **HD44780 controller** IC on the back of the glass.

**How it works, internally.**

- The HD44780 keeps a **display RAM (DDRAM)** with one byte per character cell
  — 80 cells per line internally, of which this module shows the first 16.
- Each byte you write is a **character code** that the controller looks up in
  its built-in **character generator ROM** (the standard ASCII + Japanese Kana
  table) and renders as a 5×8 dot matrix on the glass.
- Two control pins decide *what* a write means:
  - **RS** (register select): `0` = the byte is an *instruction* (move the
    cursor, clear, set the line); `1` = the byte is a *character* to display.
  - **EN** (enable): the clock. The controller samples the data bus on the
    **falling edge** of EN — the firmware puts the nibble on the bus, raises
    EN, lowers EN, and the byte is latched.
- **4-bit mode** is why only D4–D7 are wired: each 8-bit value is sent as a
  **high nibble then a low nibble**, two EN pulses per byte. It halves the
  wiring from 8 + 2 pins to 6. The firmware's `lcd.c` sends every byte in two
  nibble-writes (see [gpio.md](../registers/gpio.md)).
- Line addressing: row 1 is DDRAM 0x00–0x0F, row 2 is 0x40–0x4F. The firmware
  sets the DDRAM address (`lcd_set_cursor`) then streams characters.

**What the firmware does.** Writes the power-on init sequence (function-set for
4-bit / 2 lines, display on, clear, entry mode), then every screen update is
"set cursor + write characters". R/W is tied low (write-only), so the firmware
never reads the busy flag — it just delays long enough after each command. The
LCD shows: the boot splash, the password prompts, the **live countdowns** in
the top-right corner, the post-unlock clock, and fault screens.

**Failure signature.** Backlight on but no characters → contrast pot at an
extreme; garbage characters → nibble order / D4–D7 swapped. See
[connections.md §4](connections.md) and §16.

---

## 3. The 4×4 matrix keypad — how 16 keys fit on 8 pins

**What it is.** 16 tactile switches wired as a 4×4 grid: each switch connects
**one row line to one column line**. Pressing a key shorts its row to its
column.

**How matrix scanning works.** You never read 16 inputs; you *scan* in two
passes per key cycle:

1. Drive **one row low** (all other rows high), and read all 4 columns.
2. A column reading low tells you "the key at (that row, that column) is
   pressed".
3. Repeat for the next row. Four row-writes × four column-reads covers all 16
   keys.

```
        COL1   COL2   COL3   COL4
        P1.20  P1.21  P1.22  P1.23
ROW1  ┌──────┬──────┬──────┬──────┐
P1.16 │  1   │  2   │  3   │  A   │      press "6": ROW2 is driven LOW,
ROW2  ├──────┼──────┼──────┼──────┤      COL3 reads LOW  →  key = 6
P1.17 │  4   │  5   │  6   │  B   │
ROW3  ├──────┼──────┼──────┼──────┤
P1.18 │  7   │  8   │  9   │  C   │
ROW4  ├──────┼──────┼──────┼──────┤
P1.19 │  *   │  0   │  #   │  D   │
      └──────┴──────┴──────┴──────┘
```

**Details that matter in firmware:**

- Rows are **outputs** (P1.16–P1.19), columns are **inputs** (P1.20–P1.23) with
  pull-ups, so an unpressed column reads HIGH and a pressed one reads LOW.
- **Debounce:** a mechanical switch bounces for a few milliseconds, so
  `keypad_scan()` reads a key only after it has been stable, and
  `keypad_getkey_timeout()` additionally waits for *release* before accepting
  the next press.
- **Why this matters for the countdown:** the Level-2 entry polls
  `keypad_scan()` itself in a loop, redrawing the live countdown each pass —
  that's how the seconds counter and the keypresses share the same code path
  without blocking ([authentication.md](../firmware/authentication.md)).

**Failure signature.** Scrambled digits → rows and columns swapped/rotated;
double entries → key held too long. See [connections.md §5](connections.md).

---

## 4. The HC-05 Bluetooth module — the phone's serial cable

**What it is.** A Bluetooth Classic (2.0 + EDR, class 2 ≈ 10 m) module whose
whole job is to be a **wireless serial (UART) cable**: bytes sent by the phone
app come out of the module's TXD pin, and bytes sent into its RXD pin go out
over the air. From the LPC2148's point of view it is just another UART device.

**How it works, internally.**

- It contains a Bluetooth radio + baseband chip and a simple UART bridge. The
  phone **pairs** to it once (default PIN `1234` or `0000`), then any serial
  data from either side is transparently transported to the other.
- **Two operating modes**:
  - **Command (AT) mode** — with KEY/EN held high, the module interprets
    incoming bytes as `AT` configuration commands (baud, name, PIN, etc.).
  - **Data mode** — with KEY/EN low/floating, everything is passed through
    the radio as data.
- In this project **KEY is not wired** (unless `BT_KEY_CTRL_ENABLED=1`), so the
  module is *permanently in data mode* — which is exactly why the firmware's
  boot self-test cannot send "AT" to prove the module is present, and instead
  proves the **UART1 link** with an internal loopback
  ([bluetooth-hc05.md](../firmware/bluetooth-hc05.md)).
- Power/levels: VCC takes **+5 V** (the breakout has its own 3.3 V regulator),
  but TXD/RXD are **3.3 V logic** — safe for the LPC2148's 3.3 V UART pins.
  **Never** plug it into the ±12 V DB9 sockets.

**What the firmware does.** Configures UART1 for 9600 8N1 (`BT_DATA_BAUD`),
and its **receive interrupt** (`UART1_ISR`, VIC slot 1) drains each incoming
byte into a ring buffer as it arrives. The phone sends `1234#`; the `#` is the
**terminator**; anything after it is classified as trailing junk and rejected
(so `1234#123456789` can never get through). When the main loop sees the buffer
is ready, it snapshots it, clears it, and hands it to the Level-1 comparison.
Full protocol: [bluetooth-hc05.md](../firmware/bluetooth-hc05.md) and
[uart.md](../registers/uart.md).

**Failure signature.** "BT LINK OK / HC-05 IN DATA" is a **pass** (module in
data mode, expected); garbage characters → baud mismatch or missing common
ground; the module never talks → TXD/RXD swapped, or the MAX232 is driving the
same pins (see [connections.md §8](connections.md), Warning 2).

---

## 5. The AT24C256 I2C EEPROM — the password vault

**What it is.** A 256 Kbit (32 KB) serial EEPROM in an 8-pin DIP: **non-volatile**
memory that keeps its contents with power off, written and read over the I2C
bus. It is *mandatory* in this project — both passwords live here and the boot
self-test blocks until the chip answers.

**How it works, internally.**

- Organized as 32,768 bytes. One byte at a time or a 64-byte page at a time.
- It has a **fixed I2C address** formed by the three address pins: `1010` +
  A2·A1·A0 + R/W. All three pins are grounded here → **slave address 0xA0**
  (write byte 0xA0, read byte 0xA1). See [i2c.md](../registers/i2c.md).
- **To write a byte:** the master sends (1) the device address 0xA0, (2) the
  2-byte memory address, (3) the data byte. The chip's internal **write cycle**
  then burns the bit into its floating-gate cells (~5 ms, during which it stops
  acknowledging — the firmware waits `I2C_WAIT_LIMIT` for that).
- **To read a byte:** device address, 2-byte memory address, then a
  **repeated start** and the *read* device address 0xA1; the chip then clocks
  the stored byte out.
- Endurance: roughly a million write cycles — effectively unlimited for a
  locker that changes passwords a handful of times.
- The **WP pin** (write-protect) is tied to GND. If it were HIGH, every
  password write would silently fail and the locker would deny everyone.

**What the firmware stores** (see [memory-map.md](../registers/memory-map.md)):

| Address | Contents |
|---|---|
| 0x0000 | `LKR1` magic marker — proves the EEPROM holds *this* project's data |
| 0x0010 | Level-1 (Bluetooth) password — factory `1234` |
| 0x0020 | Level-2 (keypad) password — factory `5678` |
| 0x0040 | scratch byte, only for the boot self-test (0x5A / 0xA5 probe) |

**Why the self-test is clever.** It writes `0x5A` then `0xA5` to the scratch
byte and reads both back. A bus stuck high reads `0xFF`, stuck low reads `0x00`
— neither can "pass" two bitwise-complement patterns, so a dead bus can't fake
a working EEPROM. And because defaults are only written *after* the POST
passes, the firmware can never "write defaults" into a missing chip and then
deny everyone forever (the original bug this project's POST fixes).

**Failure signature.** Parks on "I2C EEPROM NOT CONFIGURED" → missing 4.7 kΩ
pull-ups, SDA/SCL swapped, WP high, or no common ground. See
[connections.md §9](connections.md).

---

## 6. The DS1307/DS3231 RTC — how the clock survives a power cut

**What it is.** A battery-backed **real-time clock** chip on the same I2C bus as
the EEPROM (address 0x68 / write byte 0xD0). It is *optional* but it is the
**only** way this project keeps real time across a full power-off.

**Why the LPC2148 alone can't.** The LPC2148's on-chip RTC is clocked from PCLK
only — it has **no RTCX1/RTCX2 crystal pins and no VBAT pin** (confirmed in the
vendored `LPC214x.h`). The moment the board loses power the on-chip RTC stops
ticking. The external chip keeps its own 32.768 kHz oscillator alive on a coin
cell, so it keeps counting while the board is dead. See
[rtc.md](../registers/rtc.md).

**How it works, internally.**

- A 32.768 kHz crystal (built into the DS3231, external on the DS1307) feeds a
  divider that increments **BCD-encoded time registers** once per second.
- Registers 0x00–0x06 hold seconds, minutes, hours, day-of-week, date, month,
  and year — in **binary-coded decimal** (e.g. 42 seconds = `0x42`), so the
  firmware reads/writes two decimal digits per register.
- The **coin cell on VBAT** powers the oscillator and registers when the main
  supply is off. A fresh CR2032 keeps the DS3231 within ~1 minute/year (it has
  a temperature-compensated oscillator); the DS1307 drifts more but needs the
  cheaper external crystal.
- The DS1307's oscillator **starts on the first clock write** (the seconds
  register's clock-halt bit is cleared) — which is why the bench-test procedure
  says "set the clock once" on a brand-new chip.

**What the firmware does.** At boot `rtc_init()` probes 0xD0; if a chip answers
with a plausible time, that time is loaded into the on-chip RTC, and **every**
admin-menu clock write is mirrored back to the chip. The boot log honestly
reports which mode is active (`RTC: battery-backed external RTC found` vs
`RTC: no external RTC`). Reads are tear-free (the CTIME registers are re-read
until consistent, so you never see 12:59:59 roll to 12:00:00 mid-read). The
12:00:00 / 01-01-2024 default is applied **only** when the RTC is genuinely
unset — never unconditionally (the faculty's "comes back at 12:00 PM" bug).

**Failure signature.** After a power-off the clock is wrong → no external RTC
fitted, dead coin cell, or the chip's SDA/SCL are swapped. A *reset* (power
never lost) keeps the time in every case. See [connections.md §10](connections.md).

---

## 7. The L293D + DC motor — how the latch opens

**What it is.** An **L293D H-bridge** driver IC that lets a small
**DC gear motor** run forward (open the latch), reverse (close it), or stop —
from just two GPIO pins, without the motor's current ever flowing through the
MCU.

**How an H-bridge works.** Four switches arranged in a square around the motor:
"bridge" the top-left and bottom-right switches → current flows one way (motor
forward); the other diagonal → current flows the other way (motor reverse);
all off → motor coasts; both on the same side → brake. The L293D puts those
switches inside one 16-pin DIP with clamping diodes included.

**The two inputs drive one bridge** (the firmware drives only IN1/IN2; the
enable pin is tied to +5 V because this firmware has no enable output):

| IN1 (P1.24) | IN2 (P1.25) | Outputs | Motor |
|---|---|---|---|
| 1 | 0 | 1Y high, 2Y low | Forward — **open** |
| 0 | 1 | 1Y low, 2Y high | Reverse — **close** |
| 0 | 0 | both low | Stop |

**Power rails matter.** VSS (pin 16) is the logic supply from the board's
+5 V; VS (pin 8) is the **separate motor supply (6–12 V)**. The motor's start
surge would brown out the board's 5 V rail and reset the MCU — hence the second
supply, and the **one common ground** between them
([connections.md §11](connections.md)).

**What the firmware does** (`open_locker_sequence()`): forward 500 ms → stop →
hold "LOCKER OPEN" for 5 s → settle 200 ms → reverse 500 ms → stop. A
re-entrancy guard (`locker_busy`) makes sure only one cycle can be in flight.
The 100 nF capacitor across the motor terminals suppresses brush noise that
would otherwise corrupt the I2C/UART lines.

**Failure signature.** Motor never turns → pin 1 (1,2EN) not tied to +5 V, or
no common ground; board resets when the motor runs → motor powered from the
board's 5 V rail. See [connections.md §16](connections.md).

---

## 8. The buzzer — the alarm

**What it is.** An **active** buzzer (on-board): it contains its own oscillator,
so applying a steady DC voltage makes it beep at a fixed frequency — no
to-generation or tone setup needed from the firmware.

**How the firmware uses it.** P1.26 drives it active-high. For a quiet buzzer
that pin can drive it directly; for a loud one a 2N3904 transistor (1 kΩ base
resistor) switches the +5 V rail instead. `buzzer.c` exposes `on`/`off`/`alert`;
the firmware sounds it for the tamper alarm and the alarm-clock trigger, and it
**never** fires on a wrong password (silent denial is a deliberate security
choice — see [SECURITY.md](../../SECURITY.md)).

**One hardware trap that matters:** P1.26 is also the LPC2148's **RTCK/JTAG**
pin (and P1.20 is TRACESYNC). If either is held LOW while RESET is released,
the whole pin bank (buzzer + motor + keypad) comes up as the debug/trace port
instead of GPIO. Hence the rule: **never fit pull-downs on P1.26 or P1.20, and
don't hold a keypad key at power-up.** See [connections.md §6](connections.md).

---

## 9. The tamper switch — intrusion detection

**What it is.** A **normally-closed** lever or reed switch on P0.4, held HIGH by
a 10 kΩ pull-up to +3.3 V, closing to GND when the enclosure is opened.

**How it works, physically:**

- Lid **shut** → the lever is pressed by the lid → the NC contact *opens* →
  the pull-up holds P0.4 **HIGH** → normal.
- Lid **opened** → the lever releases → the NC contact *closes* → P0.4 pulled
  to **GND** → the firmware logs `TAMPER ALERT` and sounds the buzzer.

The design is "fail-safe" in the same way a smoke detector is: a **cut wire**
reads as tamper, not as quiet. And because the firmware polls it in *every*
phase — boot self-test retries, Level-2 password entry, lockout, idle — the
enclosure is never unguarded, even while the system is parked on a fault screen
or counting down a lockout. See [main-flow.md](../firmware/main-flow.md) and
[connections.md §7](connections.md).

---

## 10. The admin button — the menu door

**What it is.** An active-low momentary switch on P0.7, wired to the LPC2148's
**EINT2** external interrupt, configured falling-edge (press = pin goes LOW).

**How it works.** A press fires the EINT2 interrupt (VIC slot 2); the ISR sets
`admin_flag` and clears the pending interrupt. The main loop checks the flag on
every pass and opens the admin menu — set clock, set alarm, change passwords,
system info. It is a momentary *edge* trigger, not a level, so one press means
one menu entry. The menu has its own per-screen timeouts so it can never trap
the user (every wait is bounded — see [admin-menu.md](../firmware/admin-menu.md)).

**Failure signature.** Button "does nothing" → wired to the *active-high* switch
bank; EINT2 wants a falling edge. Use the active-low bank (SW5–SW8). See
[connections.md §7](connections.md).

---

## 11. The MAX232 + DB9 — how the audit log reaches a PC

**What it is.** A **RS-232 level shifter** (ADM232L) between the LPC2148's
UART0 and a DB9 connector. The LPC2148's UART outputs 0–3.3 V logic; RS-232
uses **±12 V signals**. The MAX232 (with its small charge-pump capacitors)
translates between the two so a standard PC serial port can be used.

**How it works.** Charge pumps inside the chip generate the ±12 V rails from
the single +5 V supply; the transmitter inverts/drives them, the receiver
tolerates the wide RS-232 swings and outputs clean 3.3 V logic back to P0.1.
RXD0/TXD0 are already routed to it on the PCB — nothing to wire.

**What it's used for.** The **audit log** streams out UART0 at 9600 8N1 to a
PC terminal (PuTTY/Tera Term): every open, denial, timeout, tamper event,
lockout and admin change, timestamped by the RTC. The **critical warning**:
never connect the 3.3 V HC-05 to the ±12 V DB9 — it will destroy it
([connections.md §8](connections.md)). The other DB9 is UART1's and must stay
disconnected while the HC-05 is wired to P0.8/P0.9.

---

## 12. One unlock, end to end — how it all fits together

```
phone app ──► HC-05 ──UART1──► LPC2148              LPC2148 ──I2C0──► AT24C256
 "1234#"      radio   P0.8/P0.9  ├─ UART1_ISR fills  "1234" vs stored  Level-1
               bridge            └─ the ring buffer   L1@0x0010          OK?
                                                                        │ yes
user ──► 4x4 keypad ──GPIO──► LPC2148 ──► reads stored L2@0x0020 ◄── EEPROM
 "5678"    matrix scan  P1.16-23   Level-2 compare            │ match
                                                                        ▼
                LPC2148 ──GPIO P1.24/25──► L293D ──► DC motor ──► latch OPENS
                │
                └── UART0 ──► MAX232 ──► DB9 ──► PC terminal
                    every event, timestamped (RTC: on-chip ◄── DS1307/3231)
```

1. The phone's `1234#` arrives over Bluetooth; UART1's interrupt stores it.
2. The main loop compares it to the Level-1 password in the EEPROM.
3. On match, the LCD asks for Level-2; the keypad's matrix scan delivers `5678`
   (masked as `****`), with the live countdowns running.
4. On match, the motor sequence pulses the L293D → the latch opens, holds, and
   closes.
5. Meanwhile UART0/MAX232/DB9 streams the whole story to the PC, and the RTC
   stamps every line with the real time.
```

---

## 13. Where to go next

- Pin-by-pin wiring + troubleshooting: [connections.md](connections.md)
- Full parts list with values: [bill-of-materials.md](bill-of-materials.md)
- Register-level detail for every peripheral: [`../registers/README.md`](../registers/README.md)
- The firmware flow each component feeds into: [`../firmware/README.md`](../firmware/README.md)
