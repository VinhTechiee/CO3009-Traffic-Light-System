# Firmware architecture and integration contracts

The application runs on STM32F103RB. The .ioc defines the pin assignments and peripheral settings. The CMake build uses the matching CMSIS startup, STM32CubeF1 HAL, and the repository's 128 KiB Flash / 20 KiB SRAM linker script. Board wiring and electrical calibration are covered in the [board checklist](board-demo-checklist.md).

## Startup and scheduling

Main initializes HAL, the system clock, GPIO, USART2, I2C1, and TIM2. With a measured profile, the board layer preloads both Red decoder codes while the traffic pins are inputs and enables one road's output pins at a time. The runtime initializes traffic, configured buttons, scheduler time, software timers, and mode policy. It configures available I2C services and starts Automatic before enabling the TIM2 interrupt. Registration failure deletes registered tasks and startup requests all-red.

The 8 MHz HSI feeds the PLL through /2 ×16 for 64 MHz SYSCLK/HCLK. APB1 runs at 32 MHz with the ×2 timer clock rule, giving TIM2 64 MHz. Prescaler 7999 and ARR 79 yield 100 Hz nominal. Startup compares the hardware clock configuration and timer counts with the application's 10 ms tick. TIM2's update callback calls only `SCH_Update`. SysTick independently advances the HAL millisecond clock. I2C1 is remapped to PB8/PB9 at 100 kHz. The equal-priority event and error IRQs forward to HAL and then the adapter. TIM2 has higher priority than either I2C IRQ. PA13/PA14 retain SWD while PB3/PB4 are released from JTAG.

| Foreground callback | Period | Responsibility |
| --- | --- | --- |
| `button_task` | 10 ms | Sample the measured inputs, debounce and queue logical events when enabled. |
| `fsm_mode_task` | 10 ms | Consume one event and dispatch the active FSM. |
| `dht20_task` | 10 ms | Advance sensor startup, conversion, receive, or recovery by one bounded step. |
| `lcd_task` | 10 ms | Advance a display initialization or line transfer when enabled. |
| `lcd_display_task` | 100 ms | Format mode, timing, and sensor snapshots and submit changed rows. |
| Sensor restart check | 5 s | Restart a sensor that exhausted its driver retry budget. |

The main loop calls `SCH_Dispatch_Tasks`. The scheduler keeps a foreground relative-delay queue with stable ordering for simultaneous deadlines. Adding a task requires a nonzero initial delay. Period zero creates a one-shot. ISR work is one fixed-width epoch increment. Foreground snapshots of the 64-bit clock save and restore PRIMASK. Dispatch coalesces missed periodic callbacks and resumes on their original cadence. Software timers read scheduler ticks so they account for elapsed time during delayed dispatch. When Automatic is serviced late it starts the next visible phase at that time, preserving its full duration.

## Modes, buttons, and outputs

The application FSMs consume logical events and call the traffic driver without HAL or pin dependencies. Mode takes an Automatic snapshot before exit, cancels the outgoing FSM through its exit API, flushes pending and held input, requests all-red, and enters the destination. Automatic frees its phase timer. Manual freezes the incoming lamp pair when that pair corresponds to a manual phase and advances once per BTN2 press. Tuning holds all-red, changes a draft and preserves the last valid value after a rejected edit. On save, Mode publishes the draft after successful Automatic entry. Cancel restarts Automatic with the existing configuration. Mode timings are held in RAM.

The demo starts with Red 5000 ms, Green 3000 ms and Yellow 2000 ms. Red, Green and Yellow are independently adjustable in 1000 ms steps. Valid ranges are 2000–40000 ms, 1000–30000 ms and 1000–10000 ms respectively. Red must cover Green plus Yellow both in milliseconds and after rounding to scheduler ticks. Automatic runs R/G → R/Y → optional R/R → G/R → Y/R. The optional all-red interval is the excess of Red over Green plus Yellow, applied once per complete cycle.

The button driver requires three consistent samples. BTN2 and USER distinguish long presses at 500 ms and 1500 ms. BTN3/BTN4 emit adjustment repeats after 500 ms, then every 250 ms. When both adjustment buttons are held, adjustment input is suppressed until both release. A held input at mode handoff requires a debounced release before it can act again.

The traffic driver rejects a dual-green request before touching outputs. Per-road decoder codes are validated by the profile. The driver removes the previous Green first and routes two-bit changes through a non-Green intermediate. The physical reset bias and actual lamp transitions remain board checks.

## Shared I2C and presentation

LCD and DHT20 share a copied transport table with the same backend context. The backend admits one owner/token at a time and gives a contending client a short opportunity to acquire the next free slot. HAL interrupts use a bounded private staging buffer. Foreground releases ownership only after completion or after quiescing the peripheral on error or cancellation. The backend reconfigures I2C1 after an abort timeout or sustained bus busy. A device holding SCL or SDA low may require physical isolation and power cycling.

The DHT20 uses 7-bit address 0x38 and a minimum 2 s measurement interval. The driver verifies status and CRC, retains the last good values through transient faults, and reports stale or unavailable data. The display adapter supports an explicitly configured HD44780-compatible panel with a PCF8574/A backpack. It sends bounded packets, backs off on failure, reinitializes the controller and repaints the buffered rows. A failed optional peripheral leaves traffic scheduling active. See [the DHT20 driver](dht20-driver.md) and [board profile calibration](board-demo-checklist.md).

## Evidence and remaining inputs

[Pre-board validation](pre-board-validation.md) records the tested source and results. The team still needs the official Project_Group specification for course-specific gestures and timing rules, and the actual shield at the bench for polarity, truth tables, LCD identity, and measured bus and clock behavior.
