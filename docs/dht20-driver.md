# Cooperative DHT20 driver

## Scope and architecture

The driver is pure C99. It takes a clock and an `i2c_async.h` transport through
its configuration. Firmware supplies the shared STM32 I2C1 backend in
`Core/Src/i2c_stm32.c`. `tests/test_dht20_driver.c` exercises the driver with a
fake transport.

The original `dht20_init()`, `dht20_task()` and `dht20_get_sample()` signatures
and `dht20_sample_t` fields are preserved. New configuration, stop and diagnostic
APIs make dependencies and stale/error behavior explicit. One private aggregate
holds the singleton required by the existing no-argument API. There are no
application globals. All APIs must run serially in foreground, never reentrantly
or directly from an ISR. Callbacks must not reenter the driver.

## Verified protocol references

Verified on 2026-11-16:

1. Aosong/ASAIR, [DHT20 Data Sheet V1.0, May 2021](https://asairsensors.com/wp-content/uploads/2021/09/Data-Sheet-DHT20-Humidity-and-Temperature-Module-ASAIR-V1.0.03.pdf),
   sections 7.3-7.4: 7-bit address `0x38`, startup status mask `0x18`, trigger
   `AC 33 00`, 10 ms pre-measurement wait, 80 ms conversion wait, busy bit 7,
   and initialization of registers `1B`, `1C`, `1E` only when necessary.
2. The [manufacturer's DHT20 product page](https://asairsensors.com/product/dht20-sip-packaged-temperature-and-humidity-sensor/)
   links "The sample program" to
   [AHT20-21 DEMO V1_3(1).rar](https://www.aosong.com/userfiles/files/software/AHT20-21%20DEMO%20V1_3%281%29.rar).
   Downloaded and inspected `AHT20-21 DEMO V1_3/AHT20-21_DEMO_V1_3.c`:
   `main`, `JH_Reset_REG`, and `AHT20_Start_Init` provide the missing repair
   sequence and 500/5/10 ms waits. The archive name says V1_3; the source banner
   says V1.2. Archive SHA-256:
   `89120fe910b1faa7a2cb78576f66778aee0dbe462127b51128c49d3695dc82a6`.
   This DHT20-linked program is labeled AHT20-21; its repair sequence matches
   the DHT20 datasheet's named registers. No unrelated `AHT20_Init` commands,
   bit-banged GPIO mappings, busy waits, or blocking code were imported.
3. V1.3 September 2024 table 8 supplies OTP/calibration status definitions;
   see [protocol references and status policy](dht20-protocol.md). Frame validity
   and conversions remain entirely in the approved parser.

The demo's first `0x70`/`0x71` byte encodes the address and direction. Status is a
one-byte receive at public address `0x38`. The STM32 adapter shifts the address
at the HAL boundary.

## State machine and initialization

`configure -> init -> POWER_UP_WAIT -> STATUS_REQUEST -> TRANSFER_WAIT -> STATUS_CHECK`.

`init` returns `APP_OK` when startup is accepted, not when the sensor is ready.
It starts a conservative 500 ms power-up wait from the call, as in manufacturer
demo `main`. It does not switch sensor power; the application must ensure VDD
is already stable before calling it. Startup busy status fails the attempt.
If status has both bits in `0x18`, initialization needs no repair.

If required, process each register `1B`, `1C`, `1E` exactly once per attempt:

1. Transmit `[register, 00, 00]`.
2. After observed completion, wait at least 5 ms.
3. Receive three bytes; preserve the second and third bytes.
4. After observed completion, wait at least 10 ms.
5. Transmit `[B0 | register, preserved_second, preserved_third]`.
6. Continue with the next register. After the final restore, wait at least
   10 ms, reread status and require nonbusy status with mask `0x18`.

These steps correspond to `CAL_WRITE_REQUEST`, `CAL_READ_DELAY`,
`CAL_READ_REQUEST`, `CAL_RESTORE_DELAY`, `CAL_RESTORE_REQUEST`, `CAL_NEXT`,
and `CAL_FINISH_WAIT`, using the common `TRANSFER_WAIT` state for each transaction.
Still-invalid status after repair fails initialization rather than repeatedly
rewriting registers indefinitely. A new retry starts a new bounded attempt.

## Measurement sequence

`IDLE -> MEASUREMENT_REQUEST -> TRANSFER_WAIT -> CONVERSION_WAIT -> READ_REQUEST
-> TRANSFER_WAIT -> VALIDATE -> IDLE`.

- Wait at least 10 ms after initialization or publication before the next
  trigger, and satisfy the configured measurement interval after last success.
- Transmit `AC 33 00`; after completion wait at least 80 ms without holding a
  bus transaction open. Another client can use the bus during conversion waits.
- Receive seven bytes including CRC and call `dht20_protocol_parse_frame()`.
- On busy with correct CRC, schedule another seven-byte read on a later task
  invocation; do not send another trigger. A conversion deadline bounds this.
- Timestamp the first observed COMPLETE for a seven-byte receive, after the
  poll callback returns. Preserve that timestamp while the frame awaits parsing.
- Publish only on parser success, atomically in foreground. `last_success_ms`
  is the successful frame's observed reception time, not publication time or an
  estimate of the physical sensor conversion instant. A delayed valid frame
  is published as immediately stale when its age is at least `stale_after_ms`.
  CRC/status-invalid frames never replace last-good values or their timestamp.

Each task invocation executes one switch branch and at most one transport
callback, with no driver loops. A completed poll also samples the clock again
to timestamp completion observation. Validation adds at most two fixed six-byte CRC
passes (the second identifies CRC versus OTP diagnostics on an error).

## Transport ownership and cancellation

`i2c_async_t` supplies transmit, receive, poll, and cancel functions plus a backend
context. Configuration copies the table. Backend/context and clock context must
outlive driver use. Each accepted transfer returns a generation-safe token bound
to the driver's stable owner identity. Tokens are opaque; zero is not assumed
invalid. BUSY never transfers ownership and causes retry on a later invocation.

The backend serializes DHT20 and LCD transactions on I2C1.
The driver can own only one transaction; it never cancels by address, bus-wide
reset, or another owner's token. TX data stays unchanged; RX is not touched
until terminal completion. Driver storage remains alive for the program lifetime.

On timeout, restart, or stop, cancellation is cooperative:
`CANCEL_REQUEST -> CANCEL_WAIT`. Cancel is requested once, then poll is called
once per task until COMPLETE/ERROR guarantees all ISR/DMA access has stopped.
Only then may a retry or rebind reuse buffers. `stop` preserves readings and
settles at UNINITIALIZED; `init` during a transfer cancels and then restarts.

If a broken backend cannot quiesce an abort, the driver remains unavailable in
CANCEL_WAIT and never issues new transfers or reuses borrowed memory. There is
no safe software deadline after which it can pretend DMA ownership ended. The
adapter must supply an eventual terminal abort, or system-level recovery must
quiesce the peripheral. This dependency is explicit, not simulated by this layer.

## Configuration, timeouts and retries

All five time intervals are explicit configuration; there are no application
defaults. Values must be nonzero and at most INT32_MAX ms; conversion timeout
must be greater than the sensor's 80 ms wait. `max_retries` is 0..255, counting
additional attempts. Configuration is copied and rejected atomically on invalid
dependencies/values. Rebinding is allowed only when uninitialized/unavailable
and no transfer remains borrowed; otherwise it returns `APP_BUSY`.

- `measurement_interval_ms`: minimum interval from last successful frame reception
  to the next ordinary trigger; retries may occur earlier after a failure.
  The datasheet recommends 2 s sampling to limit self-heating; choose the product
  cadence explicitly. The test value 2000 ms is not an implicit driver default.
- `transfer_timeout_ms`: bounds each request's bus-acquisition window and,
  separately from acceptance time, its transaction-completion window.
- `conversion_timeout_ms`: bounds starting/repeating conversion reads from
  trigger completion. An already accepted read finishes under its transfer
  timeout. Deadline exhaustion reports SENSOR_BUSY; it is a client deadline,
  not an invented manufacturer maximum conversion time.
- `retry_delay_ms`: backoff before a whole new initialization attempt.
- `stale_after_ms`: age at which a retained sample becomes stale.

Use intervals with adequate scheduler/transport margin. For a 10 ms task period,
very short transfer deadlines or a conversion deadline barely above 80 ms can
expire before the next cooperative step. No blocking catch-up or burst of
transactions is used. Time comparison uses unsigned `(now - start) >= interval`.
The clock must remain monotonic modulo 2^32; task/getter observations must be
less than one full clock wrap apart. The same monotonic epoch must persist
across rebinding. If there is a retained sample, changing the clock function or
context is rejected with APP_INVALID_ARGUMENT because timestamps cannot be
translated between unknown clock domains. Rebinding forces retained data stale.

Failures mark retained data stale immediately. Whole-attempt retries include
startup/repair/measurement errors. Retry count resets only on successful sample
publication or explicit `init`, not merely successful status checks, so a device
that initializes but never measures cannot retry forever. After exhaustion the
driver latches UNAVAILABLE, retains the root cause and sets `retry_exhausted`.
Explicit `init` restarts after external recovery; there is no infinite automatic
recovery loop. Error details distinguish contention, transfer, timeout, sensor
busy, CRC, OTP, calibration, and invalid backend responses.

## Samples and API usage

Call `dht20_configure(&transport, clock_ms, clock_context, &config)` before
`dht20_init()`, then schedule `dht20_task()`. Stop through `dht20_stop()` and
continue dispatching until UNINITIALIZED before rebinding dependencies.

`dht20_get_info()` copies state, error, retry count, availability and age:

| Sample status | Meaning |
| --- | --- |
| NEVER | Acquisition started, no successful sample yet |
| FRESH | Successful sample below age threshold and no subsequent known failure |
| STALE | Retained valid sample aged out, failed subsequently, or was restarted |
| UNAVAILABLE | Stopped, exhausted, or waiting for cancellation |

`has_sample` independently indicates whether any last-good values exist.
The age-accounting invariant is: without a new successful measurement, the
retained reception timestamp never changes and sample age never decreases.
Age accumulates using unsigned `(now - previous_observation)` deltas, saturates
at UINT32_MAX, and cannot become fresh again after a full timer wrap. Task and
getter calls update age; time spent in the completed-frame buffer is accumulated
separately by the same rule. Successful publication copies the frame's original
reception timestamp and already accumulated age, rather than resetting age.

Reconfiguration accounts for elapsed time with the old clock/threshold before
copying settings, and preserves both the timestamp and accumulated age. The new
threshold is used for subsequent freshness evaluation. Once stale is observed,
it is latched until another successful frame, so increasing the threshold cannot
revive old data. Natural expiry does not accelerate the configured measurement
cadence. Stop/restart/reinitialization preserve last-good values and timestamp,
force stale/unavailable status as appropriate, and discard unvalidated frames.

Reception observation is not hardware acquisition time: the transport does not
provide a DMA completion timestamp. This layer accounts for buffering after it
observes COMPLETE; actual transfer/interrupt-to-poll latency is bounded by the
transfer deadline and adapter/scheduler behavior, and must be assessed on board.

The legacy getter copies last-good values if present: APP_OK means fresh,
APP_NOT_READY means never/stale, APP_IO_ERROR means unavailable. It leaves output
unchanged before the first measurement; NULL returns APP_INVALID_ARGUMENT.
The sample's `valid` field means protocol-valid last-good data, not fresh data.
Callers must check the return status or diagnostic metadata before displaying
a reading as current. This extends the scaffold's documented readiness behavior
without changing the old function signatures or sample layout. NULL info is
rejected; info before configure returns APP_NOT_READY without modifying output.

## Host validation

The fake clock never sleeps. The fake bus borrows the real driver buffers until
poll/cancel completion, verifies TX storage stability, checks owner/token pairs,
and rejects simultaneous submissions by one client. Every task invocation is
checked for at most one backend call. Synthetic frames are project fixtures,
not claimed manufacturer vectors or hardware recordings.

Tests cover startup and calibration-required startup, exact repair command order,
preserved read-back bytes and minimum waits, successful parser integration,
multiple measurements, CRC/status failures, transient/persistent busy, bus
contention, submit/poll errors, transfer timeout, delayed cancellation, stopping,
bounded retries, explicit restart, last-good preservation, freshness, saturating
age, rollover during startup/conversion/transfer, foreign/stale token isolation,
copied dependencies/configuration and invalid/null input. Repeated calls at
unchanged time do not advance startup or wait synchronously.

Regression fixtures A-F cover 10-second delayed publication, 5-second age
preservation on reconfiguration, reconfiguration across rollover, delayed
invalid frames, repeated interval/retry changes with nondecreasing age, and stale
data staying stale until a new success. Pending-frame age is tested across
rollover and saturation as well. Existing age assertions include the ordinary
10 ms reception-to-validation interval instead of assuming zero publication age.

Additional fixtures inject an I2C error during the second register's repair
read, then require a complete retry from register 1B. Each register returns
distinct bytes; a subsequent measurement returns different temperature/humidity.
A retired token is polled/canceled while a newer same-owner transfer is active.
Stop/restart requests during pending abort and both immediate/delayed terminal
cancellation ERROR are exercised. Exact transfer, bus-acquisition and conversion
deadline boundaries are checked. The fake still does not emulate simultaneous
DMA/ISR execution or every late-callback race; those are adapter-dependent.

Run from repository root in PowerShell:

```powershell
gcc -std=c99 -Wall -Wextra -Werror -pedantic -Wconversion -Wshadow -ICore/Inc Core/Src/dht20_protocol.c tests/test_dht20_protocol.c -o "$env:TEMP/co3009_dht20_protocol_tests.exe"
& "$env:TEMP/co3009_dht20_protocol_tests.exe"
gcc -std=c99 -Wall -Wextra -Werror -pedantic -Wconversion -Wshadow -ICore/Inc Core/Src/dht20.c Core/Src/dht20_protocol.c tests/test_dht20_driver.c -o "$env:TEMP/co3009_dht20_driver_tests.exe"
& "$env:TEMP/co3009_dht20_driver_tests.exe"
git diff --check
```

Host evidence does not establish hardware transfer timing, WCET, electrical
compatibility, bus reliability or board operation. No hardware validation claimed.

## Firmware integration

The STM32 backend uses I2C1 event and error interrupts and stages transfer
bytes in private storage. It matches owner and token on every poll or cancel,
returns BUSY on contention, and copies received bytes to the driver only after
successful completion. Abort or transfer error can reset and reinitialize I2C1.
Continuously held-low bus lines require physical recovery.

The application supplies a monotonic `HAL_GetTick()` clock and schedules
`dht20_task` once per 10 ms in foreground. A sensor supervisor checks every
five seconds and starts a new attempt after the driver's retry budget is
exhausted. This allows sensor reconnection while traffic control keeps running.
The optional LCD shares the backend, uses its own owner and transport buffer,
and displays the sensor status and freshness.

The actual sensor revision, pull-ups, bus-sharing electrical constraints and
recovery behavior require board verification.
