# UART0 / UART1 — Line Control, Baud Divisor, FIFOs and the Interrupt

## Peripheral intro

The LPC2148 has two UARTs. This project uses both, and they have two very
different jobs:

- **UART0 — the audit-log console.** `P0.0`/`P0.1` (TXD0/RXD0) talk to a PC
  through a USB-to-serial adapter. The firmware *only transmits* on UART0
  (`uart0_tx` / `uart0_string` / `uart0_int`) — everything that
  `security.c`'s `log_event()` prints (the access log, boot self-test results,
  admin actions) goes out on this port. **Passwords and keypad digits are
  never logged** (see `SECURITY.md`).
- **UART1 — the HC-05 Bluetooth link.** `P0.8`/`P0.9` (TXD1/RXD1) talk to the
  HC-05 module. Transmit is polled (`uart1_tx`), but **receive is interrupt
  driven** — `UART1_ISR` in `bluetooth.c` fills a ring buffer so the main loop
  never blocks on individual bytes.

Both run at **9600 baud**, computed from the 15 MHz peripheral clock
(`divisor = PCLK / (16 · baud)`).

| | UART0 | UART1 |
|---|---|---|
| Base address | `0xE000C000` | `0xE0010000` |
| Pins | P0.0 TXD0 / P0.1 RXD0 | P0.8 TXD1 / P0.9 RXD1 |
| Direction | TX only | TX polled + RX interrupt |
| Baud | 9600 | 9600 (data mode) / 38400 (optional AT mode) |
| Driver | `uart.c` + `security.c` (log) | `uart.c` + `bluetooth.c` |

## Registers used

Addresses are `base + offset`. `RBR` and `THR` share one address: **reading**
it returns the receiver buffer register, **writing** it loads the transmit
holding register. `DLL`/`DLM` (the baud divisor) sit at the same addresses as
`RBR`/`IER` and are only reachable while the **DLAB** bit of `LCR` is set.

| Offset | U0 | U1 | Name |
|---|---|---|---|
| +0x00 | `U0RBR`/`U0THR` | `U1RBR`/`U1THR` | RX buffer / TX holding |
| +0x04 | `U0IER` | `U1IER` | Interrupt enable |
| +0x08 | `U0FCR` | `U1FCR` | FIFO control |
| +0x0C | `U0LCR` | `U1LCR` | Line control |
| +0x10 | `U0MCR` | `U1MCR` | Modem control |
| +0x14 | `U0LSR` | `U1LSR` | Line status |

### UxLCR — Line Control Register (`+0x0C`)

| Bit | Meaning |
|---|---|
| 7 | **DLAB** — Divisor Latch Access Bit (1 = DLL/DLM programmable) |
| 6 | Break control (0) |
| [5:3] | Parity: `0b000` = none |
| 2 | Stop bits: `0` = 1 stop bit |
| [1:0] | Word length: `0b11` = 8 data bits |

**Firmware values:**
- `0x83` = `1000 0011` → **DLAB set**, 8 data bits, no parity, 1 stop bit.
  Written first so the baud divisor can be programmed.
- `0x03` = `0000 0011` → the **same frame format with DLAB cleared** (normal
  operating mode). Written after `UxDLL`/`UxDLM`.

### UxDLL / UxDLM — Baud Rate Divisor (`+0x00` / `+0x04`)

```
divisor = PCLK / (16 · baud) = 15000000 / (16 · 9600) = 97
```

`UxDLL = divisor & 0xFF` (`0x61`), `UxDLM = (divisor >> 8) & 0xFF` (`0x00`).
The divisor is loaded into the low byte first; a divisor of 97 is the closest
integer to the exact value 97.66 — under 0.7% error, well inside UART
tolerance.

### UxFCR — FIFO Control Register (`+0x08`)

**Firmware value: `0x07`** → FIFO enable (bit 0) + clear RX FIFO (bit 1) +
clear TX FIFO (bit 2). The LPC2148 UART FIFOs are 16 bytes deep. Flushing both
FIFOs is what makes `bluetooth_loopback_test()` start from a known state.

### UxLSR — Line Status Register (`+0x14`)

| Bit | Flag | Used for |
|---|---|---|
| 0 | **RDR** — Receiver Data Ready | `UART1_ISR` drain loop, loopback test |
| 1 | OE — overrun error | loopback error mask |
| 2 | PE — parity error | loopback error mask |
| 3 | FE — framing error | loopback error mask (wrong divisor ⇒ FE) |
| 4 | BI — break interrupt | loopback error mask |
| 5 | **THRE** — THR empty | bounded TX wait in `uart0_tx` / `uart1_tx` |
| 7 | RXFE — error in RX FIFO | loopback error mask |

