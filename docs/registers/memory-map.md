# Memory Map — Where the Code Runs and Where the Passwords Live

## Peripheral intro

The LPC2148 has two on-chip memories, and the project adds a third, external
one. All three are documented here so the reader knows exactly where the code,
its data, and the *secrets* live.

| Memory | Range | Size | Contents |
|---|---|---|---|
| **Flash (IROM)** | `0x0000_0000`–`0x0007_FFFF` | 512 KB | The whole program + constants |
| **SRAM (IRAM)** | `0x4000_0000`–`0x4000_7FFF` | 32 KB | All RW/ZI data at run time |
| **AT24C256 EEPROM** (external, I²C) | `0x0000`–`0x7FFF` (device) | 32 KB | The passwords + magic marker |

The flash/RAM ranges come straight from the Keil project
(`majorproject12.uvproj`): `IROM(0-0x7FFFF)` and `IRAM(0x40000000-0x40007FFF)`
for the `LPC2148` CPU. The EEPROM is not in the chip at all — it is the
`0xA0` I²C device described in [i2c.md](i2c.md).

## The link-time layout (`majorproject12.sct`)

The scatter file pins the three execution regions:

```
LR_IROM1 0x00000000 0x00080000  {        ; LOAD region: whole 512 KB flash
  ER_IROM1 0x00000000 0x00080000  {      ; EXEC region, load == exec address
    *.o (RESET, +First)                  ; Startup.s vector table comes FIRST
    *(InRoot$$Sections)                  ; anything the linker pins at the root
    .ANY (+RO)                           ; all code + read-only constants
  }
  RW_IRAM1 0x40000000 0x00008000  {      ; RAM region: 32 KB
    .ANY (+RW +ZI)                       ; all initialized data + BSS
  }
}
```

- **`RESET, +First`** guarantees the reset vector (from `Startup.s`) sits at
  address `0x0`, where the ARM7TDMI looks for it on power-up.
- `LR_IROM1` = `ER_IROM1` at the same address means the program is **in place**
  in flash — no load-time copy phase, the chip boots straight from it.
- The `CLOCK(12000000)` in the project file tells Keil the crystal is 12 MHz —
  the same value the PLL code in `projectmain.c` multiplies up (see
  [clocking.md](clocking.md)).

## Where the interesting data lives at run time

Because there is no OS, the 32 KB SRAM holds everything the firmware allocates
as globals, e.g.:

- `bt_buffer[BT_BUF_SIZE]` (32 bytes) + `bt_index`/`bt_rx_ready`/`bt_fault`/
  `bt_activity` — the interrupt-driven HC-05 receive state (`bluetooth.c`).
- The admin-menu state (`admin_flag`, the alarm variables, menu locals).
- Stack and heap, sized by the startup code.

There is no `malloc`; every buffer is a fixed array declared at file scope, so
RAM usage is static and predictable.

## The EEPROM password layout (`defines.h`)

The AT24C256's 2-byte internal address space is laid out deliberately, with
padding between regions so a future 8-byte password or a longer marker cannot
collide:

| EEPROM address | Content | Bytes |
|---|---|---|
| `0x0000` | Magic marker **"LKR1"** | 4 |
| `0x0010` | **Level-1** (Bluetooth) password | `PWD_LEN` = 4 ASCII digits |
| `0x0020` | **Level-2** (keypad) password | `PWD_LEN` = 4 ASCII digits |
| `0x0040` | Scratch byte (boot self-test only) | 1 |

`EEPROM_MAGIC_ADDR` is the discriminator: if the 4 bytes at `0x0000` do not
equal `"LKR1"`, the EEPROM counts as blank/uninitialised and the firmware loads
the two factory-default passwords on first boot (`ensure_default_passwords()`).
Every password **write** is followed by an immediate **read-back** compare
(the admin menu, `edit_passwords()`) so a bus glitch cannot silently corrupt a
stored password.

### Why these addresses, and a note on security

- The EEPROM is a separate device from the LPC2148's flash — its contents
  survive power-off *and* are independent of re-flashing the chip, which is
  exactly what a "persistent password" needs.
- The marker at `0x0000` and the password slots are **plaintext ASCII digits**.
  Nothing encrypts them at rest: a physical attacker with an I²C reader can
  dump them. That is a documented, accepted limitation of this teaching
  project (see `SECURITY.md`), not an oversight.

## Hardware consequences

- The vector table *must* be at flash offset `0x0` (`RESET, +First`) or the
  CPU fetches garbage on reset.
- 32 KB of SRAM is small but the firmware is entirely static-allocation, so it
  is comfortably within budget. Every extra `char buf[32]` in a driver shows
  up in `RW_IRAM1`.
- Because the code runs **in place** from flash, the 60 MHz core with MAM
  enabled (see [clocking.md](clocking.md)) reads instructions from the flash
  on every fetch — the MAM prefetch is what keeps that from stalling the CPU.
- The EEPROM layout is part of the project's *data contract*: changing the
  addresses or the marker in `defines.h` invalidates passwords stored by an
  earlier build (the marker mismatch would simply re-default them).
