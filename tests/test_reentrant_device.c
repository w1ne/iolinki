/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>

#include "iolinki/device.h"

static int noop_user(void* user)
{
    (void) user;
    return 0;
}

static void test_device_context_api_initializes_two_configs(void** state)
{
    (void) state;
    iolink_device_ctx_t dev_a;
    iolink_device_ctx_t dev_b;
    static iolink_device_phy_t phy = {.init = noop_user};
    iolink_device_config_t cfg_a = {
        .phy = phy,
        .stack = {
            .m_seq_type = IOLINK_M_SEQ_TYPE_1_1,
            .min_cycle_time = 10U,
            .pd_in_len = 1U,
            .pd_out_len = 0U,
            .t_pd_us = 0U,
        },
    };
    iolink_device_config_t cfg_b = {
        .phy = phy,
        .stack = {
            .m_seq_type = IOLINK_M_SEQ_TYPE_2_1,
            .min_cycle_time = 10U,
            .pd_in_len = 2U,
            .pd_out_len = 2U,
            .t_pd_us = 0U,
        },
    };

    assert_true(iolink_device_ctx_size() == sizeof(iolink_device_ctx_t));
    assert_int_equal(iolink_device_init(&dev_a, &cfg_a), 0);
    assert_int_equal(iolink_device_init(&dev_b, &cfg_b), 0);
    assert_int_equal(iolink_device_get_pd_in_len(&dev_a), 1U);
    assert_int_equal(iolink_device_get_pd_out_len(&dev_a), 0U);
    assert_int_equal(iolink_device_get_pd_in_len(&dev_b), 2U);
    assert_int_equal(iolink_device_get_pd_out_len(&dev_b), 2U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_device_context_api_initializes_two_configs),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
