# STM32 Standby Pump Controller

> **Current image:** DIP3 ON runs the LED-only durability test. All DIPs OFF
> runs production AUTO. DIP1/DIP2 are reserved. The test repeats 20 IND1..IND9
> scans, then 20 all-LED blinks, at 60 ms per step. Both relays remain OFF.
> The raw all-output PIN_TEST diagnostic is disabled.


STM32F405RGTx firmware for a manual selector-based standby pump controller built with STM32CubeIDE and HAL.

## Overview

This project supports three control behaviors in the source:

- `AUTO`
- `MANUAL`
- `TEST`

In dual-pump builds:

- The operator selects `OFF`, `PUMP 1`, or `PUMP 2`
- Only one pump may run at a time
- `AUTO` tries the selected pump first, then automatically tries the other pump if feedback fails
- Alarm latches only after all available pumps have failed feedback
- ACK resets the automatic controller and lets it start fresh

The current image uses `STBY_CONTROL_MODE_TEST` as a DIP-selected runtime
controller. **All DIPs OFF runs the production AUTO state machine**, including
selector, pressure/RPM demand, AC availability, feedback timeout, failover,
alarm latch, ACK and the production display indications. Relays do not cycle
on a timer in this state. SYS LEDs are steady ON when either AC input is active;
otherwise SYS_LED2 provides the heartbeat.

DIP3 ON (closed to GND, PB2 / S1 contacts 1-4) selects the LED durability test.
It repeats 20 complete IND1 -> IND9 scans, then 20 all-on/all-off blinks.
Each LED step and blink half-period lasts 60 ms (nominal 13.2 s per pattern).
SYS LEDs alternate during scans and blink together during the all-LED phase.
Both relay commands remain OFF; process/panel inputs, including AC and ACK,
do not affect the pattern. DIP1 and DIP2 are reserved and ignored.

DIP3 changes mode while powered; no restart is needed. Changes must remain
stable for 50 ms. DIP3 ON enters the LED test and commands both relays OFF.
DIP3 OFF returns to AUTO with fresh controller state and a new confirmed
100 ms OFF interval before a relay can start. AUTO still requires the matching
inverter permission and normal demand. DIP1/DIP2 remain reserved and ignored.
Entering test stops running pumps; returning to AUTO may start a pump.


The electrical source of truth and complete shift-register map are documented
in [`HARDWARE_IO_MAP.md`](HARDWARE_IO_MAP.md). The current native KiCad source
is under `Kicad/stby_pump_v4` and `Kicad/Display`.

## Firmware code overview

The project uses a cooperative 20 ms polling loop without an RTOS. CubeMX
startup code configures the 8 MHz HSE/168 MHz clock, GPIO, SPI1 and SPI2. The
application then clears both 74HC595 output registers, samples the inputs once
to prime the debouncers, and enters this repeated flow:

1. `ReadRawInputs()` reads DI1..DI6 directly and captures U3 74HC165 through
   SPI2 for the four panel contacts and two AC-ready inputs.
2. `ProcessInputs()` debounces the raw levels and converts their configured
   electrical polarity into semantic pressure, RPM, feedback, selector, ACK
   and availability states.
3. `RunControlLogic()` selects exactly one compile-time behavior: `AUTO`,
   `MANUAL`, or `TEST`.
4. `UpdateOutputs()` applies relay mutual exclusion and 100 ms
   break-before-make, encodes the two-byte U5/U2 74HC595 frame, and latches it
   through SPI1.
5. `UpdateSysLeds()` updates the two direct MCU LEDs. With DIP3 ON they follow
   the durability pattern; with DIP3 OFF they show production status.

