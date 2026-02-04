/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_init.c
 * @brief Unit tests for IO-Link stack initialization
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/iolink.h"

/* Local mocks to avoid linking issues with CMocka symbols */
static int local_mock_phy_init(void)
{
    return (int) mock();
}
static void local_mock_phy_set_mode(iolink_phy_mode_t mode)
{
    (void) mode;
}
static void local_mock_phy_set_baudrate(iolink_baudrate_t baudrate)
{
    (void) baudrate;
}
static int local_mock_phy_send(const uint8_t *data, size_t len)
{
    (void) data;
    (void) len;
    return 0;
}
static int local_mock_phy_recv_byte(uint8_t *byte)
{
    (void) byte;
    return 0;
}

static const iolink_phy_api_t local_phy_mock = {.init = local_mock_phy_init,
                                                .set_mode = local_mock_phy_set_mode,
                                                .set_baudrate = local_mock_phy_set_baudrate,
                                                .send = local_mock_phy_send,
                                                .recv_byte = local_mock_phy_recv_byte};

/* --- Tests --- */

static void test_iolink_init_success(void **state)
{
    (void) state;
    will_return(local_mock_phy_init, 0);
    int result = iolink_init(&local_phy_mock, NULL);
    assert_int_equal(result, 0);
}

static void test_iolink_init_fail_null(void **state)
{
    (void) state;
    int result = iolink_init(NULL, NULL);
    assert_int_not_equal(result, 0);
}

static void test_iolink_init_fail_driver(void **state)
{
    (void) state;
    will_return(local_mock_phy_init, -1);
    int result = iolink_init(&local_phy_mock, NULL);
    assert_int_equal(result, -1);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_iolink_init_success),
        cmocka_unit_test(test_iolink_init_fail_null),
        cmocka_unit_test(test_iolink_init_fail_driver),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
