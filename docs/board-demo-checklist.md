# Board calibration and acceptance

Record the board revision, firmware commit, measured profile and results. The [validation record](pre-board-validation.md) covers software evidence. All bench checks are pending.

## 1. Connect and flash the diagnostic

Power down before wiring. Disconnect the traffic decoders for initial tests. Connect ST-LINK SWDIO, SWCLK, ground, voltage reference and NRST. Check the target voltage and STM32F103 medium-density device before programming. Check the shield wiring and I2C logic levels against the actual assembly. [XNUCLEO carrier manual, Figure 2](https://www.waveshare.com/wiki/upload/d/d1/Xnucleo-UserManual.pdf)

| Signal | Carrier pin | MCU pin |
| --- | --- | --- |
| Road 1 decoder A/B | D3 / D4 | PB3 / PB5 |
| Road 2 decoder A/B | D5 / D6 | PB4 / PB10 |
| BTN1 / BTN2 | D7 / D8 | PA8 / PA9 |
| BTN3 / BTN4 | D9 / D10 | PC7 / PB6 |
| USER | Onboard / CN7-23 | PC13 |
| I2C1 SCL / SDA | D15 / D14 | PB8 / PB9 |
| SWDIO / SWCLK | SWD header | PA13 / PA14 |
| Diagnostic tick / one-shot | D13 / CN10-2 | PA5 / PC9 |

Use the [README build commands](../README.md#build-and-test) to generate both images. From the repository root, program the diagnostic and confirm verification. Adjust the STM32CubeCLT path to match the installed version.

```powershell
$clt = 'C:\ST\STM32CubeCLT_1.22.0'
$programmer = "$clt\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
& $programmer -c port=SWD -w .\build\diagnostic\co3009-board-diagnostic.hex -v -rst
```

After the flash, close any other probe session. Start the SWD GDB server in the first terminal and open the matching diagnostic ELF in a second terminal at the repository root.

```powershell
& "$clt\STLink-gdb-server\bin\ST-LINK_gdbserver.exe" -d -g -p 61234 -cp "$clt\STM32CubeProgrammer\bin"
```

```powershell
& 'C:\ST\STM32CubeCLT_1.22.0\GNU-tools-for-STM32\bin\arm-none-eabi-gdb.exe' .\build\diagnostic\co3009-board-diagnostic.elf
```

At the GDB prompt, run `target remote localhost:61234`, then `continue`. Press Ctrl+C to inspect variables and `continue` again to resume. [ST-LINK GDB server manual](https://www.st.com/resource/en/user_manual/um2576-stm32cubeide-stlink-gdb-server-stmicroelectronics.pdf)

## 2. Measure the hardware

| Check | Procedure and expected observation |
| --- | --- |
| SWD | Halt, inspect registers and resume. Verify reconnect after reset. |
| Tick and one-shot | `board_diagnostic_tick_callbacks` increases about 100/s. `board_diagnostic_one_shots` stays at 1 and `board_diagnostic_one_shot_tick` is 25. On a freely running target, measure 10 ms between PA5 edges. PC9 rises 250 ms after TIM2 starts, about 240 ms after the first PA5 edge. |
| Raw buttons | Use `p/t board_diagnostic_button_high_mask`. Record released and pressed levels for each of bits 0–4 (BTN1–BTN4 and USER). Set the corresponding polarity bit for a high-to-low press. GPIO inputs use external bias. |
| I2C addresses | After `board_diagnostic_i2c_scan_finished` becomes true, run `p/x board_diagnostic_i2c_ack[0x08]@112`. Entry zero represents 7-bit address `0x08`. A value of 1 marks an ACK. Power down between wiring changes. Scan each device alone, then together. Expect DHT20 at `0x38` and record the LCD's separate address. |
| Decoder truth tables | The diagnostic leaves the traffic pins high-impedance. Disconnect the MCU and test one decoder at a time. Apply the module's documented levels for A/B `00`, `01`, `10` and `11`. Record distinct Off, Red, Amber and Green codes per road, with A as bit 1 and B as bit 0. Check that reset and high-impedance bias keep both roads out of Green. |
| LCD identity | Record the controller, expander, geometry, DDRAM row starts, pin masks and backlight polarity. Enable the adapter for an HD44780-compatible panel with PCF8574/A. Verify that RW is tied low or uses an otherwise unused expander pin. Check I2C pull-ups, voltage compatibility and level shifting. |

The diagnostic waits 500 ms for sensor startup and scans I2C before starting TIM2.

## 3. Build and flash the measured profile

Copy the [tracked template](../firmware/profiles/board-profile.template.h) to `firmware/profiles/board.local.h`. Fill in the measured values and enable the corresponding hardware only after calibration. The `.local.h` profile is ignored by Git. Synthetic profiles live under `tests/fixtures/` for software verification.

```powershell
$profile = (Resolve-Path .\firmware\profiles\board.local.h).Path
cmake -S . -B build/board-acceptance -G Ninja -DCMAKE_BUILD_TYPE=Release -DCO3009_BOARD_DIAGNOSTIC=OFF "-DCO3009_PROFILE_HEADER=$profile"
if ($LASTEXITCODE -ne 0) { throw 'Application configuration failed' }
cmake --build build/board-acceptance --clean-first
```

After a successful build, disconnect GDB and stop its server. Repeat the programmer command with `.\build\board-acceptance\co3009.hex`. Verify the commanded pins with the decoders isolated. Then reconnect both roads with power off.

## 4. Run acceptance

1. **Automatic.** Observe at least two cycles and confirm the mode on the LCD. At defaults the pair sequence is Red/Green (3 s), Red/Amber (2 s), Green/Red (3 s), Amber/Red (2 s).
2. **Manual.** Press BTN1 and verify the current pair is retained. Tap BTN2 for one phase advance. Hold BTN2 and confirm it still advances only one phase.
3. **Tuning.** Press BTN1 from Manual and verify all-red. Raise Red to 6 s, short-press BTN2 to select Green and raise Green to 4 s. Verify another Green increase is rejected. Hold BTN2 for at least 0.6 s to save. Verify the new Automatic timing, then re-enter Tuning, edit Amber and press BTN1 to cancel. Confirm the saved timing is retained. In a separate pass, save Red 7 s, Green 4 s, Amber 2 s and verify 1 s of All-Red after Road 2 Amber.
4. **LCD and DHT20.** Check the mode, selected duration, and temperature and humidity readings. Power down, disconnect each I2C device in turn, and restart. Verify traffic control continues and check recovery after reconnecting.
5. **Lamp safety.** Capture both Green signals through power-on, reset, phase changes and mode handoffs. Verify that Green outputs never overlap. Save the capture with the profile and commit.

If SWD fails, check target voltage and ground, then use `mode=UR` with NRST connected. For a missing I2C ACK, check idle levels and scan devices separately. For wrong colors, isolate the decoders and repeat the truth-table check. A garbled LCD usually calls for rechecking contrast, RW, expander masks and DDRAM offsets.
