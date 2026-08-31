# Bluetooth / HC-05 — the Receiver, the Protocol, and the Layered POST

Everything about the wireless factor lives in `bluetooth.c` (plus the UART1
register setup in `uart.c`). This page covers the receive path, the protocol
rules, the burst-settle window, and the boot self-test that had to be honest
about what it can and cannot prove.

## The wiring and the one hard constraint

```
HC-05 RXD  ← LPC2148 TXD1 (P0.8)
HC-05 TXD  → LPC2148 RXD1 (P0.9)
HC-05 VCC  → 5V     (module has its own regulator/level shifting)
HC-05 GND  → GND
HC-05 KEY/EN → NOT CONNECTED in this project
```

**KEY/EN unwired means the module is always in data mode.** An HC-05 accepts AT
commands only while KEY is held HIGH (command mode, fixed 38400 baud). With
KEY low/unconnected, everything the MCU sends is transmitted over the air —
"AT" is *not* a command in that mode. This one fact drives the entire POST
design (below): it means the module **cannot** be interrogated, so the boot
check must prove the MCU-side link instead.

## The protocol

The paired phone app sends the password digits followed by a `#` terminator:

```
1234#
```

- Only `#` terminates a command and signals readiness (`bt_rx_ready`).
- **CR/LF are deliberately ignored** — most phone apps append a trailing `\n`
  after what you type. If `\n` were treated as a terminator, it would arrive in
  its own interrupt right after the `#` and overwrite the just-received command
  with an empty string. If it were treated as trailing junk, it would invalidate
  every correct password. So CR/LF are checked and skipped *first*.

## The receiver — `UART1_ISR`

UART1 receive is fully interrupt driven (VIC source 7, slot 1; see
[interrupts.md](../registers/interrupts.md)). The ISR drains the 16-byte FIFO
into a ring buffer:

```c
while ((U1LSR & 0x01) && (drain > 0U)) {   /* RDR set? (bounded) */
    ch = U1RBR;                            /* read clears RDR   */
    bt_activity++;
    if (ch == '\r' || ch == '\n') continue;        /* framing noise: skip first */
    if (bt_rx_ready) { bt_fault |= BT_RX_TRAILING; continue; }  /* junk after '#' */
    if (ch == '#') { bt_buffer[bt_index]='\0'; bt_rx_ready=1; bt_index=0; }
    else if (bt_index < BT_MAX_PAYLOAD) bt_buffer[bt_index++] = ch;
    else                                bt_fault |= BT_RX_OVERFLOW;
}
VICVectAddr = 0;                            /* end-of-interrupt */
```

The drain loop is **bounded by `BT_ISR_MAX_BYTES`** so a stuck RDR flag can
never spin inside an interrupt forever. The two abuse cases it detects:

| Fault flag | Trigger | Why it exists |
|---|---|---|
| `BT_RX_TRAILING` | any real character arriving **after** a completed command is still pending | this is the `1234#123456789` bypass — old code dropped the extras and granted access to a pristine "1234" |
| `BT_RX_OVERFLOW` | payload longer than `BT_MAX_PAYLOAD` (31) | old code silently truncated a 40-char entry and compared its first 31 chars |

`bt_activity` is a wrapping counter incremented on *every* byte — used purely
as a "is the line busy?" indicator (see the settle window) and as the
"did the module answer?" indicator in the AT probe.

## The burst-settle window — why security can't rely on timing

A phone app sends a whole line as one back-to-back burst. At 9600 baud a
character takes ~1 ms, but the main loop polls every 100 ms — so for
`1234#123456789`, the `#` and all nine trailing characters arrive within
~15 ms, long before the foreground looks. That ordering happens to work in
the firmware's favour, but relying on it would make security depend on timing
luck.

`bluetooth_settle()` waits for the line to fall quiet for `BT_SETTLE_MS`
(40 ms), checked in `BT_SETTLE_STEP_MS` (5 ms) steps. By the time it returns,
every byte the sender transmitted has been classified by the ISR — so the
trailing-junk check is **deterministic**. The total wait is capped at
`BT_SETTLE_MAX_MS` (2 s) so a hostile stream can't hold the main loop here
forever (that would also stop the tamper switch from being polled); hitting
the cap is itself a sign of abuse, and such a flood will have set the fault
flags long before.

