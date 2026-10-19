#ifndef STBY_CONFIG_H
#define STBY_CONFIG_H

/* Firmware behavior selected at compile time. */
#define STBY_CONTROL_MODE_AUTO    0U
#define STBY_CONTROL_MODE_MANUAL  1U
#define STBY_CONTROL_MODE_TEST    2U
/* Temporary 595 pin measurement only: 24 V relay supply MUST be disconnected. */
#define STBY_CONTROL_MODE_PIN_TEST 3U
#define STBY_PIN_TEST_PHASE_MS 3000U

/* DIP1=1, DIP2=2, DIP3=4; displayed binary order is DIP3..DIP1.
 * 0=AUTO, 1=LED blink (relays OFF), 2=all outputs ON (bench only).
 * Other codes hold outputs OFF. */
#ifndef STBY_CONTROL_MODE
#define STBY_CONTROL_MODE STBY_CONTROL_MODE_TEST
#endif
#define STBY_DIP_MODE_AUTO       0U
#define STBY_DIP_MODE_LED_BLINK  1U
#define STBY_DIP_MODE_ALL_ON     2U
#define STBY_LED_TEST_STEP_MS 60U
#define STBY_TEST_DIP_DEBOUNCE_MS 50U

/* AUTO/MANUAL lamp test and relay safety timing. */
#define STBY_OUTPUT_TEST_STEP_MS       200U
#define STBY_RELAY_BREAK_BEFORE_MAKE_MS 100U

/*
 * Revised display harness (J8 on controller to J1/J3 on display board).
 * The schematic names the four contacts IN_DISP1..4 but does not assign their
 * operator functions. This mapping preserves the existing two-bit selector
 * and ACK behavior. IN_DISP4 remains reserved until the panel wiring is named.
 */
#define STBY_DISP_SEL_P1_BIT  0U
#define STBY_DISP_SEL_P2_BIT  1U
#define STBY_DISP_ACK_BIT     2U
#define STBY_DISP_SPARE_BIT   3U

/* Input semantic polarities after reading the MCU/74HC165 logic level. */
#define STBY_PRESSURE_ACTIVE_LEVEL 0U
#define STBY_RPM_ACTIVE_LEVEL      1U
#define STBY_AC_ACTIVE_LEVEL       0U
#define STBY_FEEDBACK_ACTIVE_LEVEL 0U
#define STBY_SELECTOR_ACTIVE_LEVEL 0U
#define STBY_ACK_ACTIVE_LEVEL      0U

#endif /* STBY_CONFIG_H */
