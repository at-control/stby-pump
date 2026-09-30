#define run_tests normal_tests_unused
#include "bench_test.c"
#undef run_tests

int run_tests(void)
{
    reset(0);
    memset(&g_in, 0, sizeof(g_in));
    tick(0); CHECK(latched[1] == 0x80 && (latched[0] & 1) == 0);
    g_in.ac_p1 = 1;
    tick(100); CHECK(latched[1] == 0xC0);
    g_in.ac_p1 = 0; g_in.ac_p2 = 1;
    tick(200); CHECK(latched[1] == 0x88);
    g_in.ac_p1 = 1;
    tick(400); CHECK(latched[1] == 0xC8);
    tick(800); CHECK(latched[1] == 0xC8);
    g_in.fb_p1 = 1; tick(820); CHECK(latched[1] == 0xE8);
    g_in.fb_p1 = 0; g_in.fb_p2 = 1;
    tick(840); CHECK(latched[1] == 0xCC);
    g_in.selector = SELECTOR_P1; tick(860); CHECK(latched[1] == 0xDC);
    g_in.selector = SELECTOR_P2; tick(880); CHECK(latched[1] == 0xCE);
    g_in.pressure_p1 = 1; tick(900); CHECK(latched[1] == 0xCF);
    g_in.pressure_p1 = 0; g_in.pressure_p2 = 1;
    tick(920); CHECK(latched[1] == 0xCF);

    /* Live test replaces status; returning to AUTO restores live indicators. */
    switches = 4; tick(1000); tick(1060); tick(1080);
    CHECK(latched[1] == 0x80 && latched[0] == 0);
    switches = 0; tick(1100); tick(1160); tick(1180);
    CHECK(latched[1] == 0xCF && (latched[0] & 1) == 0);
    memset(&g_in, 0, sizeof(g_in));
    tick(1200); CHECK(latched[1] == 0x80);
    return 0;
}