| File | Responsibility |
| --- | --- |
| `Core/Src/main.c` | Input acquisition, debounce, mode logic, alarm state machine, interlocks and shift-register output |
| `Core/Inc/stby_config.h` | Compile-time mode, panel-bit assignments, input polarities and test/interlock timing |
| `Core/Inc/main.h` | CubeMX-generated GPIO port/pin names |
| `Core/Src/stm32f4xx_hal_msp.c` | SPI GPIO alternate-function and peripheral clock setup |
| `Core/Src/stm32f4xx_it.c` | STM32 exception and interrupt handlers |
| `stby_pump.ioc` | CubeMX pin, clock and peripheral configuration source |
| `HARDWARE_IO_MAP.md` | Hardware-authoritative MCU and shift-register mapping |

## Control Logic

Current control logic in `Core/Src/main.c` uses these meanings:

- `AC1_IN` / `AC2_IN` = selected remote pump is available for module control
- `Pressure` input = low-pressure indication on the engine
- `RPM` input = engine can sustain itself without the standby pump

For the selected pump:

In `AUTO` mode:

1. A run request exists when the selected pump sees `pressure_low` active or `rpm` inactive
2. The selected pump is the primary pump; the other pump is the secondary pump
3. On demand, the controller tries the primary pump first if it is ready
4. If the primary has no feedback after `3 s`, the controller tries the secondary pump if it is ready
5. If all available pumps fail feedback, both outputs turn off and alarm latches
6. `ACx_IN` not ready is never a fault; that pump is simply skipped
7. Selector `OFF` or `INVALID` stops the automatic controller without alarm

In `MANUAL` mode:

1. Selecting `PUMP 1` forces Pump 1 on
2. Selecting `PUMP 2` forces Pump 2 on
3. Selecting `OFF` turns the outputs off
4. Pressure, RPM, AC, and feedback do not gate the output command
5. Manual mode is open loop and does not generate standby alarm

Common behavior:

1. `System ready` means the module is powered and running
2. In `AUTO`, `IND9 Standby alarm` follows the latched `3 s` no-feedback alarm
3. `Pump 1 ON` and `Pump 2 ON` always follow live feedback, even if the module output is off

## Alarm / ACK / Lamp Test

- Alarm is latched
- `ACK_LT1` short press clears the alarm latch
- `ACK_LT1` long press activates lamp test and also clears the alarm latch
- Lamp test affects display LEDs only
- Alarm indication blinks at 1 Hz
- Current implementation performs ACK on the press edge, so long press = ACK + lamp test

## Hardware Summary

MCU:

- STM32F405RGTx

Shift register chain:

- `MCU -> U2 -> U5`
- `U2` (nearest) = `IND1..IND8`
- `U5` (farthest) = `IND9`, relay `Q1`, and relay `Q2`
- Both `74HC595` output-enable pins are tied to ground; `PA6` is not connected

Relay/DC mapping:

- `U5` bit 6 (`QG`) = `Q1` / Pump 1 command
- `U5` bit 7 (`QH`) = `Q2` / Pump 2 command
- Firmware enforces a `100 ms` all-off interval when changing pumps

Inputs:

- `DI1 / I1 / PC11` = Pressure switch Pump 1
- `DI2 / I2 / PC12` = RPM switch Pump 1
- `DI3 / I3 / PD2` = Feedback Pump 1
- `DI4 / I4 / PB3` = Pressure switch Pump 2
- `DI5 / I5 / PB4` = RPM switch Pump 2
- `DI6 / I6 / PB5` = Feedback Pump 2
- `IN_DISP1..4` and `AC1_IN/AC2_IN` are read through `U3` (`74HC165`) on SPI2
- `PH0/PH1` are reserved for the 8 MHz HSE crystal

Switch wiring:

- All six direct MCU inputs use PC817 collectors with external `2.7 kOhm` pull-ups
- The four display contacts use external `10 kOhm` pull-ups and are active low
- `AC1_IN` / `AC2_IN` use optocouplers and are active low
- The schematic does not name the operator function of `IN_DISP1..4`; the configurable default is `IN_DISP1=SEL_P1`, `IN_DISP2=SEL_P2`, `IN_DISP3=ACK`, `IN_DISP4=reserved`
- Confirm that display-harness assignment before energizing either relay

### Firmware-used MCU pin map

