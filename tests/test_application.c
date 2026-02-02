/**
 * @file test_application.c
 * @brief Unit tests for Application Layer PD API
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/iolink.h"
#include "iolinki/application.h"
#include "test_helpers.h"

static void test_pd_input_update_flow(void **state)
{
    (void)state;
    iolink_init(&g_phy_mock);
    
    uint8_t input_data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    assert_int_equal(iolink_pd_input_update(input_data, sizeof(input_data), true), 0);
    
    /* We can't directly inspect g_dll_ctx as it's static in iolink_core.c, 
       but we verify the return code and length check. */
    assert_int_equal(iolink_pd_input_update(input_data, 100, true), -1); /* Too large */
}

static void test_pd_output_read_flow(void **state)
{
    (void)state;
    iolink_init(&g_phy_mock);
    
    uint8_t out_buf[4];
    /* Initially empty */
    assert_int_equal(iolink_pd_output_read(out_buf, sizeof(out_buf)), 0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pd_input_update_flow),
        cmocka_unit_test(test_pd_output_read_flow),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
