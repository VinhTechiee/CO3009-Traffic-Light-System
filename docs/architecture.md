# Architecture and proposed API contracts

## Status and dependencies

Only README.md existed at inspection. The referenced Project_Group.docx is absent.
These proposed contracts need team review. Sources intentionally implement none
of their declared functions; an application using them will not link yet.
There is no CubeIDE project, HAL, startup, main.c or .ioc. global.c defines no state.

Generated initialization -> drivers and scheduler initialization.
Timer interrupt -> SCH_Update only.
Foreground superloop -> SCH_Dispatch_Tasks -> registered bounded callbacks.
fsm_mode -> button event queue -> active Automatic/Manual/Tuning FSM.
Active FSM -> software_timer and traffic_light; presentation -> lcd and dht20.
All modules -> global types. No FSM may depend on HAL or board pin definitions.

## A: scheduler and timers

SCH_Init/Add_Task/Delete_Task/Update/Dispatch_Tasks initialize, register, cancel,
advance and dispatch work. Delay/period use ticks; delay >= 1; period zero is
one-shot. Reject NULL callbacks/outputs. Return APP_NO_CAPACITY on exhaustion,
APP_NOT_FOUND for invalid handles. Output handles are valid only on success.

Implement a relative-delay queue. Update touches only its head and bounded
readiness bookkeeping, even for simultaneous expirations; never scan tasks or
execute callbacks in the ISR. Foreground dispatch processes ready zero-delay
successors. Insert/delete preserve relative deadlines. Agree capacity, handle
reuse, atomic access, overflow, delayed-dispatch accounting, periodic phase and
callback add/delete semantics before implementation.

software_timer_init/start/cancel/is_expired/tick provide one-shot timers; expiry
is sticky until cancellation. Cancel expired handles before reusing capacity.
Start rejects zero duration and NULL output. Invalid handles return APP_NOT_FOUND.
Tick is scheduled in foreground, not a second ISR time source. Agree timer tick,
capacity, rounding and missed-tick accounting with scheduler integration.

## B/C: drivers

button_init/task/take_event produce debounced logical press/release events.
fsm_mode_task is the sole foreground event consumer and forwards each event once
to the active FSM. Sampling/debounce, queue size/overflow and button mapping are
unresolved. IDs have no assigned application actions.

traffic_light_init/apply are the sole traffic output interface. Init must establish
verified safe outputs before APP_OK. Apply validates the complete pair and rejects
invalid colors or dual green without writes. Safe physical sequencing must remove
green before asserting another green. Error recovery and transition output policy
need agreement. Shared types do not enforce safety; B must implement and verify it.

lcd_init/request_line/task copy bounded text and perform bounded transfer steps.
dht20_init/task/get_sample expose nonblocking measurement steps and copied samples
with explicit units. All pointer-taking status APIs reject NULL with
APP_INVALID_ARGUMENT. void FSM event handlers ignore NULL. Initialization is boot
work; timed device startup must progress via scheduled steps/APP_NOT_READY.
Agree I2C ownership, addresses, dimensions, text capacity, freshness, invalidation,
timeouts and transfer bounds. No hardware configuration has been invented.

## Modes and shared state

Each FSM exposes enter(config), exit(), on_button(event), task(). Entry copies
validated configuration. task runs only for the active mode; behavior is pending.
fsm_mode_init/request/get/task owns selection and routing. Register fsm_mode_task
only for mode execution; it calls the active FSM task. Register driver/timer tasks
separately as needed, avoiding duplicate execution.

Agree transition ordering (exit, timer cancellation, safe output, entry), failure
handling and event flushing before implementation. No boot mode is implied.
Mode get returns APP_NOT_READY before initialization. Request acceptance versus
transition completion remains unresolved. Timing configuration uses milliseconds;
no defaults, allowed ranges or red/yellow/green relationships are assumed.
Shared ownership must settle authoritative storage, validation and publication of
tuned values. global.h contains types only, with no writable globals.

## Unresolved decisions

- Board identity: user says XNUCLEO-F103RB, README setup says NUCLEO-F103RB.
- Pin map, polarities, clocks, TIM2 tick, I2C settings and device addresses.
- Button count/IDs/actions, debounce/repeat and queue overflow policy.
- Timing defaults/ranges/relations, per-road versus common durations, conversion.
- Automatic phase sequence, Manual actions, Tuning commit/cancel behavior.
- Boot mode, mode entry/exit, timer reset/resume, safe state and failure recovery.
- Scheduler/timer capacities, concurrency, handle lifetime and overrun behavior.
- LCD layout, DHT20 cadence/freshness/errors and bounded bus scheduling.

Headers describe proposed signatures, not completed features. No runtime safety
or firmware build is claimed by this scaffold.
