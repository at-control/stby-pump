/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 STMicroelectronics.
 * All rights reserved.
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "stby_config.h"
#include <string.h>

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
typedef enum
{
    SELECTOR_OFF = 0,
    SELECTOR_P1,
    SELECTOR_P2,
    SELECTOR_INVALID
} SelectorState_t;

typedef enum
{
    PUMP_STATE_OFF = 0,
    PUMP_STATE_RUNNING
} PumpState_t;

typedef enum
{
    AUTO_STATE_OFF = 0,
    AUTO_STATE_TRY_PRIMARY,
    AUTO_STATE_PRIMARY_RUNNING,
    AUTO_STATE_TRY_SECONDARY,
    AUTO_STATE_SECONDARY_RUNNING,
    AUTO_STATE_ALARM
} AutoState_t;

typedef struct
{
    uint8_t pressure_p1_raw;
    uint8_t rpm_p1_raw;
    uint8_t fb_p1_raw;
    uint8_t pressure_p2_raw;
    uint8_t rpm_p2_raw;
    uint8_t fb_p2_raw;

    uint8_t ac_p1_raw;
    uint8_t ac_p2_raw;

    uint8_t ack_lt1_raw;
    uint8_t sel_p1_raw;
    uint8_t sel_p2_raw;
} RawInputs_t;

typedef struct
{
    uint8_t pressure_p1;
    uint8_t rpm_p1;
    uint8_t fb_p1;
    uint8_t pressure_p2;
    uint8_t rpm_p2;
    uint8_t fb_p2;

    uint8_t ac_p1;
    uint8_t ac_p2;

    uint8_t ack_short;
    uint8_t lamp_test;
    SelectorState_t selector;
} Inputs_t;

typedef struct
{
    uint8_t pump1_cmd;
    uint8_t pump2_cmd;

    uint8_t ind1_system_ready;
    uint8_t ind2_p1_ready;
    uint8_t ind3_p1_on;
    uint8_t ind4_p1_standby;
    uint8_t ind5_p2_ready;
    uint8_t ind6_p2_on;
    uint8_t ind7_p2_standby;
    uint8_t ind8_pressure_low;
    uint8_t ind9_standby_alarm;
} Outputs_t;

typedef struct
{
    PumpState_t state;
    uint32_t state_tick;
} PumpChannel_t;

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define INPUT_ACTIVE_STATE GPIO_PIN_SET
#define LED_ON_STATE GPIO_PIN_SET
#define LED_OFF_STATE ((LED_ON_STATE == GPIO_PIN_SET) ? GPIO_PIN_RESET : GPIO_PIN_SET)
#define PIN_IS_ACTIVE(port, pin) (HAL_GPIO_ReadPin((port), (pin)) == (INPUT_ACTIVE_STATE))
#define MAYBE_UNUSED __attribute__((unused))

#define SR_LATCH_GPIO SR_LATCH_GPIO_Port
#define SR_LATCH_PIN SR_LATCH_Pin
/* ---------------- Configuration ---------------- */
#define CONTROL_MODE_AUTO STBY_CONTROL_MODE_AUTO
#define CONTROL_MODE_MANUAL STBY_CONTROL_MODE_MANUAL
#define CONTROL_MODE_TEST STBY_CONTROL_MODE_TEST
#define CONTROL_MODE_PIN_TEST STBY_CONTROL_MODE_PIN_TEST
#define CONTROL_MODE STBY_CONTROL_MODE

/* Input polarities: set 1 if active high, 0 if active low */
#define PRESSURE_ACTIVE_LEVEL STBY_PRESSURE_ACTIVE_LEVEL
#define RPM_ACTIVE_LEVEL STBY_RPM_ACTIVE_LEVEL
#define AC_ACTIVE_LEVEL STBY_AC_ACTIVE_LEVEL
#define FEEDBACK_ACTIVE_LEVEL STBY_FEEDBACK_ACTIVE_LEVEL
#define SELECTOR_ACTIVE_LEVEL STBY_SELECTOR_ACTIVE_LEVEL
#define ACK_LT1_ACTIVE_LEVEL STBY_ACK_ACTIVE_LEVEL

/* Timing */
#define LOOP_DELAY_MS 20U
#define T_DEBOUNCE_MS 200U
#define T_RPM_DEBOUNCE_MS 2000U
#define T_ACK_DEBOUNCE_MS 50U
#define T_ACK_LONGPRESS_MS 1500U
#define T_BLINK_MS 500U
#define T_FEEDBACK_TIMEOUT_MS 3000U
#define T_OUTPUT_TEST_STEP_MS STBY_OUTPUT_TEST_STEP_MS
#define T_RELAY_BREAK_BEFORE_MAKE_MS STBY_RELAY_BREAK_BEFORE_MAKE_MS

#define FAULT_P1_FEEDBACK_TIMEOUT_MASK (1UL << 0)
#define FAULT_P2_FEEDBACK_TIMEOUT_MASK (1UL << 1)

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
SPI_HandleTypeDef hspi1;
SPI_HandleTypeDef hspi2;

/* USER CODE BEGIN PV */

static RawInputs_t g_raw;
static Inputs_t g_in;
static Outputs_t g_out;

static AutoState_t g_auto_state = AUTO_STATE_OFF;
static uint8_t g_alarm_latched = 0;
static uint8_t g_alarm_blink = 0;
static uint32_t g_auto_state_tick = 0U;
static SelectorState_t g_auto_primary_pump = SELECTOR_OFF;
static uint32_t g_auto_failed_mask = 0U;

static uint32_t g_fault_active_mask = 0U;
static uint32_t g_fault_new_mask = 0U;
static uint32_t g_fault_prev_active_mask = 0U;
static uint32_t g_fault_latched_mask = 0U;

static PumpChannel_t g_p1_channel = {PUMP_STATE_OFF, 0U};
static PumpChannel_t g_p2_channel = {PUMP_STATE_OFF, 0U};

