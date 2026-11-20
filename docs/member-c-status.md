# Member C status and board integration

The local C branch was `feature/c-lcd-tuning` at `407dc01` before the implementation. The working tree was clean. The architecture scaffold notes predate the implementations in this checkout. The STM32F103RB `.ioc` is present. `Project_Group.docx`, generated application entry point, and LCD hardware adapter were absent.

| Module | At inspection | Current evidence |
| --- | --- | --- |
| Scheduler, software timer, Automatic (A) | Implemented. Scheduler uses CMSIS IRQ intrinsics. Automatic has four phases and validates the red relationship. | All cross-compiled. Real Automatic/timer exercised in the Mode host suite. Scheduler runtime was outside this validation. |
| Button, traffic light, Manual (B) | Implemented with injectable host I/O. Manual consumes BTN2 PRESS and can reapply an incoming phase. | Real modules linked in the Mode suite. The fake checks dual-green after each GPIO write. Physical outputs remain to be measured. |
| Tuning and Mode (C) | Header declarations and source shells. | Implemented and host-tested for range/overflow checks, save/cancel, transition ordering, timer cleanup, held-input flushing and entry failure. |
| LCD (C) | Driver shell. Formatter and sensor presentation already implemented. | Buffered driver and scheduled display task tested with a mock controller and shared asynchronous bus. Physical controller protocol depends on the board. |
| DHT20 protocol and driver (C) | Implemented and previously covered by host fixtures. | Existing suites rerun. New LCD suite shares a fake bus with the real DHT20 driver and exercises both directions of contention and sensor presentation. |

## Mode and timing contract

- Initialize the button, traffic outputs and software timer first. Call `fsm_mode_configure(&settings)` with agreed bindings and timing policy, and `fsm_mode_init(initial_mode, &initial_timing)`. Check status returns. Register only `fsm_mode_task` for mode execution.
- Bindings are per mode. Each binding selects a single PRESS, SHORT or LONG as supported by the current Button driver. BTN2/USER can use distinct SHORT and LONG bindings. RELEASE and REPEAT are excluded from mapped actions. BTN2 PRESS remains owned by Manual while Manual is active. A mapping is needed for cycle/select, increase, decrease, confirm and cancel. Test mappings are fixtures.
- Tuning starts on RED. `RED = GREEN + YELLOW` in milliseconds, matching Automatic. Increment/decrement updates the selected component and derived red atomically. Editing red changes the companion field selected by `red_adjusts`. A range violation or overflow leaves the last valid draft intact, with the rejection available in `fsm_tuning_get`. Mode owns committed durations in RAM. It publishes a confirmed draft only after successful entry into the configured destination. Cancel or an explicit mode switch discards the draft. Choose both destinations in the integration configuration.
- The outgoing mode exits before entry, so Automatic releases its owned timer and Manual clears pending presses. Mode flushes all button IDs, including held buttons, and checks the all-red write before entry. Tuning stays all-red. Entry failure leaves Mode inactive. `fsm_mode_last_status()` reports the failure and an explicit `fsm_mode_request()` can retry with committed timings. Other software timer clients retain their timers.
- **Proposal for A:** Implement `app_status_t fsm_automatic_get_phase(traffic_state_t *phase)`. Return an actual applied snapshot while active, `APP_NOT_READY` when inactive, and `APP_INVALID_ARGUMENT` for NULL. Wire it through `fsm_mode_config_t.automatic_phase` for Auto-to-Manual handoff. Mode snapshots before Auto exit and passes the pair after all-red. With the callback absent or unavailable, Manual begins all-red. The existing void Automatic task can stop internally on error. A runtime status API would be needed for Mode to report that fault separately from transition errors.
- **A and B:** Confirm course min/max/step, the companion color for RED edits, initial mode/configuration, destinations after confirm/cancel, and button gestures. Confirm whether the agreed step and durations should align with `FSM_AUTOMATIC_TICK_MS` to avoid Automatic's per-phase rounding. These settings must come from the course specification or team agreement.

## I2C, LCD and scheduling

- **B:** Supply one `i2c_async_t` instance for LCD and DHT20 on the same peripheral. Both copies must share the same backend context. Implement `transmit`, `receive`, `poll`, `cancel` with stable owners, generation-safe tokens, BUSY on contention, and terminal status only after all ISR/DMA buffer access ends. Choose fair acquisition and verify it under simultaneous display/sensor work. See `Core/Inc/i2c_async.h`.
- **B and C:** Verify LCD controller/backpack, 7-bit address, dimensions, wiring and startup/timing. Supply the protocol through `lcd_config_t.prepare` and `write_line`. Each callback performs a bounded step via the shared backend. A line remains borrowed across APP_BUSY/APP_NOT_READY. Before returning any terminal status, the callback must release LCD line references and retire its bus transfer. Handle bus-acquisition and transfer deadlines, asynchronous cancel, and retry or restart partially completed device operations in the adapter. The test address and dimensions are mock fixtures. Driver storage supports up to 4 rows and 40 columns.
- Call `lcd_configure`, `lcd_init`, then `lcd_display_configure`. Give the mode a dedicated row. Timing and sensor can use separate rows or share a row with timing shown during Tuning. The driver copies and pads each row, coalesces pending refreshes and retries failed lines. The display uses `lcd_format` and `lcd_presentation`; if a stale reading with values is too wide, it shows `Stale` when it fits. Other unrenderable lines show a placeholder.
- Call `dht20_configure(&shared_bus, clock_ms, clock_context, &sensor_settings)` and `dht20_init()`. Use an atomic monotonic millisecond clock and set measurement interval, acquisition/transfer timeout, conversion timeout, retries and freshness for the measured board/scheduler. See [DHT20 driver](dht20-driver.md) for timing margin, timeout/cancel and recovery. On retry exhaustion, use explicit `dht20_init()` after resolving the fault. The presenter labels stale/unavailable measurements.
- **A and B:** Wire `SCH_Update()` to the tick ISR and dispatch foreground callbacks. Register `software_timer_tick`, `button_task`, `fsm_mode_task`, `dht20_task`, `lcd_task` and `lcd_display_task` once via `SCH_Add_Task(fn, delay_ticks, period_ticks, &id)`. Check registration failures and cancel prior tasks if startup cannot proceed. Match timer cadence to the Automatic build setting and button sampling to its existing 10 ms contract. Pick DHT20/LCD periods against driver deadlines and keep display formatting cadence independent of bus progression.

## Validation

Run on Windows from the repository root with GCC on PATH:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/run-c-host-tests.ps1
git diff --check
```

All seven host suites passed with warnings treated as errors. All 15 `Core/Src` units cross-compiled for Cortex-M3 with `STM32F103xB` and `USE_HAL_DRIVER`, using STM32CubeF1 V1.8.7 headers and its HAL configuration template. The objects passed a relocatable module link. The final application link, flash, physical transition timing, GPIO polarity, I2C adapter/abort behavior, LCD controller and DHT20 board measurements remain pending.
