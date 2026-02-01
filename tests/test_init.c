/**
 * @file test_init.c
 * @brief Unit tests for IO-Link stack initialization
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/iolink.h"
#include "test_helpers.h"

/* --- Tests --- */

static void test_iolink_init_success(void **state)
{
    (void)state;
    
    /* Expectations */
    will_return(g_phy_mock.init, 0);

    int result = iolink_init(&g_phy_mock);
    assert_int_equal(result, 0);
}

static void test_iolink_init_fail_null(void **state)
{
    (void)state;
    
    int result = iolink_init(NULL);
    assert_int_not_equal(result, 0);
}

static void test_iolink_init_fail_driver(void **state)
{
    (void)state;
    
    /* Expectations */
    will_return(g_phy_mock.init, -1);

    int result = iolink_init(&g_phy_mock);
    assert_int_equal(result, -1);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_iolink_init_success),
        cmocka_unit_test(test_iolink_init_fail_null),
        cmocka_unit_test(test_iolink_init_fail_driver),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
