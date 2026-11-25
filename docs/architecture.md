# Firmware architecture

The STM32F103RB build uses STM32CubeF1 HAL, CMSIS startup, and the repository linker script for 128 KiB Flash and 20 KiB SRAM. The CubeMX `.ioc` records the peripheral configuration. For the pin assignments, profile settings, and bench measurements, see the [board checklist](board-demo-checklist.md).

## Startup and scheduling

`main` initializes HAL, clocking, GPIO, USART2, I2C1, and TIM2. An enabled traffic profile preloads both Red codes while the decoder pins are inputs, then enables the outputs one road at a time. `app_runtime_start` initializes the traffic and optional services, registers callbacks, and enters Automatic before TIM2 starts. If runtime startup fails, the app removes any registered tasks and requests all-red.

The 8 MHz HSI feeds the PLL through `/2 × 16`. SYSCLK and HCLK are 64 MHz. APB1 runs at 32 MHz and supplies TIM2 at 64 MHz through the timer clock multiplier. Prescaler 7999 and ARR 79 produce a nominal 100 Hz update rate. Startup checks the configured division against the 10 ms scheduler tick. TIM2's application callback calls only `SCH_Update`. SysTick supplies the independent HAL millisecond clock.

| Foreground callback | Period | Responsibility |
| --- | --- | --- |
| `button_task` | 10 ms | Debounce enabled inputs and queue logical events |
| `fsm_mode_task` | 10 ms | Consume one event and dispatch the active mode |
| `dht20_task` | 10 ms | Advance sensor initialization, acquisition, or recovery |
| `lcd_task` | 10 ms | Advance display initialization or a line transfer |
| `lcd_display_task` | 100 ms | Format snapshots and submit changed rows |
| `app_retry_sensor` | 5 s | Restart the sensor after its retry budget is exhausted |

`SCH_Update` increments a 64-bit tick counter in constant time. The foreground snapshots it with PRIMASK saved and restored. `SCH_Dispatch_Tasks` services a relative-delay queue in stable order for equal deadlines. Registration requires a positive initial delay. A zero period makes a task one-shot. Missed periodic callbacks coalesce while the next deadline stays on the original cadence. Software timers use the elapsed scheduler ticks. A late Automatic callback starts the next phase at dispatch and gives it its full duration.

## Input and output boundaries

The FSMs consume logical button events and use the traffic driver without HAL or pin dependencies. The [FSM design](fsm-mode-design.md) describes event routing, mode transitions, tuning limits, and the explicit All-Red phase.

The traffic driver validates each requested pair before writing GPIO. It rejects dual Green, removes an outgoing Green before enabling Green on the other road, and routes a two-bit decoder change through a non-Green code. Physical reset bias and lamp transitions are bench acceptance checks.

## Shared I2C and display

I2C1 is remapped to PB8/PB9 at 100 kHz. The event and error IRQs share a priority below TIM2. PA13/PA14 retain SWD while JTAG releases PB3/PB4.

LCD and DHT20 share an `i2c_async_t` backend. Each accepted transfer has one owner and token. The STM32 adapter shifts 7-bit addresses at the HAL boundary, stages data privately, and copies RX data only after success. It offers a waiting client the next free slot for a bounded interval. Ownership retires after completion or after the adapter quiesces the peripheral on error or cancellation. A prolonged BUSY state or abort timeout reinitializes I2C1. A device holding the bus low may require isolation or power cycling.

The optional LCD adapter targets a profiled HD44780-compatible panel with a PCF8574/A backpack. Transfers advance in bounded steps. Failure triggers a backoff, reinitialization, and repaint of the buffered rows. A two-row display shares its second row between timing in Tuning and the sensor reading elsewhere. Three or more rows permit dedicated timing and sensor rows. Peripheral failures leave traffic scheduling active.

### DHT20

The driver receives a clock and the shared transport through `dht20_configure`. It waits 500 ms at startup, then reads one status byte at address `0x38`. A ready sensor has busy bit 7 clear and both `0x18` bits set. If repair is needed, the driver sends `[register, 0, 0]` for each of `0x1B`, `0x1C`, and `0x1E`, waits at least 5 ms, reads three bytes, waits at least 10 ms, and restores the last two bytes with command `0xB0 | register`. It waits at least 10 ms after the final restore before checking status again. [Aosong DHT20 datasheet, V1.0](https://asairsensors.com/wp-content/uploads/2021/09/Data-Sheet-DHT20-Humidity-and-Temperature-Module-ASAIR-V1.0.03.pdf) · [Aosong reference program](https://www.aosong.com/userfiles/files/software/AHT20-21%20DEMO%20V1_3%281%29.rar)

At least 10 ms after initialization or publication, the driver can send `AC 33 00`. It releases the bus during the 80 ms minimum conversion interval, then reads seven bytes as a single frame. A busy response with valid CRC triggers another read within the conversion deadline. The parser checks an MSB-first CRC-8 over the first six bytes using polynomial `0x31` and initial value `0xFF`, then checks busy, OTP integrity (bit 4), and calibration (bit 3). The two packed 20-bit fields give `raw × 100 / 2^20` percent RH and `raw × 200 / 2^20 − 50` °C. The V1.3 specification documents bit 4, which the V1.0 table labels reserved. [Aosong DHT20 specification, V1.3](https://manuals.plus/m/6fb94a2f5bcc906a21ab17092318ce41f04d0b1bcdb237cbcf11e6dbf6c72dbb)

Application settings are a 2 s minimum measurement interval, a 100 ms limit each for bus acquisition and transfer completion, a 250 ms conversion deadline, and a 5 s freshness threshold. The driver allows two retries with a 2 s backoff. If the retry budget is exhausted, it reports unavailable and the five-second supervisor can restart it. Failed frames preserve the last good sample and its reception timestamp. Age runs from the observed completion of the successful receive, accumulates across clock wrap, and saturates at `UINT32_MAX`. Once stale, the reading stays stale until a valid frame arrives.
