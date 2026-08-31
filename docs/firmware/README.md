# Firmware Behaviour Docs

What the firmware *does*, module by module. These pages sit one level above
the register reference: they explain the behaviour and point at the exact
registers that make it happen. For the "how do the pins/registers work" side,
see [`../registers/`](../registers/README.md).

| Page | Covers |
|---|---|
| [main-flow.md](main-flow.md) | `main()`, the boot self-test, the main-loop state machine, the motor sequence, the lockout |
| [authentication.md](authentication.md) | the Level-1 + Level-2 flow, the dual keypad timers, `password_match()`, failure handling |
| [bluetooth-hc05.md](bluetooth-hc05.md) | the UART1 receive ISR, the protocol, the burst-settle window, the layered boot POST |
| [admin-menu.md](admin-menu.md) | the EINT2 button, the clock/alarm/password sub-menus, the alarm-crossing logic |
| [audit-log.md](audit-log.md) | the log format, what is (and is never) logged, the captured example |

Suggested reading order: **main-flow** (the spine) → **authentication** (the
core security flow) → **bluetooth-hc05** (the wireless factor) → **admin-menu**
(the settings surface) → **audit-log** (the record of it all).
