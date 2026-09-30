/* Execute the real controller logic with simulated tick, GPIO and SPI I/O. */
#define main firmware_main
#include "../Core/Src/main.c"
#undef main

static uint32_t test_tick;
static uint8_t switches;
static uint8_t spi_fail;
static uint8_t shifted[2], latched[2];
static GPIO_PinState sys_led1, sys_led2;

uint32_t HAL_GetTick(void) { return test_tick; }
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    if (port == GPIOB && (pin & (DIP1_Pin | DIP2_Pin | DIP3_Pin)))
        return (switches & pin) ? GPIO_PIN_RESET : GPIO_PIN_SET;
    return GPIO_PIN_SET;
}
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    if (port == SYS_LED1_GPIO_Port && pin == SYS_LED1_Pin) sys_led1 = state;
    if (port == SYS_LED2_GPIO_Port && pin == SYS_LED2_Pin) sys_led2 = state;
    if (port == SR_LATCH_GPIO_Port && pin == SR_LATCH_Pin && state == GPIO_PIN_SET)
    {
        latched[0] = shifted[0];
        latched[1] = shifted[1];
    }
}
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *spi, const uint8_t *data,
                                 uint16_t size, uint32_t timeout)
{
    (void)spi; (void)timeout;
    if (spi_fail || size != 2U) return HAL_ERROR;
    shifted[0] = data[0]; shifted[1] = data[1];
    return HAL_OK;
}
void *memset(void *dst, int value, size_t size)
{
    unsigned char *p = dst;
    while (size--) *p++ = (unsigned char)value;
    return dst;
}

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

static void reset(uint8_t dip)
{
    test_tick = 0U; switches = dip; spi_fail = 0U;
    memset(&g_in, 0, sizeof(g_in));
    /* Known process status for checking the default display. */
    g_in.rpm_p1 = 1U; g_in.pressure_p2 = 1U;
    db_ack_lt1 = 1U;
    g_test_initialized = 0U;
    g_test_led_step = 0U;
    g_test_dip_stable = 0U;
    g_lamp_test_prev_active = 0U;
    ResetAutoController();
    g_relay_active = SELECTOR_INVALID;
    g_relay_off_confirmed = 0U;
    SR_Write16(0U, 0U);
}
static void tick(uint32_t now)
{
    test_tick = now;
    RunControlLogic();
    UpdateOutputs();
    UpdateSysLeds();
}

