# Pre-board validation

**Verdict:** The STM32F103RB firmware is ready for bench calibration and acceptance. The host checks and clean target builds passed. The physical board checks remain pending, and the full feature demo requires a measured board profile.

## Source provenance

- Integration branch `integration/pre-board-final` is based on `b14b71c2cc2f11fc92b2e9319848c3ea29147196` from [origin/main](https://github.com/VinhTechiee/CO3009-Traffic-Light-System/commit/b14b71c2cc2f11fc92b2e9319848c3ea29147196). The opening and final `git fetch --all --prune` succeeded.
- The fetched base already contains Member A's scheduler and Automatic work, including the Automatic update at `b7a3803`, Member B's `origin/feat/Manual_BTN_TF` at `6cee059`, and Member C's `origin/feature/c-lcd-dht20-tuning` at `9502296` and `origin/feature/c-lcd-tuning` at `c05a60b`. The fetched scaffold branch is also an ancestor. No fetched member branch had unmerged commits.
- The local Member C branch at `b115a61` remains available. Its separate timing draft with derived Red was superseded by the integrated Tuning FSM. Relevant uncommitted Member C changes from the preserved nested checkout were ported, including independent Red, Green and Yellow edits, the timing constraint, LCD output, and corresponding tests. Both the nested checkout and `docs/fsm-mode-design.md` remain as local work.
- Firmware source and test commit `6e1cd4c036e23d9b8e6558ff7406710917abe597`.

## Integration changes and modified files

| Area | Finding and change | Files |
| --- | --- | --- |
| A | Delayed dispatch could lose timing information. The ISR now advances a fixed-width clock and foreground maintains relative deadlines with phase-preserving periodic coalescing. Software timers account for scheduler time and Automatic supplies the applied phase for Manual handoff. | `scheduler.*`, `software_timer.*`, `fsm_automatic.*`, `timing_rules.h` |
| B | The base disabled button input until a polarity mask was supplied and used unverified traffic codes. The board profile now requires explicit measured decoder codes before enabling outputs. The driver preloads safe output latches and routes two-bit changes through a non-Green code. Button reads now use the CubeMX pin labels. Debounce, holds, repeats, and the existing Manual FSM passed host tests. | `button.c`, `traffic_light.*`, `board_profile.*`, `Project_Traffic_Light_System.ioc` |
| C | The timing editor derived Red even when Automatic accepted independent Red and clearance. Tuning now shares the timing validator and displays the stored duration. Mode accepts configured adjustment repeats, LCD recovery repaints rows, and the LCD and sensor use one serialized HAL I2C1 transport. | `fsm_mode.*`, `fsm_tuning.*`, `lcd.*`, `lcd_format.*`, `lcd_pcf8574.*`, `i2c_stm32.*` |
| Shared | The base lacked a source-complete build, MSP/IRQ routing, and LCD/DHT20 startup. Main now drives the shared runtime, with optional peripherals isolated from traffic scheduling. The separate diagnostic checks SWD-visible inputs, I2C addresses and scheduler behavior. | `main.c`, `app_runtime.*`, `stm32f1xx_hal_msp.c`, `stm32f1xx_it.c`, `firmware_init.c`, `board_diagnostic.*`, `CMakeLists.txt`, `firmware/STM32F103RBTx_FLASH.ld` |
| Evidence | Extended the host runner, added adapter fakes and the synthetic board profile, and updated build, hardware and runtime documentation. | `tests/`, `README.md`, `docs/` |

For the exact file list, run `git diff --name-status b14b71c..integration/pre-board-final`.

## Host tests

From the repository root on Windows:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/run-c-host-tests.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/run-c-host-tests.ps1 -WslSanitizers
git diff --check
```

| Check | Result | Evidence |
| --- | --- | --- |
| Existing suites at `b14b71c` | PASS | All seven suites completed before edits. |
| Final strict GCC host suite | PASS | All 14 configurations compiled with `-Wall -Wextra -Werror -pedantic -Wconversion -Wshadow` and executed with assertions enabled. |
| AddressSanitizer and UndefinedBehaviorSanitizer | PASS | All 14 configurations compiled and ran under Ubuntu WSL with both sanitizers enabled. WSL emitted a local user-lookup diagnostic while spawning processes. Each compile and test exited zero. |
| Full application | PASS | Used the production task registration with mocked switches, GPIO and shared I2C. Checked sensor data and actual LCD text, save and cancel, exact Automatic phase boundaries after tuning, 30 repeated mode loops, peripheral faults and recovery, and one simulated hour. Asserted no dual Green after each mock GPIO write and scheduler step. |
| Physical board, flash and scope checks | NOT RUN | Use the [board demonstration checklist](board-demo-checklist.md) to record results. |

The focused suites also cover task ID validity and capacity, deletion at each queue position, simultaneous deadlines, zero-delay rejection, one-shots, elapsed software timers, delayed dispatch, automatic cycles, switch bounce, long/repeat and simultaneous button input, alternate decoder codes, tuning bounds, stale inputs, CRC and sensor busy, shared I2C deadlines and cancellation, and LCD packet encoding and repaint.

## Firmware build and inspection

Clean builds used CMake 4.3.1, Ninja 1.13.2, GNU Tools for STM32 14.3.1 from STM32CubeCLT 1.22.0, and STM32CubeF1 V1.8.7. CMake used the vendor STM32F103xB CMSIS startup and system files, and linked the application and HAL at the medium-density target addresses.

```powershell
cmake -S . -B build/firmware -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/firmware --clean-first
cmake -S . -B build/diagnostic -G Ninja -DCMAKE_BUILD_TYPE=Release -DCO3009_BOARD_DIAGNOSTIC=ON
cmake --build build/diagnostic --clean-first
$profile = (Resolve-Path tests/fixtures/board-profile-validation.h).Path
cmake -S . -B build/profile-validation -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCO3009_PROFILE_HEADER=$profile"
cmake --build build/profile-validation --clean-first
arm-none-eabi-readelf -l build/firmware/co3009.elf
arm-none-eabi-objdump -d --disassemble=SCH_Update build/firmware/co3009.elf
```

| Image | Result | Flash / 131072 B | RAM / 20480 B including 1 KiB stack reserve |
| --- | --- | ---: | ---: |
| Default application | PASS | 38088 B (29.06%) | 2792 B (13.63%) |
| Board diagnostic | PASS | 14944 B (11.40%) | 1840 B (8.98%) |
| Application with synthetic hardware profile | PASS | 38160 B (29.11%) | 2792 B (13.63%) |

Each build generated fresh ELF, HEX, BIN and map files. The default application outputs are under `build/firmware`. The board diagnostic is under `build/diagnostic`. The synthetic-profile image under `build/profile-validation` is for compile-time verification. Compiler and linker diagnostics were clear in the final builds. ELF inspection showed vectors at `0x08000000` and separate executable Flash and writable SRAM load segments. The BIN vector entries matched the linked Reset, TIM2, I2C1 event/error and SysTick handler addresses and the initial SRAM stack pointer. The application ELF has no `HAL_Delay` or allocator symbols. The linked scheduler ISR code has a fixed sequence that increments the epoch, with no loop, task-table access or callback. The configured nominal TIM2 rate is 100 Hz. Measuring the actual rate on the board is still required.

| Fresh HEX file | SHA-256 |
| --- | --- |
| `build/firmware/co3009.hex` | `f5a2f5fe0b0cbb296fbc1939713541020d56c00ba6886b0f66d98baab58df70a` |
| `build/diagnostic/co3009-board-diagnostic.hex` | `e4199698ac212db9a824d7b10a6cb017be077c6c0be7011c29b3882acd49b6c9` |
| `build/profile-validation/co3009.hex` | `16549da3d1d212fa544c2ddac9869ed8b47ff7c39ff33170bb3bae7edb075051` |

## Hardware and specification acceptance

- Obtain the Project Announcement, detailed Project_Group handout and any shield schematic. Compare the course-specific button mapping, gestures and timing limits with the demo settings in the [README](../README.md).
- Measure both traffic decoder truth tables and reset bias, each button's idle and active level, and the I2C wiring and pull-up voltage. Identify the installed LCD controller, expander, address, geometry and pin map.
- Use the diagnostic image to verify SWD, nominal 10 ms scheduler ticks, one-shot behavior and both I2C addresses. Flash a fresh application build with the measured profile and record the mode, display, fault and lamp-safety results.

The default build keeps unverified traffic GPIOs high-impedance and leaves buttons and LCD disabled. Traffic timing and DHT20 acquisition can run in that configuration. On a calibrated board, saved timings remain in RAM until reset. A backpack with an incompatible LCD protocol needs its own adapter. A peripheral holding an I2C line low can require hardware isolation or power cycling.
