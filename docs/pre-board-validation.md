# Pre-board validation

**Software verification passed. Physical acceptance is pending.** The images are ready for bench calibration using the [board checklist](board-demo-checklist.md).

## Provenance

- `integration/pre-board-final` was based on `b14b71c2cc2f11fc92b2e9319848c3ea29147196`. It already included Member A's scheduler and Automatic work, including `b7a3803`, Member B's `feat/Manual_BTN_TF` at `6cee059`, and Member C's `feature/c-lcd-dht20-tuning` and `feature/c-lcd-tuning` at `9502296` and `c05a60b`. [Upstream base](https://github.com/VinhTechiee/CO3009-Traffic-Light-System/commit/b14b71c2cc2f11fc92b2e9319848c3ea29147196)
- Integration commit `f6704c5` completed the scheduler, HAL wiring, shared I2C, display and mode integration. It included Member C's local independent timing edits and tests. The earlier local C draft core at `b115a61` remains on its original branch.
- The firmware baseline is `6e1cd4c036e23d9b8e6558ff7406710917abe597`. The consistency review consolidated documentation, tracked the reusable profile template, and left firmware behavior and tests unchanged.
- The nested Member C checkout remains intact, including its uncommitted files and stash `0e998ad`. A root-anchored ignore rule prevents accidental staging. Member C's original mode proposal was saved byte-for-byte as `.local/fsm-mode-design.pre-cleanup.md` and its applicable contracts are in the [current FSM design](fsm-mode-design.md).

## Host results

| Check | Result | Coverage |
| --- | --- | --- |
| Upstream baseline at `b14b71c` | PASS | Seven existing suites ran before integration. |
| Strict host GCC | PASS | All 14 configurations with assertions and warnings as errors. |
| ASan and UBSan | PASS | All 14 configurations ran under Ubuntu WSL with both sanitizers enabled. |
| Automatic timing | PASS | Positive timed clearance over repeated cycles, zero clearance, invalid raw and rounded timing, exact phase boundaries and delayed dispatch. |
| Full application | PASS | Save and cancel, 30 mode loops, shared-bus faults and recovery, and one simulated hour. Asserted the dual-green invariant after each mock GPIO write and scheduler step. |
| Physical flash, measurements and acceptance | NOT RUN | See the [board checklist](board-demo-checklist.md). |

Run the host tests with the [README commands](../README.md#build-and-test). Run the same suite with sanitizers in the default WSL distribution and check whitespace with these commands.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/run-c-host-tests.ps1 -WslSanitizers
git diff --check
```

The suites also cover scheduler add/delete, relative deadlines, task IDs and capacity, one-shots, missed periodic deadlines, switch bounce and repeats, Manual progression, stale input, CRC, sensor busy, I2C cancellation and LCD repaint.

## Target builds

Clean Release builds used STM32CubeF1 V1.8.7, ARM GNU 14.3.1 from STM32CubeCLT 1.22.0, CMake 4.3.1 and Ninja 1.13.2. Use the [README commands](../README.md#build-and-test) for the default application and diagnostic. Each build uses the vendor CMSIS startup and produces ELF, HEX, BIN and map files.

| Image | Result | Flash / 131072 B | RAM / 20480 B including 1 KiB stack reserve |
| --- | --- | ---: | ---: |
| Default application | PASS | 38088 B | 2792 B |
| Diagnostic | PASS | 14944 B | 1840 B |
| Application with synthetic test profile | PASS | 38160 B | 2792 B |

The clean builds produced the same HEX digests as the firmware baseline. ELF and vector inspection at that baseline verified the Flash origin, initial SRAM stack pointer and Reset, SysTick, TIM2 and I2C1 handlers. Disassembly confirmed constant work in `SCH_Update`, and the application ELF contained no `HAL_Delay` or allocator symbols. TIM2 is configured for a nominal 10 ms tick. The actual period is a bench measurement.

| HEX file | SHA-256 |
| --- | --- |
| `build/firmware/co3009.hex` | `f5a2f5fe0b0cbb296fbc1939713541020d56c00ba6886b0f66d98baab58df70a` |
| `build/diagnostic/co3009-board-diagnostic.hex` | `e4199698ac212db9a824d7b10a6cb017be077c6c0be7011c29b3882acd49b6c9` |
| `build/profile-validation/co3009.hex` | `16549da3d1d212fa544c2ddac9869ed8b47ff7c39ff33170bb3bae7edb075051` |

The [profile template](../firmware/profiles/board-profile.template.h) is for measured hardware settings. The synthetic profile below is for build verification.

```powershell
$profile = (Resolve-Path tests/fixtures/board-profile-validation.h).Path
cmake -S . -B build/profile-validation -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCO3009_PROFILE_HEADER=$profile"
cmake --build build/profile-validation --clean-first
```

## Acceptance gate

Compare the current gestures and durations with the official course specification when available. Record board calibration, SWD, measured tick and one-shot timing, LCD and DHT20 operation, fault recovery, and a capture of both Green signals. Keep the work on the integration branch until the [physical checks](board-demo-checklist.md#4-run-acceptance) pass.
