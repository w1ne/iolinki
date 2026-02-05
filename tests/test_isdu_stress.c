/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_isdu_stress.c
 * @brief Additional stress tests for ISDU Flow Control
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/isdu.h"

static void test_rapid_concurrent_requests(void **state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;

    /* Test 5 rapid concurrent requests */
    for (int i = 0; i < 5; i++) {
        iolink_isdu_init(&ctx);

        /* Start first request */
        iolink_isdu_collect_byte(&ctx, 0x81);
        iolink_isdu_collect_byte(&ctx, 0xA2);

        /* Immediately start second request */
        iolink_isdu_collect_byte(&ctx, 0x82);

        iolink_isdu_process(&ctx);

        /* Verify Busy response each time */
        uint8_t byte;
        assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
        assert_int_equal(byte & 0x80, 0x80);

        /* Skip to error code */
        iolink_isdu_get_response_byte(&ctx, &byte);
        iolink_isdu_get_response_byte(&ctx, &byte);
        iolink_isdu_get_response_byte(&ctx, &byte);
        assert_int_equal(byte, 0x30); /* Busy */
    }
}

static void test_maximum_segmented_transfer(void **state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);

    /* Simulate a large segmented write (16 bytes = max typical ISDU payload) */
    /* Start segment */
    iolink_isdu_collect_byte(&ctx, 0x81); /* Start, Seq=1, !Last */
    iolink_isdu_collect_byte(&ctx, 0xAF); /* Write, Len=15 (extended length follows) */

    /* Extended length: 16 bytes total */
    iolink_isdu_collect_byte(&ctx, 0x02); /* Seq=2, !Last */
    iolink_isdu_collect_byte(&ctx, 0x10); /* Length = 16 */

    /* Index */
    iolink_isdu_collect_byte(&ctx, 0x03); /* Seq=3, !Last */
    iolink_isdu_collect_byte(&ctx, 0x18); /* Index MSB */

    iolink_isdu_collect_byte(&ctx, 0x04); /* Seq=4, !Last */
    iolink_isdu_collect_byte(&ctx, 0x00); /* Index LSB */

    /* Subindex */
    iolink_isdu_collect_byte(&ctx, 0x05); /* Seq=5, !Last */
    iolink_isdu_collect_byte(&ctx, 0x00); /* Subindex */

    /* Data bytes (16 bytes) */
    for (int i = 0; i < 16; i++) {
        uint8_t seq = (uint8_t) (6 + i);
        uint8_t ctrl = (i == 15) ? (uint8_t) (0x40 | seq) : seq; /* Last bit on final byte */
        iolink_isdu_collect_byte(&ctx, ctrl);
        iolink_isdu_collect_byte(&ctx, (uint8_t) (0xAA + i));
    }

    iolink_isdu_process(&ctx);

    /* Verify successful response (Start | Last) */
    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte & 0xC0, 0xC0); /* Start | Last */
}

static void test_sequence_number_wraparound(void **state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);

    /* Test sequence number wraparound (0-63) */
    /* Start with seq 62 */
    iolink_isdu_collect_byte(&ctx, 0x80 | 62); /* Start, Seq=62, !Last */
    iolink_isdu_collect_byte(&ctx, 0xA2);      /* Write, Len=2 */

    iolink_isdu_collect_byte(&ctx, 63); /* Seq=63, !Last */
    iolink_isdu_collect_byte(&ctx, 0x18);

    iolink_isdu_collect_byte(&ctx, 0x40 | 0); /* Last, Seq=0 (wraparound) */
    iolink_isdu_collect_byte(&ctx, 0x00);

    iolink_isdu_process(&ctx);

    /* Verify successful response */
    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte & 0xC0, 0xC0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_rapid_concurrent_requests),
        cmocka_unit_test(test_maximum_segmented_transfer),
        cmocka_unit_test(test_sequence_number_wraparound),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
