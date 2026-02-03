/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_isdu_segmented.c
 * @brief Unit tests for IO-Link V1.1.5 Segmented ISDU transfers
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/isdu.h"

static void test_isdu_segmented_write_2_frames(void **state)
{
    (void)state;
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);

    /* Frame 0: [Ctrl(S=1, L=0, Seq=0)] [Service/Len] */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x80), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xA1), 0); /* 1 byte data Write */

    /* Frame 1: Index High */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x01), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x01), 0); /* IndexH = 0x01 */

    /* Frame 2: Index Low */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x02), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x23), 0); /* IndexL = 0x23 -> Index 0x0123 */

    /* Frame 3: Subindex */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x03), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);

    /* Frame 4: Data (Last) */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x44), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xAA), 1); /* Complete */

    assert_int_equal(ctx.header.index, 0x0123);
    assert_int_equal(ctx.buffer[0], 0xAA);
}

static void test_isdu_segmented_read_multiplexed(void **state)
{
    (void)state;
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);

    /* Single frame segmented READ (V1.1 multiplexing simulation) */
    /* Request: [Ctrl 0x80] [Read 0x90] [0x00] [0x10] [0x00] */
    /* Wait... for READ, Start=1, Last=1 is used for single frame request. */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xC0), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x90), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x10), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 1);

    iolink_isdu_process(&ctx);
    
    /* Response: Assume we use 'is_segmented' to trigger multiplexing? 
       Actually, our implementation triggers it if is_segmented is true. 
       Let's manually set it for testing the multiplexed provider. */
    ctx.is_segmented = true;
    ctx.is_response_control_sent = false;

    uint8_t byte;
    /* Frame Segment 0: Control (Start=1, Last=0, Seq=0) */
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    
    /* Frame Segment 0: Data 0 */
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    
    /* Frame Segment 1: Control (Start=0, Last=?, Seq=1) */
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte & 0x3F, 1);
    assert_false(byte & 0x80);
}

static void test_isdu_out_of_order_sequence(void **state)
{
    (void)state;
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);

    /* Frame 1: S=1, L=0, Seq=0 */
    iolink_isdu_collect_byte(&ctx, 0x80);
    iolink_isdu_collect_byte(&ctx, 0xA2);
    iolink_isdu_collect_byte(&ctx, 0x00);
    iolink_isdu_collect_byte(&ctx, 0x50);
    iolink_isdu_collect_byte(&ctx, 0x00);
    iolink_isdu_collect_byte(&ctx, 0xAA);

    /* Frame 2: WRONG SEQ (Expected 1, send 2) */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x42), -1);
    assert_int_equal(ctx.error_code, 0x81); /* Seq error */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_isdu_segmented_write_2_frames),
        cmocka_unit_test(test_isdu_segmented_read_multiplexed),
        cmocka_unit_test(test_isdu_out_of_order_sequence),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