static uint8_t g_last_ack_raw = 0;
static uint32_t g_ack_press_tick = 0;
static uint8_t g_lamp_test_active = 0;
static uint8_t g_lamp_test_group_step = 0U;
static uint8_t g_lamp_test_prev_active = 0U;
static uint32_t g_lamp_test_tick = 0U;
static uint8_t g_test_led_step = 0U;
static uint32_t g_test_step_tick = 0U;
static uint8_t g_test_dip_stable = 0U;
static uint8_t g_test_dip_candidate = 0U;
static uint32_t g_test_dip_tick = 0U;
static uint8_t g_test_initialized = 0U;
static uint8_t g_pin_test_initialized = 0U;
static uint8_t g_pin_test_byte = 0U;
static uint32_t g_pin_test_tick = 0U;
static uint8_t g_test_sys_led1 = 0U;
static uint8_t g_test_sys_led2 = 0U;
/* State of the last successfully latched relay frame. */
static SelectorState_t g_relay_active = SELECTOR_INVALID;
static uint8_t g_relay_off_confirmed = 0U;
static uint32_t g_relay_break_tick = 0U;

/* Debounced values */
static uint8_t db_pressure_p1 = 0;
static uint8_t db_rpm_p1 = 0;
static uint8_t db_pressure_p2 = 0;
static uint8_t db_rpm_p2 = 0;
static uint8_t db_ac_p1 = 0;
static uint8_t db_ac_p2 = 0;
static uint8_t db_sel_p1 = 0;
static uint8_t db_sel_p2 = 0;
static uint8_t db_fb_p1 = 0;
static uint8_t db_fb_p2 = 0;
static uint8_t db_ack_lt1 = 0;

static uint32_t db_tick_pressure_p1 = 0;
static uint32_t db_tick_rpm_p1 = 0;
static uint32_t db_tick_pressure_p2 = 0;
static uint32_t db_tick_rpm_p2 = 0;
static uint32_t db_tick_ac_p1 = 0;
static uint32_t db_tick_ac_p2 = 0;
static uint32_t db_tick_sel_p1 = 0;
static uint32_t db_tick_sel_p2 = 0;
static uint32_t db_tick_fb_p1 = 0;
static uint32_t db_tick_fb_p2 = 0;
static uint32_t db_tick_ack_lt1 = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_SPI1_Init(void);
static void MX_SPI2_Init(void);
/* USER CODE BEGIN PFP */

static void ReadRawInputs(void);
static void ProcessInputs(void);
static void UpdateSysLeds(void);

static void SR_LatchPulse(void);
static void SR_Write16(uint8_t far_u5, uint8_t near_u2);
static uint8_t PISO_Read8(void);
static void ApplyRelayInterlock(void);
static void UpdateOutputs(void);

static uint8_t NormalizeLevel(uint8_t raw_active, uint8_t active_level);
static void DebounceBit(uint8_t raw, uint8_t *db, uint32_t *tick, uint32_t debounce_ms);

static void ClearOutputs(void);
static void RunControlLogic(void);
static MAYBE_UNUSED void RunAutoModeSection(void);
static MAYBE_UNUSED void RunManualModeSection(void);
static MAYBE_UNUSED void RunTestModeSection(void);
static MAYBE_UNUSED void RunOutputTestProgram(void);
static void SetTestIndicatorByOrder(uint8_t index);
static void ApplyLampTestGroupToOutputs(uint8_t group);

static void StopAllPumps(void);
static void EnterPumpChannelState(PumpChannel_t *channel, PumpState_t new_state);
static void PrimeDebouncedInputs(void);

static uint8_t PressureDemandForPump(SelectorState_t pump);
static uint8_t PumpRunRequest(uint8_t pressure_low, uint8_t rpm_active);
static void UpdateAlarmLatch(uint32_t active_mask);
static uint8_t P1Ready(void);
static uint8_t P2Ready(void);
static SelectorState_t OtherPump(SelectorState_t pump);
static uint8_t PumpReady(SelectorState_t pump);
static uint8_t PumpFeedback(SelectorState_t pump);
static uint8_t PumpDemand(SelectorState_t pump);
static uint32_t FeedbackFaultMaskForPump(SelectorState_t pump);
static uint8_t PumpFailedThisCycle(SelectorState_t pump);
static void CommandOnlyPump(SelectorState_t pump);
static void EnterAutoState(AutoState_t new_state);
static void ResetAutoController(void);
static void EnterAutoAlarm(void);

static void ApplyLampTestIfNeeded(void);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

static uint8_t NormalizeLevel(uint8_t raw_active, uint8_t active_level)
{
    return active_level ? raw_active : (uint8_t)!raw_active;
}

static void DebounceBit(uint8_t raw, uint8_t *db, uint32_t *tick, uint32_t debounce_ms)
{
    uint32_t now = HAL_GetTick();

    if (raw != *db)
    {
        if ((now - *tick) >= debounce_ms)
        {
            *db = raw;
            *tick = now;
        }
    }
    else
    {
        *tick = now;
    }
}

static void ReadRawInputs(void)
{
    uint8_t piso = PISO_Read8();

    /* Revised J2 map: DI1..DI6 are the six opto-isolated process inputs. */
    g_raw.pressure_p1_raw = PIN_IS_ACTIVE(I1_GPIO_Port, I1_Pin);
    g_raw.rpm_p1_raw = PIN_IS_ACTIVE(I2_GPIO_Port, I2_Pin);
    g_raw.fb_p1_raw = PIN_IS_ACTIVE(I3_GPIO_Port, I3_Pin);

    g_raw.pressure_p2_raw = PIN_IS_ACTIVE(I4_GPIO_Port, I4_Pin);
    g_raw.rpm_p2_raw = PIN_IS_ACTIVE(I5_GPIO_Port, I5_Pin);
    g_raw.fb_p2_raw = PIN_IS_ACTIVE(I6_GPIO_Port, I6_Pin);

    /* U3 74HC165: D0..D3 = display contacts, D6/D7 = AC1/AC2. */
    g_raw.sel_p1_raw = (uint8_t)((piso >> STBY_DISP_SEL_P1_BIT) & 0x01U);
    g_raw.sel_p2_raw = (uint8_t)((piso >> STBY_DISP_SEL_P2_BIT) & 0x01U);
    g_raw.ack_lt1_raw = (uint8_t)((piso >> STBY_DISP_ACK_BIT) & 0x01U);
    g_raw.ac_p1_raw = (uint8_t)((piso >> 6U) & 0x01U);
    g_raw.ac_p2_raw = (uint8_t)((piso >> 7U) & 0x01U);
}

