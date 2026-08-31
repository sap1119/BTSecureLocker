# Future Advancements — Upgrading the Locker

This project is a complete, faculty-approved **two-factor** locker. It is also a
perfect platform to grow — the two-factor discipline, the audit trail, the
tamper monitoring and the load-bearing security rules transfer directly to a
more capable controller. This page is the **roadmap**: (A) moving off the
LPC2148 to a WiFi/Bluetooth-capable microcontroller, and (B) the future
security layers — face unlock, fingerprint, and more.

> ⚠️ **Honest framing.** Nothing on this page is implemented, wired, or
> bench-tested. This is a *design/roadmap* document. Every upgrade below is a
> real project of its own — and every one must **keep** the security invariants
> this project already enforces (two factors, constant-time password compare,
> lockout, audited changes, tamper watched during every phase). Adding features
> must never silently weaken those.

---

## Part A — Moving to a more capable microcontroller

### A.1 Why move at all

The LPC2148 is excellent at what it does (real-time pin control at 60 MHz), but
it has hard limits for the next steps:

| Limitation | What it blocks |
|---|---|
| No WiFi, no BLE | cloud logging, app control, OTA updates, encrypted remote access |
| No crypto hardware; 60 MHz slow for math | real AES/TLS on the password channel |
| 32 KB RAM / 512 KB flash | image buffers, cloud stacks, biometric algorithms |
| 2 UARTs (both used) | adding a fingerprint sensor + camera + GPS is crowded |
| No camera / sensor interfaces beyond UART/I2C/GPIO | face unlock, PIR arrays |

The **HC-05 was this project's wireless step**. The natural next step is a chip
that already has Bluetooth *and* WiFi built in.

### A.2 The three candidates

| | **ESP8266** (e.g. NodeMCU) | **ESP32** (recommended) | **Arduino** (Uno / Mega) |
|---|---|---|---|
| Core | Tensilica Xtensa LX106, 32-bit, 80–160 MHz | LX6 dual-core, 32-bit, up to 240 MHz | AVR 8-bit, 16 MHz (Uno) |
| RAM / Flash | ~80 KB usable / up to 4 MB | 520 KB SRAM / 4–16 MB | 2 KB / 32 KB (Uno) |
| Wireless | **WiFi only** (no Bluetooth) | **WiFi + Bluetooth Classic + BLE** built in | none — needs a module/shield |
| Extra UARTs | 2 | 3 | 1 hardware UART (Uno) |
| Crypto | software only | hardware AES/SHA/RSA | software only (very slow) |
| Camera | no | yes (ESP32-CAM variant) | no |
| Ease | Arduino IDE, huge community | Arduino IDE / ESP-IDF / MicroPython | easiest; biggest shield ecosystem |
| Cost | very low | low–moderate | low |
| Fits the HC-05? | still needs it (or an external BT module) | **replaces it** — BT is on-chip | keeps the HC-05, or ESP-01 for WiFi |

**Recommendation: the ESP32.** It is the only one of the three that *replaces*
the HC-05 outright (built-in Bluetooth Classic — same "serial over the air"
behaviour), adds WiFi for the cloud, has hardware AES for real encryption, and
has a camera variant for face unlock. The ESP8266 is the cheapest WiFi-only
option but still needs the HC-05 (or a separate BT chip) to keep the two-factor
Bluetooth factor. Arduino is the friendliest to code but 5 V logic, 8-bit, and
very small RAM — fine for a rebuild, not for the biometric/cloud goals.

### A.3 What the migration map looks like

Every module in this firmware has a direct successor. The security ideas stay
the same; only the silicon changes.

| Current module | LPC2148 | ESP32 replacement |
|---|---|---|
| `bluetooth.c` + `uart.c` (UART1) | HC-05 on P0.8/P0.9 | **built-in Bluetooth Serial** — same protocol, no module |
| `uart.c` (UART0) audit log | MAX232 → DB9 → PC | keep the serial log **and** mirror it to MQTT/cloud |
| `eeprom.c` | AT24C256 over I2C0 | EEPROM still works, **or** use the ESP32's internal flash/Preferences |
| `rtc.c` | DS1307/3231 over I2C0 | keep the external RTC (best accuracy) or use the ESP32's internal RTC + NTP time sync |
| `keypad.c` | 4×4 matrix scan | same 8 GPIO lines, same scan loop |
| `lcd.c` | 16×2 HD44780 4-bit | same LCD, or a cheap I2C LCD adapter to save pins |
| `motor.c` | L293D + DC motor | same L293D/motor, or a solenoid for a positive-locking latch |
| `security.c` | tamper + constant-time compare | keep both; add reed-switch door state + PIR |
| `menu.c` | EINT2 admin button | same button, or a "factory reset" via the phone app |
| `defines.h` timing rules | Timer1 `millis()` | ESP32 Arduino `millis()` — identical pattern |

