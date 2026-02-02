/**
 * @file test_crc.c
 * @brief Unit tests for IO-Link CRC and checksums
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>

#include "iolinki/crc.h"

static void test_crc6_basic(void **state)
{
    (void)state;
    /* Known test vectors for IO-Link CRC6 (Polynomial 0x1D, Init 0x15) */
    uint8_t data1[] = {0x00};
    /* Calculated according to spec or reference implementation */
    assert_int_equal(iolink_crc6(data1, 1), 0x11);
    
    uint8_t data2[] = {0xA5};
    assert_int_equal(iolink_crc6(data2, 1), 0x12);
}

static void test_checksum_ck_basic(void **state)
{
    (void)state;
    /* CK = MC XOR CKT XOR 0x5a */
    /* MC=0, CKT=0 -> 0 ^ 0 ^ 0x5a = 0x5a */
    /* Wait, checking our implementation: MC ^ CKT ^ 0x5a is common. 
       Let's verify the actual code behavior. */
    assert_int_equal(iolink_checksum_ck(0x00, 0x00), 0x5A);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_crc6_basic),
        cmocka_unit_test(test_checksum_ck_basic),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
