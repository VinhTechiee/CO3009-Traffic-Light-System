# CO3009 Two-Road Traffic Light System

Embedded C firmware for the XNUCLEO-F103RB carrier and STM32F103RB MCU. It runs Automatic, Manual and timing adjustment modes for two roads, with an LCD and a DHT20 on shared I2C1. See the [team ownership and conventions](AGENTS.md).

| Status | Scope |
| --- | --- |
| Implemented | Automatic phase timing, button-driven Manual and Tuning modes, mode handoff, O(1) scheduler tick, shared asynchronous I2C, LCD presentation and DHT20 acquisition. |
| Verified in software | 14 host configurations and clean STM32F103RB application and diagnostic builds. See the [validation record](docs/pre-board-validation.md). |
| Pending on hardware | SWD and timer measurements, button polarity, decoder truth tables and reset bias, LCD identification, shared-bus operation and lamp safety. Follow the [board checklist](docs/board-demo-checklist.md). |

## Build and test

Install STM32CubeF1 V1.8.7 and put ARM GCC, CMake and Ninja on PATH. CMake looks for the SDK at `%USERPROFILE%/STM32Cube/Repository/STM32Cube_FW_F1_V1.8.7` by default. Set `-DSTM32CubeF1_ROOT=<absolute path>` for a different location. Use GCC on PATH for host tests.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests/run-c-host-tests.ps1
cmake -S . -B build/firmware -G Ninja -DCMAKE_BUILD_TYPE=Release -DCO3009_BOARD_DIAGNOSTIC=OFF -DCO3009_PROFILE_HEADER=
cmake --build build/firmware --clean-first
cmake -S . -B build/diagnostic -G Ninja -DCMAKE_BUILD_TYPE=Release -DCO3009_BOARD_DIAGNOSTIC=ON -DCO3009_PROFILE_HEADER=
cmake --build build/diagnostic --clean-first
```

The build emits ELF, HEX, BIN and map files at `build/firmware/co3009.*` and `build/diagnostic/co3009-board-diagnostic.*`. The repository-root HEX is a historical upstream artifact.

## Calibrate and operate

Start with the diagnostic. Copy the [board profile template](firmware/profiles/board-profile.template.h) to `firmware/profiles/board.local.h` and fill it from the measured board. Build with that header using `CO3009_PROFILE_HEADER`. The default application keeps traffic outputs high-impedance and leaves buttons and LCD disabled. Use the [calibration and flashing steps](docs/board-demo-checklist.md) to enable the hardware.

The demo boots in Automatic. BTN1 advances to Manual and then Tuning. Tuning can save or cancel back to Automatic. The [FSM design](docs/fsm-mode-design.md) defines gestures, duration limits and the All-Red phase. The [architecture](docs/architecture.md) describes task scheduling, peripheral contracts and recovery. Check the demo settings against the official course handout when available.
