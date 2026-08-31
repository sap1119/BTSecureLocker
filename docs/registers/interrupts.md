# Interrupts — the VIC, UART1 Receive, and the Admin Button (EINT2)

## Peripheral intro

The LPC2148's **VIC** (Vectored Interrupt Controller) manages all interrupt
sources. This project uses exactly two of them, and both are **IRQs** (not
FIQs):

| Interrupt | VIC source | Vectored slot | Why it exists |
|---|---|---|---|
| **UART1 receive** | 7 | slot 1 | Receives HC-05 password commands into a ring buffer without blocking the main loop |
| **EINT2 (admin button)** | 16 | slot 2 | A physical push-button on P0.7 opens the admin menu |

Slot 1 has higher priority than slot 2, so UART1 receive preempts the button —
appropriate, since the receive ISR is deliberately short (it only drains the
FIFO and classifies bytes).

**A design rule visible in both ISRs:** an ISR does the *minimum* — set a flag
(UART1: `bt_rx_ready`; EINT2: `admin_flag`) and clear the pending source. All
real work happens later from the main loop. That keeps the interrupt latency
small and the ISRs trivially auditable.

## Registers used

### VIC (`0xFFFFF000` base)

| Offset | Register | Purpose |
|---|---|---|
| +0x0C | `VICIntSelect` | 1 = FIQ, 0 = **IRQ** per source |
| +0x10 | `VICIntEnable` | Unmask a source (write 1s) |
| +0x14 | `VICIntEnClr` | Mask a source (write 1s) — used for critical sections |
| +0x30 | `VICVectAddr` | Read: vector of the active ISR; **write 0 = end-of-interrupt** |
| +0x104 | `VICVectAddr1` | ISR address for slot 1 |
| +0x108 | `VICVectAddr2` | ISR address for slot 2 |
| +0x204 | `VICVectCntl1` | Slot 1 control (`0x20 \| 7`) |
| +0x208 | `VICVectCntl2` | Slot 2 control (`0x20 \| 16`) |

**VICVectCntl format:** bit 5 = slot enabled, bits [4:0] = the source number
assigned to the slot. So `0x20 | 7` = "enable slot 1 for source 7" and
`0x20 | 16` = "enable slot 2 for source 16".

### External interrupt configuration

| Register | Address | Used for |
|---|---|---|
| `EXTMODE` | `0xE01FC148` | `\|= (1<<2)` → EINT2 is **edge**-sensitive |
| `EXTPOLAR` | `0xE01FC14C` | `&= ~(1<<2)` → **falling** edge (button pulls the line low) |
| `EXTINT` | `0xE01FC140` | write `1<<2` to **clear** a pending EINT2 |

## The firmware's sequences

### `bluetooth_init()` — arm UART1 receive (from `bluetooth.c`)

```c
uart1_init(BT_DATA_BAUD);      /* P0.8/P0.9, 8N1, 9600, FIFOs on (see uart.md) */
U1MCR  = 0x00;                 /* loopback off, lines idle                    */
U1IER  = 0x01;                 /* "Receive Data Available" interrupt on        */

VICIntSelect &= ~(1UL << 7);   /* UART1 -> IRQ, not FIQ                       */
VICVectAddr1  = (u32)UART1_ISR;/* ISR address in vectored slot 1              */
VICVectCntl1  = 0x20 | 7;      /* enable slot 1, assign it to source 7        */
VICIntEnable  = (1UL << 7);    /* unmask UART1                                 */
```

### `UART1_ISR` — the receive handler (`bluetooth.c`)

