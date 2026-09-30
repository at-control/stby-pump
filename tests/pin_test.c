/* Reuse simulated hardware from the normal controller tests. */
#define run_tests normal_tests_unused
#include "bench_test.c"
#undef run_tests

int run_tests(void)
{
    reset(7);
    g_pin_test_initialized = 0;
    /* None of these inputs may prevent or accelerate pin measurements. */
    g_in.ac_p1 = 1; g_in.ac_p2 = 1; db_ack_lt1 = 0;
    tick(0);
    CHECK(latched[0] == 0 && latched[1] == 0);
    CHECK(sys_led1 == LED_OFF_STATE && sys_led2 == LED_ON_STATE);
    for (uint32_t phase = 1; phase <= 8; phase++)
    {
        tick(phase * 3000 - 1);
        CHECK(latched[0] == ((phase & 1) ? 0 : 0xFF));
        CHECK(latched[1] == latched[0]);
        tick(phase * 3000);
        CHECK(latched[0] == ((phase & 1) ? 0xFF : 0));
        CHECK(latched[1] == latched[0]);
        CHECK(sys_led1 == ((phase & 1) ? LED_ON_STATE : LED_OFF_STATE));
        CHECK(sys_led2 != sys_led1);
    }
    g_pin_test_initialized = 0;
    tick(0xFFFFFC00U);
    tick(0x000007B7U); CHECK(latched[0] == 0 && latched[1] == 0);
    tick(0x000007B8U); CHECK(latched[0] == 0xFF && latched[1] == 0xFF);
    return 0;
}
