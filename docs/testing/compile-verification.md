# Compile Verification — proving the code is correct without Keil

The firmware is normally built with **Keil µVision** (armcc). But Keil is a
Windows-only commercial IDE, so this repo carries a second, free way to *prove
the code is correct*: compile every translation unit with the open-source ARM
GNU toolchain (`arm-none-eabi-gcc`). This is exactly what [CI runs](../../.github/workflows/build.yml)
on every push and pull request.

What it proves: **syntax, types and linkage across all 12 modules**, compiled
strictly, zero warnings. What it does **not** produce: a flashable `.hex` (the
Keil build does that), and it is not a substitute for running on hardware.

## Prerequisites

- A GNU ARM embedded toolchain: `arm-none-eabi-gcc` on your PATH. Install via
  `apt-get install gcc-arm-none-eabi` (Linux), the [ARM GNU Toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)
  Windows installer, or your package manager.
- The vendored device header **`firmware/vendor/LPC214x.h`** (already in the
  repo). It is the NXP/Philips register-definition header for the LPC2148,
  copied verbatim from the Keil installation and vendored so this check needs
  **no Keil at all**.

## The commands

From the `firmware/` directory:

```bash
# 1. Compile every translation unit to an object file
for f in *.c; do
  arm-none-eabi-gcc -c -O1 -Wall -Wextra -std=c89 \
    -I. -Ivendor -D__irq= "$f" -o "/tmp/${f%.c}.o"
done
```

If that loop prints nothing, all 12 modules are syntactically and type-correct.
`-std=c89` enforces the project's ANSI C rules; `-D__irq=` maps Keil's `__irq`
keyword to a no-op so the interrupt handlers compile under GCC.

### Warning check

Because every module compiles cleanly, the whole check is made strict by
treating *any* warning from project code as a failure:

```bash
: > /tmp/build.log
for f in *.c; do
  arm-none-eabi-gcc -c -O1 -Wall -Wextra -std=c89 \
    -I. -Ivendor -D__irq= "$f" -o "/tmp/${f%.c}.o" 2>> /tmp/build.log
done
# Fail on any warning, except the one pre-existing warning inside the vendored
# device header (which is NXP's file, not ours):
grep -E "warning:" /tmp/build.log | grep -v "vendor/LPC214x.h" && echo "FAIL" || echo "CLEAN"
```

### Link check (undefined symbols)

Relocatable-link all the object files from step 1 together, then inspect the
combined undefined symbols. Linking resolves every intra-project reference, so
the only symbols left undefined are genuine external dependencies — `strcmp`
(from the C library — the project's only libc call) and `__aeabi_uidiv` (the
compiler's unsigned division helper):

```bash
# Link all objects into one relocatable image (resolves cross-module refs)
arm-none-eabi-gcc -r -o /tmp/all.o /tmp/*.o

undef=$(arm-none-eabi-nm -u /tmp/all.o | awk '{print $2}' | sort -u)
echo "$undef"
bad=$(echo "$undef" | grep -vE '^(strcmp|__aeabi_uidiv)$' || true)
[ -z "$bad" ] && echo "Link check OK" || echo "Unexpected undefined symbols: $bad"
```

> A per-object `nm -u /tmp/*.o` would list every cross-module reference (each
> module calls functions in the others) and falsely fail — that is why the
> objects must be linked together first.

Anything else listed is a missing definition — e.g. a forgotten function, a
typo in a function name, or a declaration without an implementation.

## Why `strcmp` and `__aeabi_uidiv` are allowed

| Symbol | Supplied by | Where it's used |
|---|---|---|
| `strcmp` | libc | `password_match()` in `security.c` — the one libc call in the whole project |
| `__aeabi_uidiv` | compiler runtime | any `unsigned int` division, e.g. the UART baud-divisor math |

The project deliberately has **no `printf` / `sprintf`** (see
[CONTRIBUTING.md load-bearing rule M24](../../CONTRIBUTING.md)); every timestamp
and number is assembled digit-by-digit. That is why no other libc symbols show
up.

## What CI runs

`.github/workflows/build.yml` runs exactly the three steps above (compile,
warning-gate, link check) on `ubuntu-latest` in a fresh checkout. A green
workflow badge on the repo means "this code compiled cleanly with no Keil
installed" — the same guarantee you get by running the loop locally.

## Real (Keil) build

The human build path is unchanged and documented in
[Getting Started §5](../getting-started.md):

1. Install [Keil µVision](https://www.keil.com/download/product/).
2. Open `firmware/majorproject12.uvproj`.
3. **Build (F7)** → `majorproject12.hex`.
4. Flash with **Flash Magic**: LPC2148 in ISP mode, 9600 baud, 12 MHz crystal.

## Related

- The source-of-truth build rules: [CONTRIBUTING.md](../../CONTRIBUTING.md)
- First power-on sequence after flashing: [Bench test procedure](bench-test-procedure.md)
