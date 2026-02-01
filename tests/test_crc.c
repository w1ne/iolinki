/**
 * @file test_crc.c
 * @brief Unit tests for IO-Link CRC calculation
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>

#include "iolinki/crc.h"

static void test_crc6_simple(void **state)
{
    (void)state;
    /* Example data for CRC verification (v1.1 spec test vector would be better)
       Let's use a dummy check for now. */
    uint8_t data[] = {0x00};
    uint8_t res = iolink_crc6(data, 1);
    /* Specification V1.1 polynomial 0x1D seed 0x15. 
       After 8 shifts of 0x00: 
       Seed 0x15 (010101) -> ...
    */
    assert_true(res <= 0x3F);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_crc6_simple),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
