/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_ds.c
 * @brief Unit tests for Data Storage (DS)
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/data_storage.h"
#include "test_helpers.h"

static void test_ds_storage_integration(void **state)
{
    (void)state;
    iolink_ds_mock_reset();
    iolink_ds_init(&g_ds_storage_mock);
    
    uint8_t data[] = {0x11, 0x22, 0x33, 0x44};
    g_ds_storage_mock.write(0, data, 4);
    
    uint8_t read_back[4];
    g_ds_storage_mock.read(0, read_back, 4);
    assert_memory_equal(data, read_back, 4);
}

static void test_ds_checksum_calculation(void **state)
{
    (void)state;
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    uint16_t cs1 = iolink_ds_calc_checksum(data, sizeof(data));
    uint16_t cs2 = iolink_ds_calc_checksum(data, sizeof(data));
    
    assert_int_equal(cs1, cs2);
    assert_true(cs1 != 0);
    
    data[0] = 0xFF;
    uint16_t cs3 = iolink_ds_calc_checksum(data, sizeof(data));
    assert_true(cs1 != cs3);
}

static void test_ds_state_transitions(void **state)
{
    (void)state;
    iolink_ds_init(NULL);
    
    /* 1. Mismatch -> Download */
    iolink_ds_check(0xABCD);
    iolink_ds_process(); /* Req -> Downloading */
    iolink_ds_process(); /* Downloading -> Idle */
    
    /* 2. No data -> Upload */
    iolink_ds_check(0x0000);
    iolink_ds_process(); /* Req -> Uploading */
    iolink_ds_process(); /* Uploading -> Idle */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_ds_checksum_calculation),
        cmocka_unit_test(test_ds_state_transitions),
        cmocka_unit_test(test_ds_storage_integration),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
