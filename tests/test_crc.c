/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_crc.c
 * @brief Unit tests for the IO-Link A.1.6 message checksum
 *
 * The expected values are derived from the normative formula in IO-Link
 * Interface Specification V1.1.5 A.1.6 / equations (A.1), not from the
 * implementation under test. The checksum octet is passed in with its
 * checksum bits (0-5) cleared; type/event/PD-status bits are preserved.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>

#include "iolinki/crc.h"

/* Normalise a checksum/type octet by clearing bits 0-5 (A.1.6). */
static uint8_t zero_checksum_bits(uint8_t octet)
{
    return (uint8_t) (octet & 0xC0U);
}

static void test_checksum6_master_frames(void** state)
{
    (void) state;
    /* Master frames: MC, CKT (type bits only), data... */
    const uint8_t frame1[] = {0x00, 0x00}; /* (0x00,0x00) -> 0x2D */
    assert_int_equal(iolink_checksum6(frame1, 2U), 0x2DU);

    const uint8_t frame2[] = {0xA2, 0x00}; /* (0xA2,0x00) -> 0x00 */
    assert_int_equal(iolink_checksum6(frame2, 2U), 0x00U);

    const uint8_t frame3[] = {0x20, 0x00, 0x99}; /* -> 0x06 */
    assert_int_equal(iolink_checksum6(frame3, 3U), 0x06U);

    /* A TYPE_1 write frame MC=0x00, CKT type bits=0x40, OD=A5, CK=5A. */
    const uint8_t frame4[] = {0x00, 0x40, 0xA5, 0x5A};
    assert_int_equal(iolink_checksum6(frame4, 4U), 0x35U);

    /* A TYPE_2 read frame MC=0x80, CKT type bits=0x80 -> 0x2D. */
    const uint8_t frame5[] = {0x80, 0x80};
    assert_int_equal(iolink_checksum6(frame5, 2U), 0x2DU);
}

static void test_checksum6_device_replies(void** state)
{
    (void) state;
    /* Device replies: data..., CKS (event<<7 | pdinvalid<<6 | ck6). */
    const uint8_t reply1[] = {0x10}; /* -> 0x39 */
    assert_int_equal(iolink_checksum6(reply1, 1U), 0x39U);

    const uint8_t reply2[] = {0xA5}; /* -> 0x22 */
    assert_int_equal(iolink_checksum6(reply2, 1U), 0x22U);

    /* The flag bits participate in the XOR before compression: an Event flag
       on top of 0xA5 changes the result (A.1.6). */
    const uint8_t reply3[] = {0xC5};
    assert_int_equal(iolink_checksum6(reply3, 1U), 0x1EU);

    const uint8_t reply4[] = {0x00}; /* empty data -> 0x2D */
    assert_int_equal(iolink_checksum6(reply4, 1U), 0x2DU);
}

static void test_checksum6_zeroing_is_caller_duty(void** state)
{
    (void) state;
    /* The helper expects bits 0-5 already cleared. Verify that clearing the
       low bits of the same octet yields the documented value. */
    const uint8_t with_bits[] = {0x00, 0x2DU};
    const uint8_t cleared[] = {zero_checksum_bits(with_bits[0]), zero_checksum_bits(with_bits[1])};
    assert_int_equal(iolink_checksum6(cleared, 2U), 0x2DU);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_checksum6_master_frames),
        cmocka_unit_test(test_checksum6_device_replies),
        cmocka_unit_test(test_checksum6_zeroing_is_caller_duty),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
