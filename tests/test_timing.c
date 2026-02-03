/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_timing.c
 * @brief Unit tests for time utilities and timing logic placeholders
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <unistd.h>

#include "iolinki/time_utils.h"

static void test_time_get_ms(void **state)
{
    (void)state;
    uint32_t t1 = iolink_time_get_ms();
    usleep(10000); /* 10ms */
    uint32_t t2 = iolink_time_get_ms();
    
    assert_true(t2 >= t1 + 10);
    assert_true(t2 < t1 + 20); /* Allow some jitter */
}

static void test_time_get_us(void **state)
{
    (void)state;
    uint64_t t1 = iolink_time_get_us();
    usleep(1000); /* 1ms */
    uint64_t t2 = iolink_time_get_us();
    
    assert_true(t2 >= t1 + 1000);
    assert_true(t2 < t1 + 2000);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_time_get_ms),
        cmocka_unit_test(test_time_get_us),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