| STM32F405 signal | Peripheral/GPIO | Board function | Firmware status |
| --- | --- | --- | --- |
| PH0 / PH1 | HSE oscillator | 8 MHz crystal | Active; PLL produces 168 MHz |
| PC13 | GPIO output | System LED 1 | Alternates with SYS_LED2 in TEST |
| PC14 | GPIO output | System LED 2 | Alternates with SYS_LED1 in TEST |
| PA4 | GPIO output | U2/U5 74HC595 RCLK latch | Active |
| PA5 | SPI1 SCK | U2/U5 74HC595 shift clock | Active, SPI mode 0 |
| PA7 | SPI1 MOSI | U2 74HC595 serial data | Active |
| PC11 | GPIO input | DI1 Pressure Pump 1 | Active, opto-isolated |
| PC12 | GPIO input | DI2 RPM Pump 1 | Active, opto-isolated |
| PD2 | GPIO input | DI3 Feedback Pump 1 | Active, opto-isolated |
| PB3 | GPIO input | DI4 Pressure Pump 2 | Active, opto-isolated |
| PB4 | GPIO input | DI5 RPM Pump 2 | Active, opto-isolated |
| PB5 | GPIO input | DI6 Feedback Pump 2 | Active, opto-isolated |
| PB12 | GPIO output | U3 74HC165 active-low parallel load | Active |
| PB13 | SPI2 SCK | U3 74HC165 shift clock | Active, SPI mode 0 |
| PB14 | SPI2 MISO | U3 74HC165 serial data | Active |
| PB15 | SPI2 MOSI | Dummy transmit used to generate read clocks | Configured; PCB no-connect |
| PB0 / PB1 / PB2 | GPIO inputs | Three hardware DIP inputs | DIP3 LED durability test |
| PA0 | Analog input | NTC thermistor | Reserved; ADC not initialized |
| PB8 / PB9 | CAN1 RX/TX nets | CAN transceiver U11 | Reserved; CAN not initialized |
| PA11 / PA12 | USB FS D-/D+ | USB connector J13 | Reserved; USB not initialized |

### Shift-register input map

| Received bit | Hardware input | Default firmware meaning |
| ---: | --- | --- |
| 0 | U3 D0 / `IN_DISP1` | Selector Pump 1 |
| 1 | U3 D1 / `IN_DISP2` | Selector Pump 2 |
| 2 | U3 D2 / `IN_DISP3` | ACK / lamp test |
| 3 | U3 D3 / `IN_DISP4` | Reserved |
| 4 / 5 | U3 D4 / D5 | PCB no-connect |
| 6 | U3 D6 / `AC1_IN` | Pump 1 available |
| 7 | U3 D7 / `AC2_IN` | Pump 2 available |

The first four meanings are firmware defaults, not named operator functions in
the KiCad schematic. Confirm the panel harness before enabling normal control.

## GPIO Notes

Important generated GPIO startup states:

- `SR_LATCH` (`PA4`) starts low
- `SPI2_SH_LD` (`PB12`) starts high
- `SPI1` drives the output chain on `PA5/PA7`
- `SPI2` reads `U3` on `PB12/PB13/PB14`; `PB15` sends an unused dummy byte and is not connected on the PCB
- `PH0/PH1` run the 8 MHz HSE crystal; the MCU clock is 168 MHz
- `PB0/PB1/PB2` select the TEST relay/LED step speed

## LED Mapping

`U2` (nearest output register; ULN wiring reverses the bit order)

- Bit 7 = `IND1` System ready
- Bit 6 = `IND2` Pump 1 ready
- Bit 5 = `IND3` Pump 1 on
- Bit 4 = `IND4` Pump 1 standby
- Bit 3 = `IND5` Pump 2 ready
- Bit 2 = `IND6` Pump 2 on
- Bit 1 = `IND7` Pump 2 standby
- Bit 0 = `IND8` Pressure low

`U5` (farthest output register)

