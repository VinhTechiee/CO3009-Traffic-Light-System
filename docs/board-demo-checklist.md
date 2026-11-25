# STM32F103RB board demonstration checklist

Use [pre-board validation](pre-board-validation.md) for the host and firmware build evidence. Every measurement and observation below is a physical board acceptance check and remains pending until someone records it at the bench. Record the board revision, Git commit, profile, and measured results together.

## Connect the board

The `.ioc` and `Core/Inc/main.h` use these STM32 pin assignments. The Arduino labels are on the XNUCLEO-F103RB carrier. Verify the traffic decoder and button shield wiring separately against the actual assembly. [Waveshare XNUCLEO manual, Figure 2](https://www.waveshare.com/wiki/upload/d/d1/Xnucleo-UserManual.pdf).

| Function | Carrier pin | STM32 signal |
| --- | --- | --- |
| Road 1 decoder A, B | D3, D4 | PB3, PB5 |
| Road 2 decoder A, B | D5, D6 | PB4, PB10 |
| BTN1, BTN2 | D7, D8 | PA8, PA9 |
| BTN3, BTN4 | D9, D10 | PC7, PB6 |
| USER button | Onboard / Morpho CN7-23 | PC13 |
| I2C1 SCL, SDA | D15, D14 | PB8, PB9, remapped |
| SWDIO, SWCLK | SWD header / Morpho CN7-13, CN7-15 | PA13, PA14 |
| Diagnostic tick and one-shot | D13, Morpho CN10-2 | PA5, PC9 |

- Power down before wiring. Share ground across the MCU, probe, and peripherals. Connect ST-LINK SWDIO, SWCLK, ground, voltage reference, and NRST if available. Confirm the probe sees the target voltage and the STM32F103 medium-density device before programming.
- Attach DHT20 and the verified LCD backpack to SDA/SCL with suitable pull-ups and their specified supply rails. Measure the I2C idle levels before connecting to the MCU. Use level translation if the backpack bus voltage or input thresholds require it.
- Keep the traffic modules disconnected during initial checks. The diagnostic keeps the four MCU traffic outputs high-impedance. Calibrate and verify safe hardware bias before reconnecting either road.

## Build and flash the diagnostic

From the repository root with ARM GCC, CMake, Ninja, STM32CubeProgrammer, and STM32CubeF1 installed, run the commands below. If CMake needs the SDK location, add `"-DSTM32CubeF1_ROOT=C:/path/to/STM32Cube_FW_F1_V1.8.7"` to the configure command.

```powershell
cmake -S . -B build/diagnostic -G Ninja -DCMAKE_BUILD_TYPE=Release -DCO3009_BOARD_DIAGNOSTIC=ON
cmake --build build/diagnostic --clean-first
STM32_Programmer_CLI -c port=SWD -w build/diagnostic/co3009-board-diagnostic.hex -v -rst
```

Expected flash origin is `0x08000000`. Wait for the programming verification to succeed, reset, and open the matching `co3009-board-diagnostic.elf` in a debugger. With the CPU running, the following checks apply.

| Check | Acceptance criterion |
| --- | --- |
| SWD | Connect and halt, inspect registers, then resume. Reconnecting after reset works and PA13/PA14 remain SWD while PB3/PB4 are GPIO. |
| I2C scan | After boot, `board_diagnostic_i2c_scan_finished` is true. `board_diagnostic_i2c_ack[address]` is `1` at each responding 7-bit address in `0x08` through `0x77`. DHT20 should ACK at `0x38`. Scan each device separately with power cycled between wiring changes, then attach both and confirm distinct addresses. An LCD backpack at `0x38` requires changing its address before sharing the bus. |
| Periodic dispatch | PA5 toggles every 10 ms, producing a nominal 20 ms period. Measure while the CPU runs freely. `board_diagnostic_tick_callbacks` increases by about 100 per second. The timing calculation is `64 MHz / (7999 + 1) / (79 + 1) = 100 Hz`. |
| One-shot | PC9 rises 250 ms after the scheduler starts and stays high. Measure relative to the PA5 tick train since the I2C scan precedes scheduler start. `board_diagnostic_one_shots` remains `1`, with `board_diagnostic_one_shot_tick == 25` in an uninterrupted run. |
| Raw button levels | `board_diagnostic_button_high_mask` reflects the physical voltage on BTN1, BTN2, BTN3, BTN4, USER in bits 0–4. Record released and pressed levels for each button. Verify stable idle levels with the button released. The STM32 inputs use `GPIO_NOPULL` and require verified off-chip bias. |

## Calibrate and build the application

1. For each button, set its bit in `CO3009_BUTTON_ACTIVE_LOW_MASK` if pressing drives it low. Clear its bit if pressing drives it high. For example, all five buttons pressed low gives `0x1F`. Use the measured mask from this assembly.
   The diagnostic observes USER. The demo controls use BTN1 through BTN4.
2. With the MCU and the other road disconnected from the decoder under test, apply only the module's documented input logic levels and supply voltage. Measure what each of `A/B = 00, 01, 10, 11` displays, one decoder at a time. Confirm each has one distinct Off, Red, Yellow, and Green code. Map the measured two-bit values into `CO3009_TRAFFIC_ROAD1_{OFF,RED,YELLOW,GREEN}_CODE` and `CO3009_TRAFFIC_ROAD2_{OFF,RED,YELLOW,GREEN}_CODE`. A is bit 1 and B is bit 0. With the controller pins high-impedance, verify that the board bias keeps each road out of Green through power-on and reset. Only then set `CO3009_TRAFFIC_PROFILE_ENABLED` to `1`.
3. Identify the LCD controller, backpack IC, expander pinout, 7-bit address, geometry, and per-row DDRAM start addresses. For a verified HD44780-compatible four-bit module with PCF8574 or PCF8574A, set `CO3009_LCD_PCF8574_ENABLED` to `1` and fill in the macros below. Verify RW is tied low or connected to an otherwise unused expander output. For other LCD hardware, leave the profile disabled pending a compatible adapter.

| LCD profile macros | What to record |
| --- | --- |
| `CO3009_LCD_ROWS`, `CO3009_LCD_COLUMNS` | Module text dimensions, with at least two rows for the application |
| `CO3009_LCD_ADDRESS_7BIT` | Address from isolated scan, excluding the sensor's `0x38` |
| `CO3009_LCD_RS_MASK`, `CO3009_LCD_ENABLE_MASK`, `CO3009_LCD_D4_MASK` … `CO3009_LCD_D7_MASK` | Single-bit masks for the corresponding expander outputs |
| `CO3009_LCD_BACKLIGHT_MASK`, `CO3009_LCD_BACKLIGHT_ACTIVE_HIGH` | Expander output mask and active level. Use mask zero if controlled externally. |
| `CO3009_LCD_ROW1_DDRAM`, `CO3009_LCD_ROW2_DDRAM`, and row 3/4 macros if used | Row starting DDRAM addresses from the verified module |

Create a header with the measured `#define` values and build the application with that header. With the traffic modules still isolated, check the four output pins against the recorded truth tables before attaching both modules for the combined test.

```powershell
cmake -S . -B build/firmware -G Ninja -DCMAKE_BUILD_TYPE=Release -DCO3009_BOARD_DIAGNOSTIC=OFF "-DCO3009_PROFILE_HEADER=C:/path/to/measured-board-profile.h"
cmake --build build/firmware --clean-first
STM32_Programmer_CLI -c port=SWD -w build/firmware/co3009.hex -v -rst
```

## Run the demonstration

1. **Automatic.** Start from reset. Road 1/2 shows Red/Green for 3 s, Red/Yellow for 2 s, Green/Red for 3 s, then Yellow/Red for 2 s. Verify several cycles and check `Automatic` on the LCD.
2. **Manual.** Tap BTN1. Verify `Manual` and that the current Automatic pair is retained after the safe transition. Verify the phase stays fixed without input. Tap BTN2 to advance one phase per press in the same order as Automatic. Hold BTN2 to check it advances only once. Repeat quick taps and verify every complete debounced press advances once.
3. **Tuning and save.** Tap BTN1 from Manual. Verify both roads are red and the LCD shows `Tuning` and `Red 5000 ms`. Tap BTN3 to raise Red to `6000 ms`. Short-press BTN2 to select Green, then tap BTN3 to raise it to `4000 ms`. A further increase is rejected by `Red ≥ Green + Yellow` and the LCD stays at `Green 4000 ms`. Short-press BTN2 again to select Yellow and once more for Red. BTN4 decreases the selected duration by 1 s per tap. BTN3/4 holds repeat after 500 ms and then every 250 ms. Holding both together suppresses adjustments until both release. Hold BTN2 for at least 0.6 s to save and restart Automatic. Verify 4 s Green, 2 s Yellow, and 6 s Red on each road. To demonstrate clearance, re-enter Tuning, raise Red to `7000 ms`, and save. Verify a 1 s all-red phase after Red/Yellow and before Green/Red in each cycle.
4. **Tuning and cancel.** Re-enter Tuning through Manual and verify it displays the committed Red value. Short-press BTN2 twice to select Yellow. Tap BTN4 to show `Yellow 1000 ms`, then tap it again to check lower-bound rejection. Tap BTN1 to cancel and restart Automatic, and verify Yellow is still 2 s. Hold a button across a mode change and verify it cannot trigger a stale action in the new mode before release and a fresh press.
5. **Shared I2C.** Verify that readings of the form `T:24.5C H:48.2%` arrive and refresh about every 2 s. The LCD has a dedicated mode row. With two rows, the tuning value temporarily occupies the sensor row. With three or more rows, sensor and tuning have separate rows. Check display updates when switching modes during a sensor read.
6. **Fault isolation.** Power down and disconnect DHT20, then reboot. Verify sensor status progresses from `No data` to `Unavailable`, with traffic and button control continuing. With a bench harness allowing safe device isolation, repeat with the LCD disconnected and confirm traffic continues. Restore each device and verify display/sensor recovery.
7. **Safety.** Observe decoder inputs and, where possible, both Green lamp signals on a scope or logic analyzer through reset, Automatic handoffs, Manual steps, tuning entry, save, cancel, and repeated mode changes. There must be no interval with both Green outputs asserted. Record reset levels and the capture for the fastest tested transitions.

## Troubleshooting and recovery

| Symptom | Check or recovery |
| --- | --- |
| No SWD connection | Check power, common ground, voltage reference, SWDIO and SWCLK orientation. Try a lower SWD clock. With NRST wired, use `STM32_Programmer_CLI -c port=SWD mode=UR` to reconnect under reset. |
| No button action or false presses | Rescan raw levels, verify idle bias and the per-button mask, then rebuild with the measured profile. Use presses and releases of at least three scheduler samples to pass debounce. |
| Any incorrect lamp color | Isolate both traffic modules before continuing. Check A/B ordering, GPIO selection, shield power, idle bias and per-road codes. Repeat the one-decoder truth-table test. |
| No I2C ACK or stuck bus | Verify common ground, pull-ups, idle SCL/SDA, and remapped PB8/PB9 wiring. Scan each device separately. Power down before isolating a device that holds the bus low. |
| LCD ACKs but text is absent or garbled | Check contrast, panel voltage, RW level, data order, enable mapping, address, geometry and DDRAM starts against the actual backpack. The application leaves the adapter disabled until its profile is selected. |
| Scheduler waveform missing or jittered | Check diagnostic image selection, TIM2 clock, ARR, prescaler, and NVIC. Measure with the CPU running without debugger halts. Compare PA5 and PC9 using the same capture. |

After calibration, retain the profile and acceptance evidence with the integration commit used for the demo. Flash the verified application image as the last step.