Because the ESP32 runs the Arduino core by default, the port is mostly a matter
of replacing register writes with Arduino library calls — the *architecture*
(document, boot sequence, state machine, security invariants) port over as-is.

---

## Part B — Future security layers

The current system already has a strong core (two factors, lockout, constant-
time compare, audited changes, tamper during every phase). The future layers
below are ordered from **cheapest/easiest** to **most ambitious**.

### B.1 Phase 0 — already done (for reference)

Bluetooth password (Level-1) + keypad password (Level-2) + tamper + lockout +
timestamped audit log.

### B.2 Phase 1 — physical-hardening add-ons (still on the LPC2148)

These are small and can be done on the existing board:

- **Door/reed status switch.** A reed switch or micro-switch on the latch reports
  *actually locked / actually unlocked* (a second physical input like the
  tamper pin). Logs "door left open" and catches a lock that failed to close —
  turning "we pulsed the motor" into "the door is verifiably shut".
- **PIR motion sensor.** A cheap HC-SR501 PIR near the locker logs "motion
  detected while the lock is armed". Easy: one GPIO input, poll it like the
  tamper pin. Bonus: it can *suppress* the face/fingerprint attempt when nobody
  is there.
- **Solenoid latch.** Swap the DC motor + L293D for a 12 V solenoid (via a
  MOSFET). A solenoid is **spring-loaded shut** — it only opens while powered —
  so it is inherently fail-secure and draws no current in the locked state.

### B.3 Phase 2 — ESP32 upgrade + IoT (the big step)

Move to the ESP32 (Part A) and add:

- **Cloud access log & dashboard.** Every event that today goes to UART0 also
  goes to MQTT (or Firebase / ThingSpeak / a Telegram bot): `open at 12:08`,
  `wrong password`, `tamper`. A phone dashboard shows who opened it and when.
- **One-time passwords (TOTP).** The phone app (Google Authenticator / Authy)
  generates a 6-digit code that changes every 30 s (RFC 6238). The ESP32 has
  the RTC for the current time and hardware AES for HMAC-SHA1 — it can verify
  the code. This replaces the *fixed* Bluetooth password with a **rotating** one:
  a stolen password is useless 30 seconds later. **This is the single most
  valuable security upgrade.**
- **App-based remote unlock (with an honest warning).** Remote unlock from the
  phone is convenient — but it **turns a physical lock into a network-attacked
  service**. If you add it, keep the two-factor discipline: the app must send
  an OTP *plus* require the physical keypad factor, and every remote unlock is
  audited exactly like a local one. See [SECURITY.md](../SECURITY.md) for the
  model this must not weaken.
- **Encrypted link.** The HC-05 transmits in the clear. The ESP32 can do
  **AES-GCM** on the password payload and/or talk to the phone over **BLE with
  encryption and MITM pairing**, or MQTT over **TLS**. The constant-time compare
  discipline stays, now over a ciphertext channel.

### B.4 Phase 3 — biometrics

#### Fingerprint unlock (R305 / AS608 / FPM10A optical sensor)

**How it works.** An optical fingerprint module has a glass platen, an LED
array and a CCD/CMOS sensor; the built-in chip (e.g. the R30x series
algorithm) extracts a **template** from the ridge pattern — not a stored image.
The module keeps up to ~200 templates in its own flash and answers over a
simple **TTL UART protocol** (packets with a header, a command byte, and a
checksum):

- **Enroll:** put the same finger on 2–3 times → the module builds and stores a
  template and returns an ID.
- **Match/search:** put a finger once → the module scans it against every
  stored template and returns the best match ID + confidence score.
- Typical false-accept rate is ~0.001% — far better than a 4-digit PIN.

**Wiring.** 4 wires: VCC, GND, TX, RX. On the LPC2148 this is the *hard* part —
both UARTs are used (UART0 = log, UART1 = HC-05), so you would need to repurpose
one (e.g. move the HC-05 to the phone only, and put the fingerprint on UART1)
or bit-bang. **On the ESP32 it is trivial** (it has 3 UARTs). The Arduino
ecosystem has mature libraries (e.g. Adafruit fingerprint library).