- Bit 0 = `IND9` Standby alarm
- Bit 6 = `Q1` Pump 1 command
- Bit 7 = `Q2` Pump 2 command

Mode indicator meaning:

- `Pump 1 standby` = selector is on Pump 1
- `Pump 2 standby` = selector is on Pump 2

## Build / Import

This repository contains the STM32CubeIDE project files and can be imported directly.

1. Download or clone the repository
2. Open STM32CubeIDE
3. Use `File > Open Projects from File System`
4. Select the extracted project folder
5. Build the project

Notes:

- `Debug/` build artifacts are intentionally not tracked
- STM32CubeIDE will regenerate output files on build
- The `.ioc` file is included, so CubeMX settings can be reopened and regenerated

## Repository Contents

- `Core/` application and generated Cube source
- `Drivers/` STM32 HAL and CMSIS
- `stby_pump.ioc` CubeMX project file
- `.project`, `.cproject`, `.mxproject` CubeIDE project files
- `STM32F405RGTX_FLASH.ld`, `STM32F405RGTX_RAM.ld` linker scripts

## Validation Status

The revised project builds successfully with GNU Tools for STM32 `14.3.1` and STM32CubeF4 `1.28.3`.

Hardware validation is still required for:

- Selector decode
- Confirm the unnamed `IN_DISP1..4` panel-contact assignment before live operation
- ACK versus lamp test timing on the assigned display contact
- pump feedback behavior on `DI3` / `DI6`
- Shift register byte order on the real PCB
- Relay and LED bit mapping on hardware
- SPI2/74HC165 bit order and `AC1_IN` / `AC2_IN` ready behavior
- no-feedback standby alarm behavior
- run/stop behavior from pressure and RPM inputs

## Bench Checklist

- Verify `OFF / PUMP 1 / PUMP 2 / INVALID` selector decoding
- Verify short press on the assigned ACK contact clears the latched alarm
- Verify long press on the assigned ACK contact clears alarm and runs the grouped lamp test
- Verify `Pump 1 ON` follows Pump 1 feedback, including local running
- Verify `Pump 2 ON` follows Pump 2 feedback, including local running
- Verify the two-byte `U2/U5` shift order before connecting pump starters
- Verify the `100 ms` break-before-make interval on a Pump 1/Pump 2 change
- Verify only one pump output can be active at a time in `AUTO`
- Verify `AUTO` tries the selected pump first, then fails over to the other ready pump on feedback loss/timeout
- Verify `AUTO` stays off without alarm when demand exists but neither pump is ready
- Verify `MANUAL` follows selector directly regardless of pressure / RPM / AC / feedback
- Verify `IND9 Standby alarm` only turns on after all available pumps have failed feedback in `AUTO`
- Verify ACK from `AUTO` alarm resets the controller and starts a fresh cycle

## Simulated bench tests

The tests compile the actual controller logic for ARM and run it in Unicorn
with simulated GPIO, SPI, and time. They do not access or energize hardware.

```powershell
python -m venv build/test-venv
& build/test-venv/Scripts/python.exe -m pip install -r tests/requirements.txt
& build/test-venv/Scripts/python.exe tests/run_bench_tests.py
```

Coverage includes all DIP combinations, two complete durability patterns,
relay inhibition, live mode transitions, SPI recovery, mutual exclusion, selector
transfer timing and tick rollover. Physical behavior requires bench checks.

## Inverter remote/local permission

AC1/AC2 indicate the corresponding inverter remote permission (active = permitted),
not pump-running feedback. Each relay requires its own permission continuously,
including in MANUAL mode. Loss is handled on the next input/control pass (nominal
20 ms loop, plus hardware sensing delay). AUTO cancels the current attempt and
re-evaluates available pumps, retaining the 100 ms relay transfer gap. Permission
loss itself is not recorded as a pump-running feedback timeout. DI3/DI6 remain
separate pump-running feedback inputs with the existing 3-second start timeout.
DIP3 LED durability mode keeps both relays OFF regardless of permission.
