/**
 * @file test_isdu.c
 * @brief Unit tests for ISDU acyclic messaging
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/isdu.h"

static void test_isdu_vendor_name_read(void **state)
{
    (void)state;
    iolink_isdu_init();

    /* 1. Send READ Request for Index 0x10 (Vendor Name) */
    /* Header: Read (0x00), Index (0x10), Subindex (0x00) */
    assert_int_equal(iolink_isdu_collect_byte(0x00), 0);
    assert_int_equal(iolink_isdu_collect_byte(0x10), 0);
    assert_int_equal(iolink_isdu_collect_byte(0x00), 1); /* Complete */

    /* 2. Collect Response */
    uint8_t byte;
    char name[32] = {0};
    int i = 0;
    while (iolink_isdu_get_response_byte(&byte) > 0) {
        name[i++] = (char)byte;
    }
    
    assert_string_equal(name, "iolinki-project");
}

static void test_isdu_write_request(void **state)
{
    (void)state;
    iolink_isdu_init();
    
    /* Master Sends: Write (0x80), Index (0x40), Subindex (0x00) */
    assert_int_equal(iolink_isdu_collect_byte(0x80), 0);
    assert_int_equal(iolink_isdu_collect_byte(0x40), 0);
    assert_int_equal(iolink_isdu_collect_byte(0x00), 1);
    
    /* Device Responds: Expected behavior is accepting the write */
    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&byte), 0); /* No data payload for write ack */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_isdu_vendor_name_read),
        cmocka_unit_test(test_isdu_write_request),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
