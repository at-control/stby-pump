# Project Notes

> **Current image:** decimal DIP mode 0 (000) runs production AUTO.
> Mode 1 (001, DIP1) runs the LED durability test with relays OFF.
> Mode 2 (010, DIP2) holds all LEDs and both relays ON for bench testing.
> Other codes command all outputs OFF. Modes change live with 50 ms debounce.


## Current baseline

- MCU: STM32F405RGT6, LQFP64
- Hardware source of truth: `Kicad/stby_pump_v4` and `Kicad/Display`
- Firmware package: STM32CubeF4 `1.28.3`
- `.ioc` authoring version: STM32CubeMX `6.15.0`
- Compiler validated: GNU Tools for STM32 `14.3.1`
- System clock: 8 MHz HSE crystal, PLL to 168 MHz
- Current behavior: DIPs OFF = production AUTO; mode 1 = LED blink; mode 2 = all outputs ON (bench)
- Compile-time configuration: `Core/Inc/stby_config.h`
- Exact electrical mapping: `HARDWARE_IO_MAP.md`

## Control behavior

The firmware supports `AUTO`, `MANUAL`, and `TEST` builds.

In `AUTO`:

1. The two-bit selector chooses OFF, Pump 1 primary, or Pump 2 primary.
2. Demand exists when the selected pump has pressure-low active or RPM inactive.
3. The primary starts only when its AC-ready input is active.
4. Missing feedback for 3 seconds causes transfer to the available secondary.
5. If all available pumps fail feedback, both relays turn off and the standby alarm latches.
6. ACK clears the latch and restarts from OFF.

In `MANUAL`, the selector requests one pump directly. Process inputs do not gate the command. The common output encoder still enforces mutual exclusion and a 100 ms break-before-make interval.

The DIP switches encode a decimal mode: DIP1=1, DIP2=2, DIP3=4.
Binary is written DIP3-DIP2-DIP1; ON means closed to GND.

| Decimal | Binary | Switches ON | Operation |
|---|---|---|---|
| 0 | 000 | None | Production AUTO |
| 1 | 001 | DIP1 | LED durability test, relays OFF |
| 2 | 010 | DIP2 | All nine display LEDs, both SYS LEDs and both relays steadily ON |
| 3..7 | 011..111 | Other combinations | All commanded outputs OFF |

Mode 1 repeats 20 IND1 -> IND9 scans followed by 20 all-on/all-off blinks,
with 60 ms per LED step or blink half-period (nominal 13.2 s per pattern).
SYS LEDs alternate during scans and blink together during the blink phase.
Process/panel inputs do not affect either test mode.

Mode 2 is for bench testing with pump starters/field loads disconnected: it
bypasses inverter AC permission and the normal mutual exclusion of the two
relay commands. The LEDs are steady and both relay coils energize if the 24 V
coil supply is present. Unused 595 outputs are kept LOW (U5=0xC1, U2=0xFF).

Modes change live without restarting after 50 ms of stable switch levels.
Each accepted change resets controller state, transmits an OFF frame, and
requires a fresh confirmed 100 ms OFF interval before relay activation.
Failed SPI writes require another successful OFF frame and the full interval.
Returning to mode 0 may start a pump if normal demand and AC permission exist.
DIP1=PB0/S1 contacts 3-6; DIP2=PB1/contacts 2-5; DIP3=PB2/contacts 1-4.



## Input conventions

- Pressure: active low, 200 ms debounce
- RPM: active high, 2 s debounce
- Feedback: active low, 200 ms debounce
- AC-ready: active low
- Selector: active low, 200 ms debounce
- ACK: active low, 50 ms debounce
- ACK long press / lamp test: 1.5 seconds

All six DI channels are PC817 collector outputs with external pull-ups. The display contacts and AC inputs are captured by U3, a 74HC165 read through SPI2.

## Unresolved panel assignment

The current KiCad design electrically labels the four display contacts only as `IN_DISP1..4`. It does not say which physical contact is selector Pump 1, selector Pump 2, ACK, or spare.

The firmware default is:

- `IN_DISP1` = selector Pump 1
- `IN_DISP2` = selector Pump 2
- `IN_DISP3` = ACK / lamp test
- `IN_DISP4` = reserved

These are isolated in `Core/Inc/stby_config.h`. Confirm the external panel harness before energizing Q1 or Q2.

## Output safety

- The current PCB has two 74HC595 devices, not three.
- U2 is nearest to the MCU and drives IND1..IND8 through U1.
- U5 is farthest and drives IND9, Q1, and Q2 through U4.
- The transmitted order is U5 byte first, U2 byte second.
- U2/U5 output-enable pins are tied to ground. PA6 is not connected.
- Firmware writes an all-zero frame at startup.
- If both pump commands are ever requested together, both are forced off.
- A Pump 1/Pump 2 change includes at least 100 ms after a successful all-off latch; SPI failure restarts this requirement.

## Reserved hardware

The revised board includes hardware not yet used by the controller application:

- PA0 NTC analog input
- PB8/PB9 CAN transceiver
- PA11/PA12 USB connector

These pins are not used as substitutes for process inputs. CAN, USB, and ADC middleware remain intentionally uninitialized until their behavior is specified.

## Schematic/PCB review status

- Current main schematic ERC: one warning because `GND` and `DC-` are joined in the power sheet.
- Current main PCB DRC: 23 warning-level track/via items and 19 unconnected GND items.
- Display schematic ERC: clean.
- Display PCB DRC: clean with zero unconnected items.

The main PCB should not be considered fabrication-clean until its GND connectivity is fixed and DRC is rerun. This does not change the firmware mapping above, but it matters for board bring-up.

## Bench validation required

1. Disconnect both pump starters or use low-energy test loads.
2. Confirm the four display-contact functions.
3. Confirm SPI2 input bit order and AC1/AC2 polarity.
4. Confirm U5-first/U2-second output order.
5. Check IND1..IND9 one at a time.
6. Check Q1 and Q2 independently.
7. Measure the 100 ms all-off interval during a pump change.
8. Validate feedback timeout, failover, alarm latch, ACK, and lamp test.

## DIP test validation (2026-09-22)

- HAL and CMSIS HSE definitions corrected to match Y1 at 8 MHz.
- Debug and Release TEST images built successfully.
- ARM simulation passed speed selection, relay/LED sequencing, debounce,
  ACK handling, SPI failure recovery, mutual exclusion, and tick rollover.
- No physical relay or LED operation has been verified by these simulations.

## Inverter remote/local permission

AC1/AC2 indicate the corresponding inverter remote permission (active = permitted),
not pump-running feedback. In production AUTO/MANUAL each relay requires its own permission continuously.
Bench mode 2 is the explicit exception. Loss is handled on the next input/control pass (nominal
20 ms loop, plus hardware sensing delay). AUTO cancels the current attempt and
re-evaluates available pumps, retaining the 100 ms relay transfer gap. Permission
loss itself is not recorded as a pump-running feedback timeout. DI3/DI6 remain
separate pump-running feedback inputs with the existing 3-second start timeout.
Mode 1 keeps both relays OFF. Bench mode 2 explicitly bypasses AC permission
and relay mutual exclusion; the confirmed OFF gap still applies.