static void ProcessInputs(void)
{
    uint32_t now = HAL_GetTick();

    DebounceBit(g_raw.pressure_p1_raw, &db_pressure_p1, &db_tick_pressure_p1, T_DEBOUNCE_MS);
    DebounceBit(g_raw.rpm_p1_raw, &db_rpm_p1, &db_tick_rpm_p1, T_RPM_DEBOUNCE_MS);
    DebounceBit(g_raw.fb_p1_raw, &db_fb_p1, &db_tick_fb_p1, T_DEBOUNCE_MS);
    DebounceBit(g_raw.pressure_p2_raw, &db_pressure_p2, &db_tick_pressure_p2, T_DEBOUNCE_MS);
    DebounceBit(g_raw.rpm_p2_raw, &db_rpm_p2, &db_tick_rpm_p2, T_RPM_DEBOUNCE_MS);
    DebounceBit(g_raw.fb_p2_raw, &db_fb_p2, &db_tick_fb_p2, T_DEBOUNCE_MS);
    db_ac_p1 = g_raw.ac_p1_raw;
    db_tick_ac_p1 = now;
    db_ac_p2 = g_raw.ac_p2_raw;
    db_tick_ac_p2 = now;
    DebounceBit(g_raw.sel_p1_raw, &db_sel_p1, &db_tick_sel_p1, T_DEBOUNCE_MS);
    DebounceBit(g_raw.sel_p2_raw, &db_sel_p2, &db_tick_sel_p2, T_DEBOUNCE_MS);
    DebounceBit(g_raw.ack_lt1_raw, &db_ack_lt1, &db_tick_ack_lt1, T_ACK_DEBOUNCE_MS);

    g_in.pressure_p1 = NormalizeLevel(db_pressure_p1, PRESSURE_ACTIVE_LEVEL);
    g_in.rpm_p1 = NormalizeLevel(db_rpm_p1, RPM_ACTIVE_LEVEL);
    g_in.fb_p1 = NormalizeLevel(db_fb_p1, FEEDBACK_ACTIVE_LEVEL);
    g_in.pressure_p2 = NormalizeLevel(db_pressure_p2, PRESSURE_ACTIVE_LEVEL);
    g_in.rpm_p2 = NormalizeLevel(db_rpm_p2, RPM_ACTIVE_LEVEL);
    g_in.fb_p2 = NormalizeLevel(db_fb_p2, FEEDBACK_ACTIVE_LEVEL);

    g_in.ac_p1 = NormalizeLevel(db_ac_p1, AC_ACTIVE_LEVEL);
    g_in.ac_p2 = NormalizeLevel(db_ac_p2, AC_ACTIVE_LEVEL);

    {
        uint8_t sel1 = NormalizeLevel(db_sel_p1, SELECTOR_ACTIVE_LEVEL);
        uint8_t sel2 = NormalizeLevel(db_sel_p2, SELECTOR_ACTIVE_LEVEL);

        if ((sel1 == 0U) && (sel2 == 0U))
            g_in.selector = SELECTOR_OFF;
        else if ((sel1 == 1U) && (sel2 == 0U))
            g_in.selector = SELECTOR_P1;
        else if ((sel1 == 0U) && (sel2 == 1U))
            g_in.selector = SELECTOR_P2;
        else
            g_in.selector = SELECTOR_INVALID;
    }

    g_in.ack_short = 0U;
    g_in.lamp_test = 0U;

    {
        uint8_t ack_local_now = NormalizeLevel(db_ack_lt1, ACK_LT1_ACTIVE_LEVEL);
        uint8_t ack_any_now = ack_local_now;

        if ((ack_any_now == 1U) && (g_last_ack_raw == 0U))
        {
            g_in.ack_short = 1U;
            g_ack_press_tick = now;
            g_lamp_test_active = 0U;
        }

        if (ack_any_now == 1U)
        {
            if ((now - g_ack_press_tick) >= T_ACK_LONGPRESS_MS)
            {
                g_lamp_test_active = 1U;
            }
        }
        else
        {
            g_lamp_test_active = 0U;
        }

        g_in.lamp_test = g_lamp_test_active;
        g_last_ack_raw = ack_any_now;
    }

    g_alarm_blink = (((now / T_BLINK_MS) & 0x01U) != 0U) ? 1U : 0U;
}

