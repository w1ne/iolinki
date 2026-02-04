/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

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
    /* Known test vectors for IO-Link CRC6 (Polynomial 0x1D, Seed 0x15) 
       Reference values verified by implementation:
       0x00 0x00 -> 0x24
       0x0F 0x00 -> 0x0D
    */
    uint8_t data1[] = {0x00, 0x00};
    assert_int_equal(iolink_crc6(data1, 2), 0x24);
    
    uint8_t data2[] = {0x0F, 0x00};
    assert_int_equal(iolink_crc6(data2, 2), 0x0D);
}

static void test_checksum_ck_basic(void **state)
{
    (void)state;
    /* MC=0, CKT=0 -> CRC6(0x00, 0x00) = 0x24 */
    assert_int_equal(iolink_checksum_ck(0x00, 0x00), 0x24);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_crc6_basic),
        cmocka_unit_test(test_checksum_ck_basic),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
