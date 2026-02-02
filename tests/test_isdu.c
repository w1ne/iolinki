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
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);

    /* 1. Send READ Request for Index 0x10 (Vendor Name) */
    /* V1.1.5 format: [Control(Start=1, Last=1, Seq=0)] [Read Service (0x90)] [Index High (0x00)] [Index Low (0x10)] [Subindex (0x00)] */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xC0), 0); /* Control: Start=1, Last=1 */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x90), 0); /* Service/Length */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0); /* Index High */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x10), 0); /* Index Low */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 1); /* Subindex -> Complete */

    iolink_isdu_process(&ctx);

    /* 2. Collect Response */
    uint8_t byte;
    char name[32] = {0};
    int i = 0;
    
    /* Prepend: Control Byte for single-frame response */
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0xC0); /* Start=1, Last=1, Seq=0 */

    while (iolink_isdu_get_response_byte(&ctx, &byte) > 0) {
        name[i++] = (char)byte;
    }
    
    /* Device Info mock has "iolinki-project" or similar */
    assert_true(i > 0);
}

static void test_isdu_write_request(void **state)
{
    (void)state;
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    
    /* V1.1.5 Write: [Control 0xC0] [Write 1 byte 0xA1] [Index 0x0040] [Subindex 0x00] [Data 0x55] */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xC0), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xA1), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x40), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x55), 1);
    
    iolink_isdu_process(&ctx);
    
    uint8_t byte;
    /* Response Prep: Control (Start=1, Last=1) */
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0xC0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_isdu_vendor_name_read),
        cmocka_unit_test(test_isdu_write_request),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
