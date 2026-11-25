# Integration checklist

## Software evidence

- [x] Synchronize the integration branch with the fetched member implementations and review preserved local work.
- [x] Run the existing host tests before edits, then add scheduler, automatic, button, manual, I2C adapter, LCD controller and full application coverage.
- [x] Verify O(1) TIM2 tick handling, one-shot execution, task IDs, relative deadlines and delayed dispatch.
- [x] Verify Automatic, Manual and Tuning behavior, including save, cancel, stale input and the dual-green invariant in host tests.
- [x] Verify shared I2C ownership, timeout, abort, sensor faults, LCD repaint and continued traffic scheduling in host tests.
- [x] Clean-build STM32F103RB firmware and diagnostics and generate fresh ELF, HEX, BIN and map files.

## Physical and course acceptance

- [ ] Compare the provisional button actions and timings against the official Project_Group specification.
- [ ] Verify SWD, pin assignments, button polarity, decoder truth tables and safe reset bias.
- [ ] Identify the LCD controller and backpack. Measure I2C levels and confirm both device addresses.
- [ ] Verify TIM2's 10 ms timing and one-shot execution using the diagnostic image.
- [ ] Rebuild and flash the measured-profile application. Verify Automatic, Manual, Tuning save/cancel, sensor/display updates, I2C fault isolation and lamp safety.

Record actual results using the [board checklist](board-demo-checklist.md). Software test commands, evidence and remaining limitations are in [pre-board validation](pre-board-validation.md).