int run_tests(void)
{
    /* Open DIPs: no automatic test pulses, even when AC is available. */
    reset(0); g_in.ac_p1 = 1; g_in.ac_p2 = 1;
    for (uint32_t now = 0; now < 5000; now += 20)
    {
        tick(now);
        CHECK((latched[0] & 0xC0) == 0);
        CHECK(latched[1] == 0xC9);
    }
    /* Production demand, feedback loss, secondary transfer and alarm latch. */
    reset(0); g_in.selector = SELECTOR_P1;
    g_in.pressure_p1 = 1; g_in.ac_p1 = 1; g_in.ac_p2 = 1;
    tick(0); tick(100); CHECK((latched[0] & 0xC0) == 0x40);
    tick(1200); CHECK((latched[0] & 0xC0) == 0x40);
    tick(3000); CHECK((latched[0] & 0xC0) == 0);
    tick(3099); CHECK((latched[0] & 0xC0) == 0);
    tick(3100); CHECK((latched[0] & 0xC0) == 0x80);
    tick(6000); CHECK(g_auto_state == AUTO_STATE_ALARM);
    CHECK((latched[0] & 0xC0) == 0);
    g_in.ack_short = 1; tick(6020);
    CHECK(g_auto_state == AUTO_STATE_OFF);
    g_in.ack_short = 0; g_in.selector = SELECTOR_OFF;
    tick(6040); CHECK((latched[0] & 0xC0) == 0);

    /* DIP1/2 remain production; DIP3 inhibits relays even at boot. */
    for (uint8_t dip = 0; dip < 8; dip++)
    {
        reset(dip); g_in.selector = SELECTOR_P1;
        g_in.pressure_p1 = 1; g_in.ac_p1 = 1;
        tick(0); tick(49); tick(50); tick(100);
        CHECK((latched[0] & 0xC0) == ((dip & 4) ? 0 : 0x40));
    }
    /* Two whole patterns, with exact order/count and no relay outputs. */
    reset(4); tick(0);
    for (uint32_t slot = 0; slot < 440; slot++)
    {
        g_in.ac_p1 = 1; g_in.ac_p2 = 1;
        g_in.pressure_p1 = 1; g_in.selector = SELECTOR_P1;
        g_in.lamp_test = 1; g_in.ack_short = 1;
        tick(1 + slot * 60);
        uint32_t phase = slot % 220;
        uint8_t near = phase < 180 ? (phase % 9 < 8 ? 0x80 >> (phase % 9) : 0) : ((phase & 1) ? 0 : 0xFF);
        uint8_t far = phase < 180 ? (phase % 9 == 8) : !(phase & 1);
        CHECK(latched[1] == near && latched[0] == far);
        tick(59 + slot * 60);
        CHECK(latched[1] == near && latched[0] == far);
    }
    /* Bounce does not change mode; accepted changes send OFF and reset state. */
    reset(0); g_in.selector = SELECTOR_P1;
    g_in.pressure_p1 = 1; g_in.ac_p1 = 1; g_in.fb_p1 = 1;
    tick(0); tick(100);
    switches = 4; tick(120); tick(149);
    switches = 0; tick(150); tick(210);
    CHECK(g_test_dip_stable == 0 && (latched[0] & 0xC0) == 0x40);
    switches = 4; tick(220); tick(269);
    CHECK(g_test_dip_stable == 0);
    tick(270); CHECK(g_test_dip_stable == 4 && latched[0] == 0 && latched[1] == 0);
    tick(290); CHECK(latched[1] == 0x80 && latched[0] == 0);
    switches = 0; tick(300);
    switches = 4; tick(320); tick(380);
    CHECK(g_test_dip_stable == 4 && (latched[0] & 0xC0) == 0);
    switches = 0; tick(400); tick(449);
    CHECK(g_test_dip_stable == 4);
    tick(450); CHECK(g_test_dip_stable == 0 && latched[0] == 0);
    tick(470); tick(549); CHECK((latched[0] & 0xC0) == 0);
    tick(550); CHECK((latched[0] & 0xC0) == 0x40);
    /* DIP1/2 never alter production mode. */
    switches = 3; tick(570); tick(630);
    CHECK(g_test_dip_stable == 0 && (latched[0] & 0xC0) == 0x40);
    /* Leaving test with no inverter permission cannot energize either relay. */
    reset(4); tick(0); g_in.selector = SELECTOR_P1; g_in.pressure_p1 = 1;
    switches = 0; tick(20); tick(70); tick(200);
    CHECK(g_test_dip_stable == 0 && (latched[0] & 0xC0) == 0);

    /* Failed OFF writes do not count toward the relay gap. */
    reset(0); g_in.selector = SELECTOR_P1; g_in.pressure_p1 = 1; g_in.ac_p1 = 1; tick(0); tick(150);
    g_in.ac_p2 = 1;
    spi_fail = 1; test_tick = 200;
    CommandOnlyPump(SELECTOR_P2); UpdateOutputs();
    CHECK((latched[0] & 0xC0) == 0x40);
    spi_fail = 0; test_tick = 600;
    CommandOnlyPump(SELECTOR_P2); UpdateOutputs();
    CHECK((latched[0] & 0xC0) == 0);
    test_tick = 699; CommandOnlyPump(SELECTOR_P2); UpdateOutputs();
    CHECK((latched[0] & 0xC0) == 0);
    test_tick = 700; CommandOnlyPump(SELECTOR_P2); UpdateOutputs();
    CHECK((latched[0] & 0xC0) == 0x80);
    g_out.pump1_cmd = 1; g_out.pump2_cmd = 1; UpdateOutputs();
    CHECK((latched[0] & 0xC0) == 0);

    /* Inverter permission is required continuously, independent of run feedback. */
    reset(0); g_in.selector = SELECTOR_P1; g_in.pressure_p1 = 1;
    g_in.fb_p1 = 1; g_in.fb_p2 = 1;
    tick(0); tick(100); CHECK((latched[0] & 0xC0) == 0);
    g_in.ac_p1 = 1; tick(120); tick(140);
    CHECK((latched[0] & 0xC0) == 0x40);
    g_in.ac_p1 = 0; tick(160);
    CHECK((latched[0] & 0xC0) == 0 && g_auto_state == AUTO_STATE_OFF);
    tick(4000); CHECK(g_auto_state == AUTO_STATE_OFF && g_fault_latched_mask == 0);
    g_in.ac_p2 = 1; tick(4020); tick(4040);
    CHECK((latched[0] & 0xC0) == 0x80);
    g_in.ac_p2 = 0; tick(4060);
    CHECK((latched[0] & 0xC0) == 0 && g_auto_state == AUTO_STATE_OFF);
    /* Direct requests cannot bypass the permission gate either. */
    CommandOnlyPump(SELECTOR_P1); UpdateOutputs();
    CHECK((latched[0] & 0xC0) == 0);
    CommandOnlyPump(SELECTOR_P2); UpdateOutputs();
    CHECK((latched[0] & 0xC0) == 0);
    /* Loss while waiting for run feedback also cancels the timeout. */
    g_in.fb_p1 = 0; g_in.fb_p2 = 0; g_in.ac_p1 = 1;
    tick(4200); CHECK(g_auto_state == AUTO_STATE_TRY_PRIMARY);
    g_in.ac_p1 = 0; g_in.ac_p2 = 1; tick(4220);
    CHECK(g_auto_state == AUTO_STATE_TRY_SECONDARY && (latched[0] & 0xC0) == 0);
    tick(4320); CHECK((latched[0] & 0xC0) == 0x80);
    g_in.ac_p2 = 0; tick(4340);
    CHECK(g_auto_state == AUTO_STATE_OFF && (latched[0] & 0xC0) == 0);

    /* Raw selector transfer: 200 ms debounce then 100 ms confirmed OFF. */
    reset(0); memset(&g_raw, 0, sizeof(g_raw));
    g_raw.sel_p2_raw = 1; g_raw.ack_lt1_raw = 1;
    PrimeDebouncedInputs();
    for (uint32_t now = 0; now <= 400; now += 20)
    {
        test_tick = now; ProcessInputs(); tick(now);
    }
    CHECK((latched[0] & 0xC0) == 0x40);
    g_raw.sel_p1_raw = 1; g_raw.sel_p2_raw = 0;
    for (uint32_t now = 420; now <= 720; now += 20)
    {
        test_tick = now; ProcessInputs(); tick(now);
        CHECK((latched[0] & 0xC0) == (now < 600 ? 0x40 : now < 700 ? 0 : 0x80));
    }

    /* LED scheduler and live DIP debounce survive tick rollover. */
    reset(4); tick(0xFFFFFFF0U);
    switches = 0; tick(0x2BU); CHECK(latched[1] == 0x80);
    tick(0x2CU); CHECK(latched[1] == 0x40 && latched[0] == 0);
    CHECK(g_test_dip_stable == 4);
    tick(0x5DU); CHECK(g_test_dip_stable == 0 && latched[0] == 0);
    return 0;
}