- **THRE (bit 5)** is the transmit gate: `while (!(U0LSR & 0x20)) ...` — spin
  until the holding register is empty, then write `UxTHR`. The spin is bounded
  by `UART_TX_WAIT_LIMIT` (200000): if THRE never sets, the byte is **dropped**
  rather than hanging the whole locker inside the logger (see the long comment
  at the top of `uart.c`).
- **RDR (bit 0)** gates receive. **Reading `UxRBR` clears RDR.**
- The loopback test checks `lsr & 0x9E` — bits 7,4,3,2,1, i.e. every error
  flag. Any error bit set means the byte was mangled.

### UxTHR / UxRBR — Transmit / Receive (`+0x00`)

Writing `UxTHR = ch` transmits; reading `UxRBR` returns the received byte and
clears RDR. Both are single bytes (`volatile unsigned char`).

### U1IER — Interrupt Enable Register (`+0x04`)

**Firmware value: `0x01`** → bit 0 = "Receive Data Available" interrupt
enabled. This is the only interrupt source the firmware enables on UART1 — the
receiver interrupts on *every* received byte. (Note: `defines.h` carries a
manual fallback definition `*(volatile unsigned long *)0xE0010004` for older
keil/device-header builds; the vendored `LPC214x.h` defines `U1IER` natively.)

### U1MCR — Modem Control Register (`+0x10`)

| Value | Meaning |
|---|---|
| `0x00` | **Normal mode** — loopback off, lines idle. Set explicitly by `bluetooth_init()` so the module returns to a known state. |
| `0x10` | **LMS = 1, internal loopback** — the UART's transmitter is wired straight to its own receiver *inside the block*, and TXD1 is held in its idle state. Used by `bluetooth_loopback_test()`: the test sends nothing to the HC-05 and cannot disturb a paired phone. |

## The firmware's sequences

### `uart0_init(baud)` / `uart1_init(baud)` — one shared shape

```c
divisor = 15000000UL / (16UL * baud);

PINSEL0 &= ~0x0000000F;           /* UART0: clear P0.0/P0.1 function bits */
PINSEL0 |=  0x00000005;           /*        P0.0 = TXD0, P0.1 = RXD0      */
/* UART1:  PINSEL0 &= ~0x000F0000; PINSEL0 |= (1<<16)|(1<<18);  (P0.8/P0.9) */

UxLCR = 0x83;                     /* DLAB on: divisor latches reachable */
UxDLL = divisor & 0xFF;
UxDLM = (divisor >> 8) & 0xFF;
UxLCR = 0x03;                     /* DLAB off: normal register access */
UxFCR = 0x07;                     /* FIFOs on and flushed */
```

### `uartx_tx(ch)` — the bounded THRE wait

```c
u32 guard = UART_TX_WAIT_LIMIT;
while (!(UxLSR & 0x20)) {          /* THRE empty? */
    if (--guard == 0UL)
        return;                    /* stuck transmitter: drop the byte, keep running */
}
UxTHR = ch;
```

### The receive path (`bluetooth.c`, covered fully in `interrupts.md`)

- `U1IER = 0x01` — RDA interrupt on.
- `UART1_ISR` drains the FIFO: `while ((U1LSR & 0x01) && drain--)` read `U1RBR`.
- `bluetooth_loopback_test()`: `U1MCR = 0x10` (LMS loopback) → push the four
  patterns `0x55/0xAA/0x0F/0xF0` → check each came back clean (`lsr & 0x9E`)
  → `U1MCR = 0x00` to restore normal pins.

## Hardware consequences

- 9600 baud / 8-N-1 is the HC-05's factory data-mode default and a sensible
  console rate for the PC adapter. The divisor math only works because
  **PCLK is exactly 15 MHz** — if the PLL or VPBDIV were changed, every
  divisor here would be silently wrong.
- The loopback test does not rely on the module: it proves the *MCU-side*
  link (pins, divisor, frame, FIFOs, receive path). Module presence is a
  separate, weaker probe (see `docs/firmware/bluetooth-hc05.md`).
- Transmit never hangs the system: a dead UART (e.g. wrong PCLK) drops bytes
  instead of wedging the locker mid-log.
