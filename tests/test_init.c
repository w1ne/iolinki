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

static void test_iolink_init_basic(void **state)
{
    (void)state;
    
    int result = iolink_init();
    assert_int_equal(result, 0);
}

static void test_placeholder_pass(void **state)
{
    (void)state;
    /* Placeholder test to ensure framework works */
    assert_true(1);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_iolink_init_basic),
        cmocka_unit_test(test_placeholder_pass),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
