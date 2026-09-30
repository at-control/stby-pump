# STBY Pump V4 Hardware I/O Map

Source of truth: current KiCad V4 schematics and PCB in `Kicad/stby_pump_v4`, plus the separate `Kicad/Display` board.

Wiring notes verified on 2026-09-22 using XML netlists exported by KiCad 9 from
`Kicad/stby_pump_v4/stby_pump_v4.kicad_sch` (including its child sheets) and
`Kicad/Display/Display.kicad_sch`. This verifies schematic connectivity, not
assembled-board continuity or connector viewing orientation. Use pad numbers on
the PCB when making cables; do not infer left/right order from the drawing.

These notes describe the local V4 hardware and firmware. GitHub `main` at
`85f6e9a` still uses the older I/O mapping. Operator functions below are firmware
assignments; the schematic itself only names DI1..6 and IN_DISP1..4.

## Field wiring terminals

All connector references in this section belong to the controller board.

| Connector / pin | Schematic signal | Wiring / local firmware assignment |
|---|---|---|
| J1 / 1 | DC+ | Positive DC supply input, before F1 |
| J1 / 2 | GND | DC supply return |
| J2 / 1 | DI1 | Pump 1 pressure -> OPTO1 -> PC11 (U6 pin 52) |
| J2 / 2 | DI2 | Pump 1 RPM -> OPTO2 -> PC12 (U6 pin 53) |
| J2 / 3 | DI3 | Pump 1 feedback -> OPTO3 -> PD2 (U6 pin 54) |
| J2 / 4 | DI4 | Pump 2 pressure -> OPTO4 -> PB3 (U6 pin 55) |
| J2 / 5 | DI5 | Pump 2 RPM -> OPTO5 -> PB4 (U6 pin 56) |
| J2 / 6 | DI6 | Pump 2 feedback -> OPTO6 -> PB5 (U6 pin 57) |
| J3 / 1 | DO1 | K1 contact 14, Pump 1 switched contact |
| J3 / 2 | DO1_C | K1 contact 13, Pump 1 contact common |
| J4 / 1 | DO2 | K2 contact 14, Pump 2 switched contact |
| J4 / 2 | DO2_C | K2 contact 13, Pump 2 contact common |
| J5 / 1 | AC1_N | Pump 1 AC sensing neutral |
| J5 / 2 | AC1_L | Pump 1 AC sensing line |
| J5 / 3 | AC2_N | Pump 2 AC sensing neutral |
| J5 / 4 | AC2_L | Pump 2 AC sensing line |

J2 has six signal terminals and no return terminal. Each input drives a PC817
LED through its resistor network; the LED cathode and transistor emitter both
connect to board GND. These channels therefore share ground, despite using
optocouplers. Energizing an input pulls its MCU collector signal low. A dry
contact needs an appropriate external excitation supply; it is not wired like
the display's contact-to-ground inputs. Input voltage and load ratings must be
taken from the fitted circuit/components, not from MCU logic voltage.

The local firmware interprets pressure and feedback as active low, but RPM as
active high. Thus an energized RPM optocoupler reads as RPM inactive under the
current configuration; confirm the sensor's intended contact behavior.

Q1/Q2 are internal relay-drive net names, not external powered outputs. U4 O7/O8
sink K1/K2 coils whose other ends connect to +24V. J3/J4 expose the normally-open
relay contact pairs; they do not internally supply +24V to the connected load.
The schematic also has a transistor with reference **Q1** in the power circuit;
do not confuse that component reference with the relay-drive net Q1.

## MCU pins