## Reading the command — the critical section

`bluetooth_read_command()` copies the (volatile) buffer and flags into the
caller's buffer, then resets for the next command. The UART1 interrupt is
**masked for the duration** so the snapshot and reset happen atomically with
respect to the ISR — otherwise a byte arriving mid-copy could be lost or leak
a stale fault flag into the next command:

```c
VICIntEnClr = (1UL << 7);       /* --- critical section --- */
... copy buffer, read status, reset flags ...
VICIntEnable = (1UL << 7);      /* --- end critical section --- */
```

The caller **must reject anything other than `BT_RX_OK`** without comparing it
against the stored password — enforced by the classification ladder in
`main-flow.md`.

## The boot self-test — being honest about a real limitation

This is the fix for the classic false alarm: the old POST sent `"AT"` and
declared the module missing when nothing came back. On correctly wired
hardware that reported **"HC-05 BT NOT CONFIGURED" at every boot** — because a
KEY-less module in data mode forwards "AT" over the air and answers nothing.
Silence was the *expected* behaviour of a healthy module.

`bluetooth_selftest()` is therefore **layered** and returns one of four codes:

```
┌────────────────────────────────────────────────────────────┐
│ Layer 1 (deterministic, the part that can really fail):    │
│   bluetooth_loopback_test()                                │
│   U1MCR = 0x10 → TX wired to RX inside the UART block      │
│   push 0x55, 0xAA, 0x0F, 0xF0, check each returns clean    │
│   → proves pins, divisor, frame, FIFOs, RX path            │
│   fail → BT_POST_UART_FAIL  (a real fault)                 │
├────────────────────────────────────────────────────────────┤
│ Layer 2 (bonus evidence only, never a failure):            │
│   bt_at_probe() sends "AT" and watches for ANY reply byte  │
│   reply   → BT_POST_MODULE_OK  (proof of presence)         │
│   silence → BT_POST_LINK_OK    (a PASS — the norm here)    │
└────────────────────────────────────────────────────────────┘
```

Why 0x55/0xAA/0x0F/0xF0? The two pairs are bitwise inverses, so a line stuck
high or low can't pass both; the 0x0F/0xF0 pair also exercises a mid-frame
transition that catches a grossly wrong baud divisor. A framing error (`LSR &
0x9E`) in the loopback test means the divisor or the 8-N-1 format is wrong.

**What the POST still cannot prove:** without the KEY wire there is no way to
distinguish "healthy module sitting quietly in data mode" from "no module
attached at all" — the LPC2148's port pins have fixed internal pull-ups, so an
unconnected `RXD1` idles HIGH, exactly like a connected idle module's TXD
line. So the firmware reports precisely what it knows (the link is configured)
and says plainly in the log that presence could not be interrogated. **The
moment the module delivers its first password, that *is* proof of presence**
and the log records it once (`"HC-05 link confirmed: data received from the
Bluetooth module"`).

## The optional KEY-wire upgrade

If you add one wire (HC-05 KEY/EN → P0.6, a free GPIO) and set
`BT_KEY_CTRL_ENABLED = 1` in `defines.h`, the POST gains a third layer:
`bt_command_mode_probe()` drives KEY high, re-programs UART1 to the fixed
38400 baud command mode uses, sends "AT", and *requires* an answer — in that
configuration silence **is** a real fault, so the boot can report
`BT_POST_MODULE_FAIL` with certainty. It restores everything afterwards:
KEY low, data-mode baud, receiver state cleared.

## Where the registers live

- Pin selection: `PINSEL0` P0.8/P0.9 → TXD1/RXD1 ([gpio.md](../registers/gpio.md))
- Line format, baud divisor, FIFOs, loopback: UART1 registers ([uart.md](../registers/uart.md))
- The receive interrupt: VIC source 7 ([interrupts.md](../registers/interrupts.md))
