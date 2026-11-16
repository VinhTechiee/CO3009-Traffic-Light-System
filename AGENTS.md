# Team boundaries and conventions

Read README.md and docs/architecture.md first. Preserve existing files and
uncommitted changes; check git status before editing. Coordinate changes to
another owner's module and all shared API changes with affected owners.

| Owner | Modules |
| --- | --- |
| A: Core & Automatic | scheduler, software_timer, fsm_automatic |
| B: Hardware & Manual | CubeIDE project/.ioc, button, traffic_light, fsm_manual |
| C: LCD & Tuning | lcd, dht20, fsm_tuning, fsm_mode |
| Shared | global, API contracts, integration, testing, documentation |

Use Core/Inc and Core/Src, include guards, fixed-width integers, explicit units
and module-prefixed symbols (scheduler uses SCH_*). Avoid hidden mutable globals
and dynamic allocation. Validate inputs and copy configuration values.
Keep FSMs independent of HAL/pins; hardware access belongs in drivers and
CubeMX-generated initialization. Preserve generated user sections. Never invent
pin mappings or .ioc files. Scheduled work must be bounded and nonblocking:
no HAL_Delay or busy waits. Only SCH_Update belongs in the tick ISR; protect
shared scheduler state with short critical sections, not volatile alone.
Prevent both logical and transient physical dual green.

This scaffold contains declarations and source shells only. Do not claim feature
completion, build success or passing tests without actual evidence.
