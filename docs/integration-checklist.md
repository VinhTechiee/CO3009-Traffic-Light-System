# Integration checklist

## Shared preparation

- [ ] Obtain Project_Group.docx; reconcile board identity with README.
- [ ] Review headers and resolve architecture.md decisions together.
- [ ] Agree timing validation, configuration ownership and safe mode transitions.

## A: Core & Automatic

- [ ] Implement relative-delay scheduler with O(1) ISR update and one-shot/periodic work.
- [ ] Verify simultaneous deadlines, insert/delete, stale handles, wraparound,
      delayed dispatch, concurrency and callback mutation behavior.
- [ ] Implement software timers and agreed Automatic phases.

## B: Hardware & Manual

- [ ] Create actual CubeIDE/.ioc project; preserve source shells and generated user sections.
- [ ] Document verified pins/polarities, clocks, GPIO, TIM2 and I2C1 setup.
- [ ] Implement debounce/events, safe traffic writes and Manual FSM.
- [ ] Verify reset/startup and logical/transient dual-green prevention on board.

## C: LCD & Tuning

- [ ] Implement bounded LCD/DHT20 progression and shared bus/error handling.
- [ ] Implement mode routing and Tuning with coordinated timer cleanup.
- [ ] Verify text bounds, sample freshness, tuning validation/publication.

## Shared integration and evidence

- [ ] Initialize safe outputs before dispatch; handle initialization/registration failures.
- [ ] Wire one hardware tick to SCH_Update; dispatch callbacks in foreground.
- [ ] Register tasks once with documented periods and bounded execution times.
- [ ] Inspect FSM dependencies and scheduled paths for HAL calls/blocking waits.
- [ ] Build in actual CubeIDE and record version/configuration/diagnostics.
- [ ] Run meaningful checks of implemented logic and record actual results.
- [ ] Verify buttons, timing, all modes, transitions, faults and I2C on board.
- [ ] Add measured build/flash/operate instructions and demo evidence to README.

Every item remains pending; this is a responsibility checklist, not test results.