| Function | STM32F405 pin | Hardware path | Firmware use |
|---|---:|---|---|
| System LED 1 | PC13 | D1 | Status/test output |
| System LED 2 | PC14 | D2 | Alarm/test output |
| HSE crystal | PH0, PH1 | Y1 8 MHz | 168 MHz system clock |
| Output latch | PA4 | U2/U5 RCLK | GPIO output |
| Output clock | PA5 | U2/U5 SRCLK | SPI1 SCK |
| Output data | PA7 | U2 SER | SPI1 MOSI |
| DIP 1..3 | PB0, PB1, PB2 | S1, 10 kOhm pull-ups | DIP3 LED durability test; DIP1/2 reserved |
| DI1 | PC11 | OPTO1 collector | Pressure Pump 1 |
| DI2 | PC12 | OPTO2 collector | RPM Pump 1 |
| DI3 | PD2 | OPTO3 collector | Feedback Pump 1 |
| DI4 | PB3 | OPTO4 collector | Pressure Pump 2 |
| DI5 | PB4 | OPTO5 collector | RPM Pump 2 |
| DI6 | PB5 | OPTO6 collector | Feedback Pump 2 |
| Input parallel load | PB12 | U3 `/PL` | GPIO output |
| Input clock | PB13 | U3 CP | SPI2 SCK |
| Input serial data | PB14 | U3 Q7 | SPI2 MISO |
| SPI2 dummy output | PB15 | PCB no-connect | SPI2 MOSI, not physically used |
| CAN RX/TX | PB8, PB9 | U11 | Reserved, not initialized |
| USB D-/D+ | PA11, PA12 | J13 | Reserved, not initialized |
| NTC | PA0 | TH1 | Reserved, ADC not initialized |

## U3 74HC165 input byte

SPI mode 0, MSB first. After `/PL` is pulsed low, the received byte is:

| Bit | U3 input | Net | Default firmware meaning |
|---:|---|---|---|
| 0 | D0 | IN_DISP1 | Selector Pump 1 |
| 1 | D1 | IN_DISP2 | Selector Pump 2 |
| 2 | D2 | IN_DISP3 | ACK / lamp test |
| 3 | D3 | IN_DISP4 | Reserved |
| 4 | D4 | NC | Unused |
| 5 | D5 | NC | Unused |
| 6 | D6 | AC1_IN | Pump 1 available |
| 7 | D7 | AC2_IN | Pump 2 available |

The KiCad files name the four panel contacts only as `IN_DISP1..4`; they do not state their operator functions. Change the four `STBY_DISP_*_BIT` definitions in `Core/Inc/stby_config.h` if the front-panel cable uses a different order.

## U2/U5 74HC595 output frame

The MCU transmits the far byte first (`U5`), then the near byte (`U2`), and pulses PA4 RCLK. A high register output enables the corresponding ULN2803 low-side channel.

| Register bit | Hardware output | Display meaning |
|---|---|---|
| U2 bit 7 | IND1 | System ready |
| U2 bit 6 | IND2 | Pump 1 ready |
| U2 bit 5 | IND3 | Pump 1 on |
| U2 bit 4 | IND4 | Pump 1 standby |
| U2 bit 3 | IND5 | Pump 2 ready |
| U2 bit 2 | IND6 | Pump 2 on |
| U2 bit 1 | IND7 | Pump 2 standby |
| U2 bit 0 | IND8 | Pressure low |
| U5 bit 0 | IND9 | Standby alarm |
| U5 bit 6 | Q1 | Pump 1 relay |
| U5 bit 7 | Q2 | Pump 2 relay |

`U2/U5 ~OE` are tied to ground. There is no firmware-controlled output enable on this PCB.

## Display cable J8 to display J1/J3

| Controller J8 | Signal | Display destination |
|---:|---|---|
| 1 | +5V | LED common supply |
| 2,4,6,8,10,12,14,16 | IND1..IND8 | D1..D8 cathode paths |
| 15 | IND9 | D9 cathode path |
| 3,5 | GND | Ground |
| 13,11,9,7 | IN_DISP1..4 | J3 pins 1..4 |

Display J3 pins 5 and 6 are ground, so the four panel contacts are active low.

### Full 16-way display cable

Wire controller J8 to display J1 pin-for-pin. The LED functions here follow the
local firmware; display D1..D9 are the physical LED references.

| J8 pin = display J1 pin | Signal | Display destination / function |
|---:|---|---|
| 1 | +5V | LED common supply |
| 2 | IND1 | D1, system ready |
| 3 | GND | Ground |
| 4 | IND2 | D2, Pump 1 ready |
| 5 | GND | Ground |
| 6 | IND3 | D3, Pump 1 on |
| 7 | IN_DISP4 | Display J3 pin 4, reserved |
| 8 | IND4 | D4, Pump 1 standby |
| 9 | IN_DISP3 | Display J3 pin 3, assumed ACK / lamp test |
| 10 | IND5 | D5, Pump 2 ready |
| 11 | IN_DISP2 | Display J3 pin 2, assumed Pump 2 selector |
| 12 | IND6 | D6, Pump 2 on |
| 13 | IN_DISP1 | Display J3 pin 1, assumed Pump 1 selector |
| 14 | IND7 | D7, Pump 2 standby |
| 15 | IND9 | D9, standby alarm |
| 16 | IND8 | D8, pressure low |

