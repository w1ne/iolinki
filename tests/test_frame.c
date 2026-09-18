/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_frame.c
 * @brief Unit tests for shared IO-Link frame helpers
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <stdbool.h>

#include "iolinki/frame.h"
#include "iolinki/protocol.h"

static void test_encode_type0_idle(void** state)
{
    (void) state;
    uint8_t frame[2] = {0U};

    assert_int_equal(iolink_frame_encode_type0(0x00U, frame, sizeof(frame)), 2);
    assert_memory_equal(frame, ((const uint8_t[]){0x00U, 0x2DU}), sizeof(frame));
}

static void test_encode_type0_transition(void** state)
{
    (void) state;
    uint8_t frame[2] = {0U};

    assert_int_equal(iolink_frame_encode_type0(0x0FU, frame, sizeof(frame)), 2);
    assert_memory_equal(frame, ((const uint8_t[]){0x0FU, 0x2DU}), sizeof(frame));
}

static void test_encode_type1_empty_cycle(void** state)
{
    (void) state;
    uint8_t frame[3] = {0U};

    /* MC, CKT(type1 | ck6), OD. Figure A.2/A.6: no trailing checksum. */
    assert_int_equal(iolink_frame_encode_type1_cycle(NULL, 0U, 1U, frame, sizeof(frame)), 3);
    assert_memory_equal(frame, ((const uint8_t[]){0x00U, 0x75U, 0x00U}), sizeof(frame));
}

static void test_encode_type0_rejects_undersized_buffer(void** state)
{
    (void) state;
    uint8_t frame[1] = {0U};

    assert_int_equal(iolink_frame_encode_type0(0x00U, frame, sizeof(frame)), -1);
}

static void test_encode_type1_rejects_zero_od_len(void** state)
{
    (void) state;
    uint8_t frame[4] = {0U};

    assert_int_equal(iolink_frame_encode_type1_cycle(NULL, 0U, 0U, frame, sizeof(frame)), -1);
}

static void test_encode_type1_rejects_oversized_od_len(void** state)
{
    (void) state;
    uint8_t frame[8] = {0U};

    assert_int_equal(iolink_frame_encode_type1_cycle(NULL, 0U, (uint8_t) (IOLINK_OD_MAX_SIZE + 1U),
                                                     frame, sizeof(frame)),
                     -1);
}

static void test_encode_type1_allows_max_od_len(void** state)
{
    (void) state;
    uint8_t frame[IOLINK_M_SEQ_HEADER_LEN + IOLINK_OD_MAX_SIZE] = {0U};

    assert_int_equal(
        iolink_frame_encode_type1_cycle(NULL, 0U, IOLINK_OD_MAX_SIZE, frame, sizeof(frame)),
        (int) sizeof(frame));
}

static void test_decode_operate_response_with_pd(void** state)
{
    (void) state;
    /* [PD-in 0xA5][OD 0x00] CKS(no flags) = 0xA5 0x00 0x22 (A.1.5/A.1.6). */
    const uint8_t frame[] = {0xA5U, 0x00U, 0x22U};
    iolink_frame_operate_response_t resp = {0};

    assert_int_equal(iolink_frame_decode_operate_response(frame, sizeof(frame), 1U, 1U, &resp), 0);
    assert_true(resp.checksum_ok);
    assert_true(resp.pd_valid);
    assert_false(resp.event_pending);
    assert_int_equal(resp.pd_len, 1U);
    assert_int_equal(resp.pd[0], 0xA5U);
    assert_int_equal(resp.od[0], 0x00U);
}

static void test_decode_operate_response_event_flag(void** state)
{
    (void) state;
    /* Same message with the Event flag (CKS bit 7) set: 0xA5 0x00 0x8A. */
    const uint8_t frame[] = {0xA5U, 0x00U, 0x8AU};
    iolink_frame_operate_response_t resp = {0};

    assert_int_equal(iolink_frame_decode_operate_response(frame, sizeof(frame), 1U, 1U, &resp), 0);
    assert_true(resp.checksum_ok);
    assert_true(resp.pd_valid);
    assert_true(resp.event_pending);
}

static void test_decode_operate_response_pd_invalid_flag(void** state)
{
    (void) state;
    /* CKS bit 6 set means PD is invalid: 0xA5 0x00 0x7A. */
    const uint8_t frame[] = {0xA5U, 0x00U, 0x7AU};
    iolink_frame_operate_response_t resp = {0};

    assert_int_equal(iolink_frame_decode_operate_response(frame, sizeof(frame), 1U, 1U, &resp), 0);
    assert_true(resp.checksum_ok);
    assert_false(resp.pd_valid);
}

static void test_decode_operate_response_bad_checksum_succeeds_with_flag_false(void** state)
{
    (void) state;
    /* Correct layout, wrong checksum bits. */
    const uint8_t frame[] = {0xA5U, 0x00U, 0x00U};
    iolink_frame_operate_response_t resp = {0};

    assert_int_equal(iolink_frame_decode_operate_response(frame, sizeof(frame), 1U, 1U, &resp), 0);
    assert_false(resp.checksum_ok);
    assert_true(resp.pd_valid);
    assert_int_equal(resp.pd_len, 1U);
    assert_int_equal(resp.pd[0], 0xA5U);
}

static void test_decode_operate_response_rejects_oversized_pd_in_len(void** state)
{
    (void) state;
    const uint8_t frame[] = {0x00U, 0x00U};
    iolink_frame_operate_response_t resp = {0};

    assert_int_equal(iolink_frame_decode_operate_response(
                         frame, sizeof(frame), (uint8_t) (IOLINK_PD_IN_MAX_SIZE + 1U), 0U, &resp),
                     -1);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_encode_type0_idle),
        cmocka_unit_test(test_encode_type0_transition),
        cmocka_unit_test(test_encode_type1_empty_cycle),
        cmocka_unit_test(test_encode_type0_rejects_undersized_buffer),
        cmocka_unit_test(test_encode_type1_rejects_zero_od_len),
        cmocka_unit_test(test_encode_type1_rejects_oversized_od_len),
        cmocka_unit_test(test_encode_type1_allows_max_od_len),
        cmocka_unit_test(test_decode_operate_response_with_pd),
        cmocka_unit_test(test_decode_operate_response_event_flag),
        cmocka_unit_test(test_decode_operate_response_pd_invalid_flag),
        cmocka_unit_test(test_decode_operate_response_bad_checksum_succeeds_with_flag_false),
        cmocka_unit_test(test_decode_operate_response_rejects_oversized_pd_in_len),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
