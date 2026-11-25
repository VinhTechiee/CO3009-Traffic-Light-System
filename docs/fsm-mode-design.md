# Mode and traffic light design

## Mode control

The application starts in Automatic. The button driver debounces inputs and queues logical events. `fsm_mode_task()` consumes at most one event and then services the active FSM. The application FSMs use color commands and logical button events through the driver APIs.

| Mode | Controls and behavior |
| --- | --- |
| Automatic | The scheduler drives the phase timers. BTN1 transfers to Manual using the current output pair. |
| Manual | A BTN2 press advances one phase. A held BTN2 advances once. BTN1 enters Tuning. |
| Tuning | Both roads stay red. A short BTN2 press selects Red, Green, then Amber. BTN3 increases and BTN4 decreases the selected duration. Hold BTN2 to save or press BTN1 to cancel. Both return to Automatic. |

The driver requires three consistent 10 ms samples. BTN2's long threshold is 500 ms after the debounced press. BTN3 and BTN4 repeat after 500 ms and every 250 ms thereafter. Holding both suppresses adjustments until both release. USER has no demo action.

## Handoff and timing ownership

Mode snapshots the applied Automatic pair before a transition to Manual. It then exits the old FSM, flushes queued events and suppresses held inputs, checks an all-red command, and enters the destination. Automatic cancels its own phase timer on exit. Manual starts with the captured pair if it matches a Manual phase, otherwise it starts all-red.

Tuning receives a copy of the committed durations. Rejected edits leave the draft unchanged. Confirm commits the draft to RAM after successful destination entry. Cancel and an explicit departure discard it. A same-mode request keeps the active mode and its state. A failed transition leaves Mode inactive. `fsm_mode_last_status()` reports the transition result, and `fsm_mode_request()` can retry using the committed timings.

| Duration | Default | Range |
| --- | ---: | ---: |
| Red | 5000 ms | 2000–40000 ms |
| Green | 3000 ms | 1000–30000 ms |
| Amber (Yellow in code and on the LCD) | 2000 ms | 1000–10000 ms |

Each edit changes one duration in 1000 ms steps. Both roads share the same configuration. The shared timing validator requires `Red >= Green + Amber` in milliseconds and again after rounding each duration up to 10 ms ticks. The demo values and gestures are provisional until checked against the course handout.

## Automatic cycle

Automatic computes `clearance_ticks = red_ticks - green_ticks - amber_ticks`. The explicit `AUTO_PHASE_BOTH_RED_CLEARANCE` state holds both roads red for that many ticks once per full cycle. When clearance is zero, Automatic advances directly from Road 2 Amber to Road 1 Green.

| Road 1 | Road 2 | Timer |
| --- | --- | --- |
| Red | Green | `green_ticks` |
| Red | Amber | `amber_ticks` |
| Red | Red | `clearance_ticks`, when positive |
| Green | Red | `green_ticks` |
| Amber | Red | `amber_ticks` |

Each road's nominal red interval is the other road's Green plus Amber plus the shared clearance. For example, Red 7 s, Green 4 s and Amber 2 s give a 1 s All-Red phase. The all-red commands used at startup and mode changes are separate output checks without a scheduled dwell. If foreground dispatch is late, the next phase starts when serviced and receives its full duration.

The [Automatic tests](../tests/test_fsm_automatic.c) cover positive clearance across repeated cycles, zero-clearance skipping, invalid rounded timings and late dispatch. The [application test](../tests/test_app_runtime.c) verifies the 1 s transition boundaries after saving an edit.