**Design suggestion.** Make the fingerprint a **third factor or an alternative
Level-2** — never the only factor, and always leave the keypad as a fallback
(oily/sweaty fingers fail in the rain — a locker that locks its owner out is a
bug). Enrollments and deletions must be **admin-only and audited**, exactly like
password changes today.

#### Face unlock (camera module)

**How it works.** Two different problems, often conflated:

- **Face *detection*** — "is there a face in this image, and where?" This is
  cheap and runs on an **ESP32-CAM** (ESP32 + OV2640 camera + 4 MB PSRAM) using
  Espressif's on-chip face-detection library. Good enough to *trigger* a
  login attempt or to point a camera.
- **Face *recognition*** — "which face is it?" Matching the detected face against
  a gallery of known people is the computationally heavy part. On-device options
  are limited (a small Eigenface/LBPH template on the ESP32 works for a tiny
  gallery). The realistic architectures:
  - **ESP32-CAM + local LBPH** — store a few 100×100 templates in flash; match
    in a second or two. Simplest, few-enrolled-users only.
  - **ESP32-CAM + server/PC (recommended)** — the ESP32 captures the frame and
    sends it over WiFi to a PC/Raspberry Pi running OpenCV (`LBPHFaceRecognizer`
    or a DNN); the server decides and replies. Scales to many users, robust
    against lighting if trained well.
  - **Cloud recognition** — the frame goes to a cloud vision service. Easiest
    to build, but adds a network dependency (and a privacy discussion) into a
    lock — see the honest warnings below.

**Design suggestions.** Put the camera so it sees the area *in front* of the
locker; enforce good lighting; require a *live* check (blink or an anti-spoof
model) so a photo of the owner can't open the lock. Treat face as one factor
alongside the keypad (e.g. face *and then* keypad), and keep the keypad
fallback for poor lighting. Every unlock (and every failed match) is audited.

#### RFID/NFC badge (bonus, very cheap)

A 13.56 MHz **RC522** reader + cards (SPI/I2C). Each card is a fixed factor.
Because a card can be cloned, treat it like a *password, not a key* — pair it
with another factor. Ideal use: a **service/admin badge** that must still be
followed by a keypad code.

---

## Part C — A suggested staged roadmap

| Phase | Goal | Rough effort | Key parts |
|---|---|---|---|
| 0 | **Current project** (done) | — | LPC2148 + HC-05 + keypad + EEPROM + RTC + motor |
| 1 | Physical hardening | small | reed switch, PIR, solenoid latch, tamper mesh |
| 2 | ESP32 + IoT | medium | ESP32 (or ESP32-CAM), MQTT/TLS, OTP, dashboard |
| 3 | Biometrics | medium–large | R305 fingerprint, ESP32-CAM face, RC522 badge |
| 4 | Hardened remote access | large | encrypted BLE, AES-GCM payloads, app with OTP, zero-trust remote unlock |

**Suggested order:** 1 → 2 → (OTP first, remote unlock last) → 3 → 4.

---

## What to carry over from this project (do not lose these)

Whatever you build next, these existing properties are the *point* of the
project and should be treated as requirements:

1. **Two factors minimum** — one stolen factor must not open the locker.
2. **Constant-time password compare** — never leak *how much* of a guess was
   right (the `password_match()` discipline).
3. **Lockout with buffer flushing** — 3 wrong tries → lockout; no queued
   credential can act the instant it ends.
4. **Tamper monitored during every phase** — including password entry, lockout,
   and fault screens.
5. **Fully audited changes** — every open, denial, and admin change is
   timestamped and logged.
6. **Honest failure reporting** — a hardware fault (EEPROM bus dead) shows a
   fault screen instead of silently denying, and never counts as a "wrong
   password".
7. **Bounded waits everywhere** — no path may hang forever; the only deliberate
   unbounded wait is the PLL lock.
8. **No `printf`, no plaintext credentials in the log** — the audit trail must
   not leak the passwords.

Add the *new* hardware, keep the *old* invariants.

---

## Further reading

- The architecture these upgrades plug into: [architecture.md](architecture.md)
- The security model they must not weaken: [SECURITY.md](../SECURITY.md)
- The current wiring each new part joins: [hardware/connections.md](hardware/connections.md)
- The component-level working of the current parts: [hardware/component-working.md](hardware/component-working.md)
