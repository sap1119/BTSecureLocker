# Contributing to SecureLocker

First off — thanks for taking an interest! This is a small but deliberately
engineered embedded project, and every contribution makes it better. Whether
you are fixing a typo, writing a doc, or hardening the firmware, this guide
tells you how to do it without breaking the things that must never break.

> **New to open source / GitHub?** Read the short **[fork guide](#fork-guide)**
> at the bottom — it walks through the whole flow with commands.

---

## Table of contents

- [Code of conduct](#code-of-conduct)
- [What we're looking for](#what-were-looking-for)
- [How to contribute (the short version)](#how-to-contribute-the-short-version)
- [Setting up a build](#setting-up-a-build)
- [Code style](#code-style)
- [The load-bearing rules (read before touching firmware)](#the-load-bearing-rules)
- [Testing your change](#testing-your-change)
- [Pull request checklist](#pull-request-checklist)
- [Fork guide (for complete beginners)](#fork-guide)

---

## Code of conduct

We follow the project's [Code of Conduct](CODE_OF_CONDUCT.md). Be respectful,
be constructive.

## What we're looking for

- **Bug fixes** — anything in the firmware that behaves wrongly.
- **Documentation** — clearer docs, missing register explanations, better
  beginner guides. This repo's goal is to be a teaching resource.
- **Hardware documentation** — corrections or additions to the wiring
  (`docs/hardware/connections.md`).
- **Test evidence** — if you bench-tested something, add it to
  `docs/testing/`. Nothing in this project has ever been run on hardware by
  its maintainers, so *any* real on-hardware observation is extremely welcome.

## How to contribute (the short version)

1. **Fork** the repo (button in the top-right on GitHub).
2. **Clone** your fork and create a **branch**:
   ```bash
   git clone https://github.com/<you>/secure-locker.git
   cd secure-locker
   git checkout -b fix/level-2-countdown-off-by-one
   ```
3. Make your change. Keep it **small and focused** — one PR, one idea.
4. **Compile-verify** every `.c` file you touched (see below) and confirm zero
   new warnings.
5. **Commit** with a clear message:
   ```
   Fix countdown rounding in Level-2 entry

   The per-second redraw rounded down, so the display reached 0 a second
   early. Round up instead, matching the documented timeout semantics.
   ```
6. **Push** and open a **pull request** to `main`.
7. In the PR description, say *what* changed, *why*, and what you verified.

## Setting up a build

- **Keil µVision** (what the student uses): open `firmware/majorproject12.uvproj`,
  press Build. This is the real target toolchain.
- **Command line (verification / CI):** you need `arm-none-eabi-gcc`.
  ```bash
  cd firmware
  for f in *.c; do
    arm-none-eabi-gcc -c -O1 -Wall -Wextra -std=c89 \
        -I. -Ivendor -D__irq= "$f" -o /tmp/"${f%.c}.o" || exit 1
  done
  ```
  The vendored `firmware/vendor/LPC214x.h` makes this work with **no Keil
  installed**, which is also exactly what the CI workflow runs.

## Code style

- **Strict C89** (`-std=c89`). No C99/C11 features: no `//` comments, no
  mixed declarations, no `stdint.h` (`u8/u16/u32` are used instead, from
  `types.h`).
- **Tabs** for indentation, 4-column visual width.
- Comment density: the existing code is **heavily commented** on purpose.
  Explain *why* the code does what it does, not what it literally does.
- Match the surrounding file's style when you touch it.
- No `printf` / `sprintf` anywhere (see the load-bearing rules).

## The load-bearing rules

These are the invariants that four rounds of work (and a faculty review)
established. **Breaking one silently breaks the security of the locker.**
If your change touches any of these, say so explicitly in your PR.

1. **All password comparisons go through `password_match()`** (security.c).
   It enforces the exact length *and* does a full XOR compare with **no early
   exit** (constant time). Never reintroduce `strcmp()` for a password.
2. **No `printf` / `sprintf`** in the firmware. Audit lines are built with
   `log_event()` / `log_event2()` and tiny static formatters. `printf` pulls
   a large stdio runtime into the ROM image and its format handling is not
   C89-portable on this toolchain.
3. **Password masking:** never log a password or a keypad digit. Log actions
   and outcomes, not keystrokes.
4. **Timer ownership:** Timer0 belongs to `delay_ms()`/`delay_us()` (reset on
   every call). **Timer1 is the `millis()` time base** — started once by
   `timebase_init()` and never touched again. `timebase_init()` must stay the
   first call in `SystemInit_SecureLocker()` after the PLL lines.
5. **`keypad_getkey_timeout(0, …)` means "do not wait", not "wait forever".**
   `keypad_getkey()` no longer exists. No keypad call may block forever.
6. **A live LCD countdown requires the caller to poll `keypad_scan()` itself.**
   The caller-driven poll loops in `read_keypad_password()` and
   `read_menu_password()` are load-bearing — do not collapse them into one
   blocking call or the countdown freezes.
7. **Bound every wait.** The only deliberate unbounded wait in the firmware is
   the PLL-lock spin in `SystemInit_SecureLocker()` (documented in place).
   Any new wait loop must have a bound.
8. **`defines.h` is the single source of truth** for every timeout, threshold
   and pin mask. Don't re-declare constants in a `.c` file.
9. **All RTC access goes through `rtc.c`.** Read with `rtc_get()`
   (tear-free, via CTIME0/CTIME1); write with `rtc_set_time/date/dow()`
   (they pause the clock). Nothing outside `rtc.c` may name `HOUR`, `SEC`, etc.
10. **The RTC boot policy lives in `rtc_init()`.** The 12:00:00 default is
    applied **only when the on-chip RTC is completely unset**. Never restore
    an unconditional `rtc_set_date(1,1,2024)/rtc_set_time(12,0,0)` at boot.
11. **In `UART1_ISR` the CR/LF check must stay BEFORE the `bt_rx_ready`
    check.** Phone apps append CR/LF after `#`; classifying them as trailing
    junk would reject every correct password.
12. **`ensure_default_passwords()` must only run after the EEPROM POST
    passes.** Running it against an absent EEPROM silently bricks the locker.
13. **`bluetooth_selftest()` returns a 4-state code, not a bool** —
    `BT_POST_UART_FAIL=0`, `BT_POST_MODULE_FAIL=1`, `BT_POST_LINK_OK=2`,
    `BT_POST_MODULE_OK=3`. `if (bluetooth_selftest())` is wrong in both
    directions.
14. **Do not restore a pass/fail "AT" probe for the HC-05.** In this wiring
    (KEY/EN unwired = data mode) silence is the *expected* behaviour of a
    healthy module. The layered loopback POST is the correct test.

## Testing your change

- **Compile-verify** (mandatory): the loop above must produce **zero warnings
  from project code**. (The vendored `LPC214x.h` may emit one pre-existing
  `#endif` warning — that's vendor noise, not yours.)
- **Symbol check** (recommended for firmware changes): compile all 12 TUs to
  objects and diff `nm` undefined vs defined. Only `strcmp` and
  `__aeabi_uidiv` may remain unresolved (both come from libc / the compiler
  runtime).
- **Both compile-time switches** must still build if you touched anything near
  them: `BT_KEY_CTRL_ENABLED=1` and `L2_IDLE_TIMER_ARMED_AT_START=0`.
- **On hardware:** if you have the board, run the bench-test procedure in
  [docs/testing/bench-test-procedure.md](docs/testing/bench-test-procedure.md)
  and report what you observed in your PR. If you can't test on hardware, say
  so honestly.

## Pull request checklist

- [ ] One focused change per PR
- [ ] Zero new warnings under the compile-verify loop
- [ ] All 12 TUs still build; symbol check leaves only `strcmp`/`__aeabi_uidiv`
- [ ] Docs updated if behaviour or wiring changed
- [ ] If you touched a load-bearing rule, you said so in the PR description
- [ ] If you bench-tested, you reported the result

---

## Fork guide

*For complete beginners. "Forking" = making your own copy of the repository
on GitHub that you're allowed to change. Nothing here needs a paid account.*

**Step 1 — Fork the repo.** On the GitHub page of `secure-locker`, click the
**Fork** button (top-right). GitHub creates a copy under *your* account.

**Step 2 — Clone it to your computer.** Copy the URL from the green **Code**
button on *your* fork, then:
```bash
git clone https://github.com/<your-username>/secure-locker.git
cd secure-locker
```

**Step 3 — Create a branch.** A branch is a separate line of work that won't
disturb `main`:
```bash
git checkout -b improve-docs
```

**Step 4 — Make your changes** in your editor.

**Step 5 — Verify** (see [Testing your change](#testing-your-change)).

**Step 6 — Commit.** `git add` the specific files you changed, then commit:
```bash
git add docs/getting-started.md
git commit -m "Explain the two-factor flow more clearly"
```

**Step 7 — Push the branch to your fork:**
```bash
git push origin improve-docs
```

**Step 8 — Open a Pull Request.** Back on GitHub, your fork now shows a
banner: *"improve-docs had recent pushes — Compare & pull request."* Click it,
write a short description, and submit. Your PR asks the project owner to
review and merge your changes into `main`.

That's it. If the maintainers suggest changes, make them in the same branch,
commit, and push again — the PR updates automatically. Thanks for contributing!