```c
__irq void UART1_ISR(void)
{
    u8 drain = BT_ISR_MAX_BYTES;                 /* bound: 32 passes max */
    while ((U1LSR & 0x01) && (drain > 0U)) {     /* RDR set?             */
        drain--;
        ch = U1RBR;          /* read clears RDR                              */
        bt_activity++;
        /* CR/LF skipped first (phone-app framing noise)                     */
        if (bt_rx_ready) { bt_fault |= BT_RX_TRAILING; continue; }  /* junk after '#' */
        if (ch == '#') { bt_buffer[bt_index] = '\0'; bt_rx_ready = 1; bt_index = 0; }
        else if (bt_index < BT_MAX_PAYLOAD) bt_buffer[bt_index++] = ch;
        else                                bt_fault |= BT_RX_OVERFLOW;
    }
    VICVectAddr = 0;         /* end-of-interrupt */
}
```

The drain loop is **bounded** (`BT_ISR_MAX_BYTES`) because the UART1 FIFO is
16 bytes deep — a healthy interrupt never needs more than two passes, and an
unbounded spin here would wedge the whole locker inside an ISR if RDR ever
failed to clear.

**Critical sections around shared state:** `bluetooth_read_command()` and
`bluetooth_clear()` (and the loopback test) mask UART1 for the duration so a
byte arriving mid-copy cannot corrupt the snapshot:

```c
VICIntEnClr = (1UL << 7);      /* --- critical section: mask UART1 --- */
... copy buffer + flags atomically ...
VICIntEnable = (1UL << 7);     /* --- end critical section --- */
```

### `admin_int_init()` — arm the admin button (from `menu.c`)

```c
PINSEL0 &= ~(3UL << 14);
PINSEL0 |=  (3UL << 14);        /* P0.7 -> EINT2 alternate function      */

EXTMODE  |= (1UL << 2);         /* edge-sensitive (not level)            */
EXTPOLAR &= ~(1UL << 2);        /* falling edge = button pressed         */
EXTINT    = (1UL << 2);         /* clear any stale pending flag          */

VICIntSelect &= ~(1UL << 16);   /* EINT2 -> IRQ, not FIQ                 */
VICVectAddr2  = (u32)EINT2_ISR; /* ISR address in vectored slot 2        */
VICVectCntl2  = 0x20 | 16;      /* enable slot 2, assign it to source 16 */
VICIntEnable  = (1UL << 16);    /* unmask EINT2                           */
```

### `EINT2_ISR` — the button handler (`menu.c`)

```c
__irq void EINT2_ISR(void)
{
    admin_flag = 1;
    EXTINT = (1UL << 2);   /* clear the EINT2 pending flag */
    VICVectAddr = 0;       /* end-of-interrupt */
}
```

The main loop polls `admin_flag` and, when set, calls `admin_menu()` — the
menu runs entirely in the foreground, never inside the ISR.

## Why the shared pieces look the way they do

- **`VICIntSelect &= ~(...)`** routes each source to the IRQ (non-FIQ) path.
  Nothing in this firmware is fast or latency-critical enough to justify FIQ,
  and keeping both sources as IRQs means the VIC hardware serialises them for
  free.
- **`VICVectAddr = 0` at the end of every ISR** is the VIC's end-of-interrupt
  acknowledgement. Forgetting it leaves the interrupt permanently asserted.
- **Clearing `EXTINT` inside the ISR** prevents the same button press from
  re-firing as soon as the ISR returns. The falling edge is only *latched*
  until this write.
- The two critical sections exist because `bt_buffer`/`bt_fault`/`bt_rx_ready`
  are shared between the ISR and the main loop. Masking the single VIC source
  (rather than using a global interrupt disable) is the cheapest correct
  locking: nothing else in the firmware needs protecting from this ISR.

## Hardware consequences

- The admin button is **falling-edge on P0.7/EINT2** — wired with a pull-up so
  pressing it drives the line low. A *level*-sensitive config would have the
  button retriggering for as long as it is held.
- UART1 receive is fully interrupt driven, so the main loop can spend its time
  polling the tamper switch and driving the LCD while the HC-05 burst arrives —
  at 9600 baud a character takes ~1 ms, comfortably inside the ISR's budget.
- Disabling VIC slot priorities: because slot 1 > slot 2, a sustained HC-05
  flood cannot delay EINT2 indefinitely — each ISR is bounded and returns
  quickly.