static void UpdateSysLeds(void)
{
#if (CONTROL_MODE == CONTROL_MODE_PIN_TEST)
    HAL_GPIO_WritePin(SYS_LED1_GPIO_Port, SYS_LED1_Pin,
                      g_test_sys_led1 ? LED_ON_STATE : LED_OFF_STATE);
    HAL_GPIO_WritePin(SYS_LED2_GPIO_Port, SYS_LED2_Pin,
                      g_test_sys_led2 ? LED_ON_STATE : LED_OFF_STATE);
#elif (CONTROL_MODE == CONTROL_MODE_TEST)
    if (g_test_dip_stable != 0U)
    {
        HAL_GPIO_WritePin(SYS_LED1_GPIO_Port, SYS_LED1_Pin,
                          g_test_sys_led1 ? LED_ON_STATE : LED_OFF_STATE);
        HAL_GPIO_WritePin(SYS_LED2_GPIO_Port, SYS_LED2_Pin,
                          g_test_sys_led2 ? LED_ON_STATE : LED_OFF_STATE);
    }
    else
    {
        /* Production status: AC presence is steady, otherwise heartbeat. */
        HAL_GPIO_WritePin(SYS_LED1_GPIO_Port, SYS_LED1_Pin,
                          (g_in.ac_p1 || g_in.ac_p2) ? LED_ON_STATE : LED_OFF_STATE);
        HAL_GPIO_WritePin(SYS_LED2_GPIO_Port, SYS_LED2_Pin,
                          (g_in.ac_p1 || g_in.ac_p2 || g_alarm_blink) ? LED_ON_STATE : LED_OFF_STATE);
    }
#else
    HAL_GPIO_WritePin(SYS_LED1_GPIO_Port, SYS_LED1_Pin, LED_OFF_STATE);
    HAL_GPIO_WritePin(SYS_LED2_GPIO_Port, SYS_LED2_Pin,
                      g_alarm_blink ? LED_ON_STATE : LED_OFF_STATE);
#endif
}