Panel switches close display J3 pins 1..4 to J3 pin 5 or 6 (GND). The input
signals have 3.3V pull-ups on the controller. Do not connect the cable's +5V LED
supply to these contact inputs. The selector/ACK assignments remain provisional
until the actual panel harness is identified.

## Programming, configuration, and reserved connectors

| Connector | Pinout / purpose |
|---|---|
| Controller J7, SWD | 1 = +3.3V reference, 2 = GND, 3 = SWDIO (PA13/U6-46), 4 = SWCLK (PA14/U6-49); no NRST pin |
| Controller J6, BOOT0 | 1 = +3.3V, 2 = BOOT0 (U6-60), 3 = GND through R8 10 kOhm; bridge 2-3 for BOOT0 low, 1-2 for high |
| Controller J10 | Pin 1 = U8 regulator output, pin 2 = +3.3V rail; supply link, not a signal input |
| Controller J11 | Closing 1-2 enables R50 120 ohm between CANH and CANL |
| Controller J12 | 1 = CANH, 2 = CANL |
| Controller J13 | 1 = GND, 2 = USB D+, 3 = USB D- |
| Display J5 | 1 = GND, 2 = USB D+, 3 = USB D-; connects to controller J13 pin-for-pin |
| Display J2, USB-B | 1 = VBUS unconnected, 2 = D-, 3 = D+, 4 = GND, 5 = shield unconnected |

The USB data path is separate from the 16-way LED/contact cable. Display USB
VBUS is unconnected, so it does not power the board. CAN and USB are reserved
and not initialized by the current application.

S1 contact order is reversed relative to the firmware DIP numbering:

| S1 contact pair | Signal | MCU |
|---|---|---|
| 1-4 | DIP3 | PB2, U6 pin 28 |
| 2-5 | DIP2 | PB1, U6 pin 27 |
| 3-6 | DIP1 | PB0, U6 pin 26 |

Each DIP signal has a 10 kOhm pull-up to 3.3V; closing the switch reads low.
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


## Bring-up checks tied to this wiring

- Confirm panel contact assignments before relying on the selector or ACK.
- Check each J2 input independently and confirm both its MCU level and firmware meaning, especially RPM polarity.
- Confirm U3 bit order using known display contacts and AC sensing inputs.
- Check IND1..IND9 and K1/K2 independently against the connector tables.
- DIP3 ON enables the LED-only test with relays OFF. All DIPs OFF runs production AUTO.
- Y1 is 8 MHz; HAL/CMSIS HSE definitions now match. Verify physical timing on the assembled board.

## Temporary 595 voltage measurement mode

The optional (currently disabled) `STBY_CONTROL_MODE_PIN_TEST` sends both U5 and U2 0x00 for
3 seconds, then 0xFF for 3 seconds. DIP, AC and ACK do not affect this mode.
Use SWD-only power with 24 V disconnected: both relay-driver inputs go HIGH
together during this diagnostic. Rebuild/reflash normal TEST before applying 24 V.

For either U2 or U5, put the meter black probe on pin 8 (GND). QA..QH are
physical pins 15, 1, 2, 3, 4, 5, 6, 7. Each should alternate near 0 V and its
3.3 V supply. SYS_LED1 marks the HIGH phase; SYS_LED2 marks the LOW phase.
Pin 9 is the serial cascade output, not one of these latched output pins.

Run `build/test-venv/Scripts/python.exe tests/run_bench_tests.py --pin-test`
for the simulated pin-test checks; omit `--pin-test` to check normal TEST.

## Inverter remote/local permission

AC1/AC2 indicate the corresponding inverter remote permission (active = permitted),
not pump-running feedback. Each relay requires its own permission continuously,
including in MANUAL mode. Loss is handled on the next input/control pass (nominal
20 ms loop, plus hardware sensing delay). AUTO cancels the current attempt and
re-evaluates available pumps, retaining the 100 ms relay transfer gap. Permission
loss itself is not recorded as a pump-running feedback timeout. DI3/DI6 remain
separate pump-running feedback inputs with the existing 3-second start timeout.
DIP3 LED durability mode keeps both relays OFF regardless of permission.
