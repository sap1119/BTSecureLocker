# Security Policy

## Supported versions

The firmware is a single continuously-maintained build (no release branches).
The latest state of the `main` branch is the supported version. Security
fixes land in the next commit; there is no backporting process for an
embedded college project.

| Version | Supported |
|---|---|
| `main` (current) | ✅ |
| older rounds (0.1.0 / 0.2.0 / 0.3.0) | ⚠️ recommended to update |

## Reporting a vulnerability

This is a teaching/portfolio project — please open a **public issue** or
**pull request** for anything you find. For anything you'd rather not discuss
publicly, open a [security advisory] on GitHub.

We aim to acknowledge reports within a few days.

## Security model

What this firmware is designed to protect against, and how:

| Threat | Mitigation |
|---|---|
| Password guessing over Bluetooth | 3 wrong attempts → 30-second lockout; over-length / trailing-garbage payloads rejected and counted; constant-time `password_match()` |
| "Password + extra characters" bypass (`1234#123456789`) | ISR classifies trailing bytes (`BT_RX_TRAILING`) and rejects the attempt |
| Unbounded brute-force session | Level-2 keypad entry bounded: 3 min total / 1 min per character with a live countdown |
| Physical intrusion | Tamper switch polled during *every* phase, including password entry and the POST |
| Password tampering / loss | Passwords live in EEPROM with a magic marker + read-back verification on every write |
| Operator privacy | Passwords and keypad digits are **never** written to the audit log |
| Unauthorised admin changes | Admin menu requires physical button press; all menu actions are timestamped on the log |
| Clock rollback / loss | External battery-backed RTC (DS1307/DS3231) keeps the time through power-off; boot logs which clock mode is active |

## Known limitations (be honest about these)

- **Bluetooth is in the clear.** The HC-05 transmits passwords as plaintext.
  Anyone with a Bluetooth sniffer in range can read a transmission. This is an
  inherent limit of the HC-05 + LPC2148 (no crypto hardware, no secure pairing
  used). The physical keypad factor exists partly to compensate.
- **The HC-05 cannot be interrogated in this wiring.** KEY/EN is unwired, so
  the module is always in data mode. The boot POST proves the UART link via an
  internal loopback, not the module's presence. A missing module is
  indistinguishable from a healthy idle one. See the note on
  `BT_KEY_CTRL_ENABLED` in `defines.h` for a definitive wiring option.
- **The on-chip RTC cannot tick through power-off on its own** (chip design
  limit — PCLK-derived, no VBAT/RTCX pins). The external DS1307/DS3231 fixes
  this *if fitted*; without it, a full power-off loses the time.
- **No encryption at rest.** Passwords are stored in the EEPROM as plaintext
  digits behind a magic marker. A physical attacker with an I2C reader can
  dump them. Replacing this with a keyed hash would require a hash algorithm
  in the firmware.
- **Timing side channels on the keypad** are mitigated (constant-time compare,
  no early exit) but the keypad matrix scan itself is not constant-time.

## Dependencies

There are no third-party runtime dependencies. The only vendored file is the
NXP device header `firmware/vendor/LPC214x.h` (copyright NXP, distributed with
the Keil MDK install) — kept so the repo and CI can build without Keil.