static void SR_LatchPulse(void)
{
    HAL_GPIO_WritePin(SR_LATCH_GPIO, SR_LATCH_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(SR_LATCH_GPIO, SR_LATCH_PIN, GPIO_PIN_RESET);
}

static uint8_t PISO_Read8(void)
{
    uint8_t tx_dummy = 0xFFU;
    uint8_t rx = 0xFFU;
    volatile uint32_t settle;

    HAL_GPIO_WritePin(SPI2_SH_LD_GPIO_Port, SPI2_SH_LD_Pin, GPIO_PIN_RESET);
    for (settle = 0U; settle < 32U; settle++)
    {
        __NOP();
    }
    HAL_GPIO_WritePin(SPI2_SH_LD_GPIO_Port, SPI2_SH_LD_Pin, GPIO_PIN_SET);
    for (settle = 0U; settle < 32U; settle++)
    {
        __NOP();
    }

    if (HAL_SPI_TransmitReceive(&hspi2, &tx_dummy, &rx, 1U, 10U) != HAL_OK)
    {
        /* Pull-ups make 0xFF the non-active fail-safe value for display/AC inputs. */
        rx = 0xFFU;
    }

    return rx;
}

static void SR_Write16(uint8_t far_u5, uint8_t near_u2)
{
    uint8_t tx[2];

    /* Physical chain: MCU -> U2 (IND1..8) -> U5 (IND9, Q1, Q2). */
    tx[0] = far_u5;
    tx[1] = near_u2;

    if (HAL_SPI_Transmit(&hspi1, tx, 2U, 10U) == HAL_OK)
    {
        SelectorState_t latched = SELECTOR_OFF;
        SR_LatchPulse();
        if ((far_u5 & 0xC0U) == 0x40U)
            latched = SELECTOR_P1;
        else if ((far_u5 & 0xC0U) == 0x80U)
            latched = SELECTOR_P2;
        else if ((far_u5 & 0xC0U) != 0U)
            latched = SELECTOR_INVALID;

        if (latched == SELECTOR_OFF)
        {
            if ((g_relay_active != SELECTOR_OFF) || !g_relay_off_confirmed)
                g_relay_break_tick = HAL_GetTick();
            g_relay_off_confirmed = 1U;
        }
        else
        {
            g_relay_off_confirmed = 0U;
        }
        g_relay_active = latched;
    }
    else
    {
        /* Force a confirmed OFF frame and a fresh gap before any restart. */
        g_relay_active = SELECTOR_INVALID;
        g_relay_off_confirmed = 0U;
    }
}

static void ClearOutputs(void)
{
    memset(&g_out, 0, sizeof(g_out));
}

static void StopAllPumps(void)
{
    g_out.pump1_cmd = 0U;
    g_out.pump2_cmd = 0U;
}

static void EnterPumpChannelState(PumpChannel_t *channel, PumpState_t new_state)
{
    if (channel->state != new_state)
    {
        channel->state = new_state;
        channel->state_tick = HAL_GetTick();
    }
}

static void PrimeDebouncedInputs(void)
{
    uint32_t now = HAL_GetTick();

    db_pressure_p1 = g_raw.pressure_p1_raw;
    db_rpm_p1 = g_raw.rpm_p1_raw;
    db_fb_p1 = g_raw.fb_p1_raw;
    db_pressure_p2 = g_raw.pressure_p2_raw;
    db_rpm_p2 = g_raw.rpm_p2_raw;
    db_fb_p2 = g_raw.fb_p2_raw;
    db_ac_p1 = g_raw.ac_p1_raw;
    db_ac_p2 = g_raw.ac_p2_raw;
    db_sel_p1 = g_raw.sel_p1_raw;
    db_sel_p2 = g_raw.sel_p2_raw;
    db_ack_lt1 = g_raw.ack_lt1_raw;

    db_tick_pressure_p1 = now;
    db_tick_rpm_p1 = now;
    db_tick_fb_p1 = now;
    db_tick_pressure_p2 = now;
    db_tick_rpm_p2 = now;
    db_tick_fb_p2 = now;
    db_tick_ac_p1 = now;
    db_tick_ac_p2 = now;
    db_tick_sel_p1 = now;
    db_tick_sel_p2 = now;
    db_tick_ack_lt1 = now;
}

static uint8_t PressureDemandForPump(SelectorState_t pump)
{
    if (pump == SELECTOR_P1)
        return g_in.pressure_p1;
    if (pump == SELECTOR_P2)
        return g_in.pressure_p2;
    return 0U;
}

static uint8_t PumpRunRequest(uint8_t pressure_low, uint8_t rpm_active)
{
    return (uint8_t)(pressure_low || !rpm_active);
}

static void UpdateAlarmLatch(uint32_t active_mask)
{
    g_fault_new_mask = active_mask & ~g_fault_prev_active_mask;

    if (g_in.ack_short)
    {
        g_fault_latched_mask = 0U;
    }

    g_fault_active_mask = active_mask;
    g_fault_latched_mask |= g_fault_new_mask;
    g_fault_prev_active_mask = active_mask;
    g_alarm_latched = (g_fault_latched_mask != 0U) ? 1U : 0U;
}

static uint8_t P1Ready(void)
{
    return g_in.ac_p1;
}

static uint8_t P2Ready(void)
{
    return g_in.ac_p2;
}

static SelectorState_t OtherPump(SelectorState_t pump)
{
    if (pump == SELECTOR_P1)
    {
        return SELECTOR_P2;
    }

    if (pump == SELECTOR_P2)
    {
        return SELECTOR_P1;
    }

    return SELECTOR_OFF;
}

static uint8_t PumpReady(SelectorState_t pump)
{
    if (pump == SELECTOR_P1)
    {
        return P1Ready();
    }

    if (pump == SELECTOR_P2)
    {
        return P2Ready();
    }

    return 0U;
}

static uint8_t PumpFeedback(SelectorState_t pump)
{
    if (pump == SELECTOR_P1)
    {
        return g_in.fb_p1;
    }

    if (pump == SELECTOR_P2)
    {
        return g_in.fb_p2;
    }

    return 0U;
}

static uint8_t PumpDemand(SelectorState_t pump)
{
    if (pump == SELECTOR_P1)
    {
        return PumpRunRequest(PressureDemandForPump(SELECTOR_P1), g_in.rpm_p1);
    }

    if (pump == SELECTOR_P2)
    {
        return PumpRunRequest(PressureDemandForPump(SELECTOR_P2), g_in.rpm_p2);
    }

    return 0U;
}

static uint32_t FeedbackFaultMaskForPump(SelectorState_t pump)
{
    if (pump == SELECTOR_P1)
    {
        return FAULT_P1_FEEDBACK_TIMEOUT_MASK;
    }

    if (pump == SELECTOR_P2)
    {
        return FAULT_P2_FEEDBACK_TIMEOUT_MASK;
    }

    return 0U;
}

static uint8_t PumpFailedThisCycle(SelectorState_t pump)
{
    return ((g_auto_failed_mask & FeedbackFaultMaskForPump(pump)) != 0U) ? 1U : 0U;
}

static void CommandOnlyPump(SelectorState_t pump)
{
    g_out.pump1_cmd = (pump == SELECTOR_P1) ? 1U : 0U;
    g_out.pump2_cmd = (pump == SELECTOR_P2) ? 1U : 0U;
}

static void EnterAutoState(AutoState_t new_state)
{
    if (g_auto_state != new_state)
    {
        g_auto_state = new_state;
        g_auto_state_tick = HAL_GetTick();
    }
}

static void ResetAutoController(void)
{
    g_auto_state = AUTO_STATE_OFF;
    g_auto_state_tick = HAL_GetTick();
    g_auto_primary_pump = SELECTOR_OFF;
    g_auto_failed_mask = 0U;
    g_fault_latched_mask = 0U;
    g_alarm_latched = 0U;
    g_fault_active_mask = 0U;
    g_fault_new_mask = 0U;
    g_fault_prev_active_mask = 0U;
}

static void EnterAutoAlarm(void)
{
    StopAllPumps();
    g_fault_latched_mask = g_auto_failed_mask;
    g_alarm_latched = (g_fault_latched_mask != 0U) ? 1U : 0U;
    EnterAutoState(AUTO_STATE_ALARM);
}

static void SetTestIndicatorByOrder(uint8_t index)
{
    switch (index)
    {
    case 0:
        g_out.ind1_system_ready = 1U;
        break;
    case 1:
        g_out.ind2_p1_ready = 1U;
        break;
    case 2:
        g_out.ind3_p1_on = 1U;
        break;
    case 3:
        g_out.ind4_p1_standby = 1U;
        break;
    case 4:
        g_out.ind5_p2_ready = 1U;
        break;
    case 5:
        g_out.ind6_p2_on = 1U;
        break;
    case 6:
        g_out.ind7_p2_standby = 1U;
        break;
    case 7:
        g_out.ind8_pressure_low = 1U;
        break;
    case 8:
    default:
        g_out.ind9_standby_alarm = 1U;
        break;
    }
}

static void ApplyLampTestGroupToOutputs(uint8_t group)
{
    uint8_t start_index = (uint8_t)(group * 3U);
    uint8_t i;

    g_out.ind1_system_ready = 0U;
    g_out.ind2_p1_ready = 0U;
    g_out.ind3_p1_on = 0U;
    g_out.ind4_p1_standby = 0U;
    g_out.ind5_p2_ready = 0U;
    g_out.ind6_p2_on = 0U;
    g_out.ind7_p2_standby = 0U;
    g_out.ind8_pressure_low = 0U;
    g_out.ind9_standby_alarm = 0U;

    for (i = 0U; i < 3U; i++)
    {
        uint8_t led_index = (uint8_t)(start_index + i);
        if (led_index >= 9U)
        {
            break;
        }

        SetTestIndicatorByOrder(led_index);
    }
}

/* Return true once a new DIP selection has been stable for 50 ms. */
/* Select safely at boot, then accept live changes after stable debounce. */
static MAYBE_UNUSED uint8_t UpdateTestDips(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t dip = (HAL_GPIO_ReadPin(DIP3_GPIO_Port, DIP3_Pin) == GPIO_PIN_RESET) ? 4U : 0U;
    if (!g_test_initialized)
    {
        g_test_initialized = 1U;
        g_test_dip_stable = dip;
        g_test_dip_candidate = dip;
        g_test_dip_tick = now;
        return dip != 0U;
    }
    if (dip != g_test_dip_candidate)
    {
        g_test_dip_candidate = dip;
        g_test_dip_tick = now;
    }
    else if (dip != g_test_dip_stable &&
             (now - g_test_dip_tick) >= STBY_TEST_DIP_DEBOUNCE_MS)
    {
        g_test_dip_stable = dip;
        return 1U;
    }
    return 0U;
}

static void RunOutputTestProgram(void)
{
    uint32_t now = HAL_GetTick();
    if ((now - g_test_step_tick) >= STBY_LED_TEST_STEP_MS)
    {
        g_test_step_tick = now;
        g_test_led_step = (uint8_t)((g_test_led_step + 1U) % 220U);
    }
    /* 20 IND1..IND9 scans followed by 20 all-on/all-off blinks. */
    StopAllPumps();
    g_test_sys_led1 = (g_test_led_step & 1U) == 0U;
    if (g_test_led_step < 180U)
    {
        SetTestIndicatorByOrder(g_test_led_step % 9U);
        g_test_sys_led2 = (uint8_t)!g_test_sys_led1;
    }
    else
    {
        g_test_sys_led2 = g_test_sys_led1;
        if (g_test_sys_led1)
            for (uint8_t index = 0U; index < 9U; index++)
                SetTestIndicatorByOrder(index);
    }
}

/* SWD-only diagnostic: raw outputs deliberately bypass relay interlocking.
   Do not apply the 24 V coil supply while this mode is selected. */
static MAYBE_UNUSED void RunShiftRegisterPinTest(void)
{
    uint32_t now = HAL_GetTick();
    if (!g_pin_test_initialized)
    {
        g_pin_test_initialized = 1U;
        g_pin_test_byte = 0U;
        g_pin_test_tick = now;
    }
    if ((now - g_pin_test_tick) >= STBY_PIN_TEST_PHASE_MS)
    {
        g_pin_test_tick = now;
        g_pin_test_byte ^= 0xFFU;
    }
    g_test_sys_led1 = (g_pin_test_byte != 0U) ? 1U : 0U;
    g_test_sys_led2 = (uint8_t)!g_test_sys_led1;
}

static void RunControlLogic(void)
{
    ClearOutputs();

#if (CONTROL_MODE == CONTROL_MODE_PIN_TEST)
    RunShiftRegisterPinTest();
#elif (CONTROL_MODE == CONTROL_MODE_TEST)
    if (UpdateTestDips())
    {
        /* Reset mode state and require a fresh confirmed OFF interval. */
        g_relay_off_confirmed = 0U;
        ResetAutoController();
        g_test_led_step = 0U;
        g_test_step_tick = HAL_GetTick();
        g_test_sys_led1 = 0U;
        g_test_sys_led2 = 1U;
        g_lamp_test_active = 0U;
        g_lamp_test_prev_active = 0U;
        g_in.lamp_test = 0U;
        g_in.ack_short = 0U;
        g_ack_press_tick = HAL_GetTick();
        g_last_ack_raw = NormalizeLevel(db_ack_lt1, ACK_LT1_ACTIVE_LEVEL);
        return;
    }
    if (g_test_dip_stable != 0U)
        RunTestModeSection();
    else
        RunAutoModeSection();
#elif (CONTROL_MODE == CONTROL_MODE_MANUAL)
    RunManualModeSection();
#else
    RunAutoModeSection();
#endif
}

static MAYBE_UNUSED void RunAutoModeSection(void)
{
    SelectorState_t secondary_pump = OtherPump(g_auto_primary_pump);
    uint8_t demand_active = PumpDemand(g_auto_primary_pump);
    uint8_t selector_changed = 0U;

    g_fault_active_mask = 0U;
    g_fault_new_mask = 0U;
    g_fault_prev_active_mask = 0U;
    g_alarm_latched = (g_fault_latched_mask != 0U) ? 1U : 0U;

    if ((g_auto_state != AUTO_STATE_OFF) && (g_auto_state != AUTO_STATE_ALARM))
    {
        selector_changed = (g_in.selector != g_auto_primary_pump) ? 1U : 0U;
    }

    /* AC inputs indicate inverter remote permission, not motor running.
       Permission loss cancels this attempt without recording a run failure. */
    if (((g_auto_state == AUTO_STATE_TRY_PRIMARY) ||
         (g_auto_state == AUTO_STATE_PRIMARY_RUNNING)) &&
        !PumpReady(g_auto_primary_pump))
        ResetAutoController();
    else if (((g_auto_state == AUTO_STATE_TRY_SECONDARY) ||
              (g_auto_state == AUTO_STATE_SECONDARY_RUNNING)) &&
             !PumpReady(secondary_pump))
        ResetAutoController();

    switch (g_auto_state)
    {
    case AUTO_STATE_OFF:
        StopAllPumps();
        g_fault_latched_mask = 0U;
        g_alarm_latched = 0U;
        g_auto_failed_mask = 0U;
        g_auto_primary_pump = SELECTOR_OFF;

        if ((g_in.selector == SELECTOR_P1) || (g_in.selector == SELECTOR_P2))
        {
            SelectorState_t primary_pump = g_in.selector;
            SelectorState_t fallback_pump = OtherPump(primary_pump);

            if (PumpDemand(primary_pump))
            {
                if (PumpReady(primary_pump))
                {
                    g_auto_primary_pump = primary_pump;
                    CommandOnlyPump(primary_pump);
                    EnterAutoState(AUTO_STATE_TRY_PRIMARY);
                }
                else if (PumpReady(fallback_pump))
                {
                    g_auto_primary_pump = primary_pump;
                    CommandOnlyPump(fallback_pump);
                    EnterAutoState(AUTO_STATE_TRY_SECONDARY);
                }
            }
        }
        break;

    case AUTO_STATE_TRY_PRIMARY:
        if (selector_changed || !demand_active)
        {
            StopAllPumps();
            ResetAutoController();
        }
        else if (PumpFeedback(g_auto_primary_pump))
        {
            CommandOnlyPump(g_auto_primary_pump);
            EnterAutoState(AUTO_STATE_PRIMARY_RUNNING);
        }
        else if ((HAL_GetTick() - g_auto_state_tick) >= T_FEEDBACK_TIMEOUT_MS)
        {
            g_auto_failed_mask |= FeedbackFaultMaskForPump(g_auto_primary_pump);

            if (PumpReady(secondary_pump) && !PumpFailedThisCycle(secondary_pump))
            {
                CommandOnlyPump(secondary_pump);
                EnterAutoState(AUTO_STATE_TRY_SECONDARY);
            }
            else
            {
                EnterAutoAlarm();
            }
        }
        else
        {
            CommandOnlyPump(g_auto_primary_pump);
        }
        break;

    case AUTO_STATE_PRIMARY_RUNNING:
        if (selector_changed || !demand_active)
        {
            StopAllPumps();
            ResetAutoController();
        }
        else if (PumpFeedback(g_auto_primary_pump))
        {
            CommandOnlyPump(g_auto_primary_pump);
        }
        else
        {
            g_auto_failed_mask |= FeedbackFaultMaskForPump(g_auto_primary_pump);

            if (PumpReady(secondary_pump) && !PumpFailedThisCycle(secondary_pump))
            {
                CommandOnlyPump(secondary_pump);
                EnterAutoState(AUTO_STATE_TRY_SECONDARY);
            }
            else
            {
                EnterAutoAlarm();
            }
        }
        break;

    case AUTO_STATE_TRY_SECONDARY:
        if (selector_changed || !demand_active)
        {
            StopAllPumps();
            ResetAutoController();
        }
        else if (PumpFeedback(secondary_pump))
        {
            CommandOnlyPump(secondary_pump);
            EnterAutoState(AUTO_STATE_SECONDARY_RUNNING);
        }
        else if ((HAL_GetTick() - g_auto_state_tick) >= T_FEEDBACK_TIMEOUT_MS)
        {
            g_auto_failed_mask |= FeedbackFaultMaskForPump(secondary_pump);
            EnterAutoAlarm();
        }
        else
        {
            CommandOnlyPump(secondary_pump);
        }
        break;

    case AUTO_STATE_SECONDARY_RUNNING:
        if (selector_changed || !demand_active)
        {
            StopAllPumps();
            ResetAutoController();
        }
        else if (PumpFeedback(secondary_pump))
        {
            CommandOnlyPump(secondary_pump);
        }
        else
        {
            g_auto_failed_mask |= FeedbackFaultMaskForPump(secondary_pump);

            if (PumpReady(g_auto_primary_pump) && !PumpFailedThisCycle(g_auto_primary_pump))
            {
                CommandOnlyPump(g_auto_primary_pump);
                EnterAutoState(AUTO_STATE_TRY_PRIMARY);
            }
            else
            {
                EnterAutoAlarm();
            }
        }
        break;

    case AUTO_STATE_ALARM:
    default:
        StopAllPumps();
        if (g_in.ack_short)
        {
            ResetAutoController();
        }
        break;
    }

    g_out.ind1_system_ready = 1U;
    g_out.ind2_p1_ready = P1Ready();
    g_out.ind3_p1_on = g_in.fb_p1;
    g_out.ind4_p1_standby = (g_in.selector == SELECTOR_P1) ? 1U : 0U;
    g_out.ind5_p2_ready = P2Ready();
    g_out.ind6_p2_on = g_in.fb_p2;
    g_out.ind7_p2_standby = (g_in.selector == SELECTOR_P2) ? 1U : 0U;
    g_out.ind8_pressure_low = (uint8_t)(g_in.pressure_p1 || g_in.pressure_p2);
    g_out.ind9_standby_alarm =
        (((g_fault_latched_mask & (FAULT_P1_FEEDBACK_TIMEOUT_MASK | FAULT_P2_FEEDBACK_TIMEOUT_MASK)) != 0U) &&
         g_alarm_blink)
            ? 1U
            : 0U;

    ApplyLampTestIfNeeded();
}

static MAYBE_UNUSED void RunManualModeSection(void)
{
    uint8_t pump1_selected = (g_in.selector == SELECTOR_P1) ? 1U : 0U;
    uint8_t pump2_selected = (g_in.selector == SELECTOR_P2) ? 1U : 0U;

    g_fault_active_mask = 0U;
    g_fault_new_mask = 0U;
    g_fault_prev_active_mask = 0U;
    g_fault_latched_mask = 0U;
    g_alarm_latched = 0U;

    if (pump1_selected)
    {
        g_out.pump1_cmd = 1U;
        EnterPumpChannelState(&g_p1_channel, PUMP_STATE_RUNNING);
    }
    else
    {
        g_out.pump1_cmd = 0U;
        EnterPumpChannelState(&g_p1_channel, PUMP_STATE_OFF);
    }

    if (pump2_selected)
    {
        g_out.pump2_cmd = 1U;
        EnterPumpChannelState(&g_p2_channel, PUMP_STATE_RUNNING);
    }
    else
    {
        g_out.pump2_cmd = 0U;
        EnterPumpChannelState(&g_p2_channel, PUMP_STATE_OFF);
    }

    g_out.ind1_system_ready = 1U;
    g_out.ind2_p1_ready = g_in.ac_p1;
    g_out.ind3_p1_on = g_in.fb_p1;
    g_out.ind4_p1_standby = pump1_selected;
    g_out.ind5_p2_ready = g_in.ac_p2;
    g_out.ind6_p2_on = g_in.fb_p2;
    g_out.ind7_p2_standby = pump2_selected;
    g_out.ind8_pressure_low = (uint8_t)(g_in.pressure_p1 || g_in.pressure_p2);
    g_out.ind9_standby_alarm = 0U;

    ApplyLampTestIfNeeded();
}

static MAYBE_UNUSED void RunTestModeSection(void)
{
    UpdateAlarmLatch(0U);
    RunOutputTestProgram();
}

static void ApplyLampTestIfNeeded(void)
{
    if (g_in.lamp_test)
    {
        uint32_t now = HAL_GetTick();

        if (g_lamp_test_prev_active == 0U)
        {
            g_lamp_test_group_step = 0U;
            g_lamp_test_tick = now;
        }
        else if ((now - g_lamp_test_tick) >= T_OUTPUT_TEST_STEP_MS)
        {
            g_lamp_test_tick = now;
            g_lamp_test_group_step = (uint8_t)((g_lamp_test_group_step + 1U) % 3U);
        }

        ApplyLampTestGroupToOutputs(g_lamp_test_group_step);
        g_lamp_test_prev_active = 1U;
    }
    else
    {
        g_lamp_test_prev_active = 0U;
    }
}

static void ApplyRelayInterlock(void)
{
    SelectorState_t requested = SELECTOR_OFF;
    if ((g_out.pump1_cmd != 0U) && (g_out.pump2_cmd == 0U))
        requested = SELECTOR_P1;
    else if ((g_out.pump2_cmd != 0U) && (g_out.pump1_cmd == 0U))
        requested = SELECTOR_P2;

    StopAllPumps();
    /* Final permission gate applies to both AUTO and MANUAL commands. */
    if ((requested == SELECTOR_OFF) || !PumpReady(requested))
        return;

    if ((g_relay_active == requested) ||
        ((g_relay_active == SELECTOR_OFF) && g_relay_off_confirmed &&
         ((HAL_GetTick() - g_relay_break_tick) >= T_RELAY_BREAK_BEFORE_MAKE_MS)))
    {
        CommandOnlyPump(requested);
    }
    /* Otherwise transmit OFF; SR_Write16 starts the gap after the latch. */
}

static void UpdateOutputs(void)
{
#if (CONTROL_MODE == CONTROL_MODE_PIN_TEST)
    SR_Write16(g_pin_test_byte, g_pin_test_byte);
    return;
#endif
    uint8_t near_u2 = 0U; /* QA..QH drive IND8..IND1. */
    uint8_t far_u5 = 0U;  /* QA=IND9, QG=Q1, QH=Q2. */

    ApplyRelayInterlock();

    if (g_out.pump1_cmd)
        far_u5 |= (1U << 6); /* U5 QG -> ULN O7 -> Q1 */
    if (g_out.pump2_cmd)
        far_u5 |= (1U << 7); /* U5 QH -> ULN O8 -> Q2 */

    if (g_out.ind1_system_ready)
        near_u2 |= (1U << 7);
    if (g_out.ind2_p1_ready)
        near_u2 |= (1U << 6);
    if (g_out.ind3_p1_on)
        near_u2 |= (1U << 5);
    if (g_out.ind4_p1_standby)
        near_u2 |= (1U << 4);
    if (g_out.ind5_p2_ready)
        near_u2 |= (1U << 3);
    if (g_out.ind6_p2_on)
        near_u2 |= (1U << 2);
    if (g_out.ind7_p2_standby)
        near_u2 |= (1U << 1);
    if (g_out.ind8_pressure_low)
        near_u2 |= (1U << 0);
    if (g_out.ind9_standby_alarm)
        far_u5 |= (1U << 0);

    SR_Write16(far_u5, near_u2);
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_SPI2_Init();
  /* USER CODE BEGIN 2 */

    SR_Write16(0U, 0U);
    ReadRawInputs();
    PrimeDebouncedInputs();
    ProcessInputs();

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    while (1)
    {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
        ReadRawInputs();
        ProcessInputs();
        RunControlLogic();
        UpdateOutputs();
        UpdateSysLeds();
        HAL_Delay(LOOP_DELAY_MS);
    }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 336;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief SPI2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI2_Init(void)
{

  /* USER CODE BEGIN SPI2_Init 0 */

  /* USER CODE END SPI2_Init 0 */

  /* USER CODE BEGIN SPI2_Init 1 */

  /* USER CODE END SPI2_Init 1 */
  hspi2.Instance = SPI2;
  hspi2.Init.Mode = SPI_MODE_MASTER;
  hspi2.Init.Direction = SPI_DIRECTION_2LINES;
  hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi2.Init.NSS = SPI_NSS_SOFT;
  hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;
  hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi2.Init.CRCPolynomial = 10;
  if (HAL_SPI_Init(&hspi2) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI2_Init 2 */

  /* USER CODE END SPI2_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, SYS_LED1_Pin|SYS_LED2_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SR_LATCH_GPIO_Port, SR_LATCH_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(SPI2_SH_LD_GPIO_Port, SPI2_SH_LD_Pin, GPIO_PIN_SET);

  /*Configure GPIO pins : SYS_LED1_Pin SYS_LED2_Pin */
  GPIO_InitStruct.Pin = SYS_LED1_Pin|SYS_LED2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : SR_LATCH_Pin */
  GPIO_InitStruct.Pin = SR_LATCH_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : SPI2_SH_LD_Pin */
  GPIO_InitStruct.Pin = SPI2_SH_LD_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(SPI2_SH_LD_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : I1_Pin I2_Pin */
  GPIO_InitStruct.Pin = I1_Pin|I2_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : I3_Pin */
  GPIO_InitStruct.Pin = I3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(I3_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : DIP1_Pin DIP2_Pin DIP3_Pin I4_Pin I5_Pin I6_Pin */
  GPIO_InitStruct.Pin = DIP1_Pin|DIP2_Pin|DIP3_Pin|I4_Pin|I5_Pin|I6_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
    /* User can add his own implementation to report the HAL error return state */
    __disable_irq();
    while (1)
    {
    }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
    /* User can add his own implementation to report the file name and line number,
       ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
