/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_isdu_stress.c
 * @brief Stress tests for ISDU messaging engine
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "iolinki/isdu.h"
#include "iolinki/params.h"
#include "iolinki/device_info.h"

static void test_rapid_concurrent_requests(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    static const iolink_device_info_t info = {
        .vendor_name = "iolinki",
        .vendor_id = 0x1234,
        .device_id = 0x567890,
    };
    iolink_device_info_init(&info);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* 1. Start a write request */
    iolink_isdu_collect_byte(&ctx, 0x81); /* Start, Seq=1 */
    iolink_isdu_collect_byte(&ctx, 0xA2); /* Write, Len=2 */

    /* 2. Send another Start bit immediately (Collision/Concurrency) */
    iolink_isdu_collect_byte(&ctx, 0x82);

    iolink_isdu_process(&ctx);

    /* 3. Verify Busy response (0x8030) or graceful restart?
       Spec says device should return Busy if it can't handle it. */
    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte & 0x80, 0x80); /* START */

    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80); /* Error MSB */

    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte & 0x40, 0x40); /* LAST */

    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x30); /* Busy LSB */
}

static void test_maximum_segmented_transfer(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    static const iolink_device_info_t info = {
        .vendor_name = "iolinki",
        .vendor_id = 0x1234,
        .device_id = 0x567890,
    };
    iolink_device_info_init(&info);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Write 16 bytes to Index 0x0018 (App Tag) using segmentation */
    /* Header: [RW+Len] [ExtLen] [IndexH] [IndexL] [Subindex] */
    iolink_isdu_collect_byte(&ctx, 0x81); /* Start, Seq=1, !Last */
    iolink_isdu_collect_byte(&ctx, 0xAF); /* Write, Len=15 (extended length follows) */

    /* Extended length: 16 bytes total */
    iolink_isdu_collect_byte(&ctx, 0x02); /* Seq=2, !Last */
    iolink_isdu_collect_byte(&ctx, 0x10); /* Length = 16 */

    /* Index */
    iolink_isdu_collect_byte(&ctx, 0x03); /* Seq=3, !Last */
    iolink_isdu_collect_byte(&ctx, 0x00); /* Index MSB */

    iolink_isdu_collect_byte(&ctx, 0x04); /* Seq=4, !Last */
    iolink_isdu_collect_byte(&ctx, 0x18); /* Index LSB */

    iolink_isdu_collect_byte(&ctx, 0x05); /* Seq=5, !Last */
    iolink_isdu_collect_byte(&ctx, 0x00); /* Subindex */

    /* Data: 16 bytes */
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
    if ((byte & 0xC0) != 0xC0) {
        uint8_t err = 0;
        iolink_isdu_get_response_byte(&ctx, &err);
        printf("DEBUG: Expected 0xC0, got 0x%02X. Next byte: 0x%02X\n", byte, err);
    }
    assert_int_equal(byte & 0xC0, 0xC0); /* Start | Last */
}

static void test_sequence_number_wraparound(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    static const iolink_device_info_t info = {
        .vendor_name = "iolinki",
        .vendor_id = 0x1234,
        .device_id = 0x567890,
    };
    iolink_device_info_init(&info);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Test sequence number wraparound (0-63) */
    /* Start with seq 61 */
    iolink_isdu_collect_byte(&ctx, 0x80 | 61); /* Start, Seq=61, !Last */
    iolink_isdu_collect_byte(&ctx, 0x90);      /* Read, Len=0 */

    iolink_isdu_collect_byte(&ctx, 62);   /* Seq=62, !Last */
    iolink_isdu_collect_byte(&ctx, 0x00); /* Index MSB */

    iolink_isdu_collect_byte(&ctx, 63);   /* Seq=63, !Last */
    iolink_isdu_collect_byte(&ctx, 0x18); /* Index LSB (App Tag) */

    iolink_isdu_collect_byte(&ctx, 0x40 | 0); /* Last, Seq=0 (wraparound) */
    iolink_isdu_collect_byte(&ctx, 0x00);     /* Subindex */

    iolink_isdu_process(&ctx);

    /* Verify successful response */
    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte & 0x80, 0x80); /* START */
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
