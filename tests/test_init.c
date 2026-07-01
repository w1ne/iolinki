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

#include "iolinki/device.h"

/* Local mocks to avoid linking issues with CMocka symbols */
static int local_mock_phy_init(void* user)
{
    (void) user;
    return (int) mock();
}
static void local_mock_phy_set_mode(void* user, iolink_phy_mode_t mode)
{
    (void) user;
    (void) mode;
}
static void local_mock_phy_set_baudrate(void* user, iolink_baudrate_t baudrate)
{
    (void) user;
    (void) baudrate;
}
static int local_mock_phy_send(void* user, const uint8_t* data, size_t len)
{
    (void) user;
    (void) data;
    (void) len;
    return 0;
}
static int local_mock_phy_recv_byte(void* user, uint8_t* byte)
{
    (void) user;
    (void) byte;
    return 0;
}

static const iolink_phy_api_t local_phy_mock = {.init = local_mock_phy_init,
                                                .set_mode = local_mock_phy_set_mode,
                                                .set_baudrate = local_mock_phy_set_baudrate,
                                                .send = local_mock_phy_send,
                                                .recv_byte = local_mock_phy_recv_byte};

static iolink_device_config_t make_device_config(void)
{
    iolink_device_config_t config;
    (void) memset(&config, 0, sizeof(config));
    config.phy = local_phy_mock;
    return config;
}

/* --- Tests --- */

static void test_iolink_init_success(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    iolink_device_config_t config = make_device_config();

    will_return(local_mock_phy_init, 0);
    int result = iolink_device_init(&ctx, &config);
    assert_int_equal(result, 0);
}

static void test_iolink_init_fail_null(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    int result = iolink_device_init(&ctx, NULL);
    assert_int_not_equal(result, 0);
}

static void test_iolink_init_fail_driver(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    iolink_device_config_t config = make_device_config();

    will_return(local_mock_phy_init, -1);
    int result = iolink_device_init(&ctx, &config);
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
