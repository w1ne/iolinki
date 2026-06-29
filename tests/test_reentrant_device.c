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
#include "iolinki/protocol.h"

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

static void test_parameter_contexts_keep_writable_tags_isolated(void** state)
{
    (void) state;
    iolink_device_info_ctx_t info_a;
    iolink_device_info_ctx_t info_b;
    iolink_params_ctx_t params_a;
    iolink_params_ctx_t params_b;
    const uint8_t tag_a[] = "DeviceA";
    const uint8_t tag_b[] = "DeviceB";
    uint8_t out_a[32] = {0};
    uint8_t out_b[32] = {0};

    iolink_device_info_ctx_init(&info_a, NULL);
    iolink_device_info_ctx_init(&info_b, NULL);
    iolink_params_ctx_init(&params_a, &info_a);
    iolink_params_ctx_init(&params_b, &info_b);

    assert_int_equal(iolink_params_ctx_set(&params_a, IOLINK_IDX_APPLICATION_TAG, 0U, tag_a,
                                           sizeof(tag_a) - 1U, true),
                     0);
    assert_int_equal(iolink_params_ctx_set(&params_b, IOLINK_IDX_APPLICATION_TAG, 0U, tag_b,
                                           sizeof(tag_b) - 1U, true),
                     0);
    assert_int_equal(iolink_params_ctx_get(&params_a, IOLINK_IDX_APPLICATION_TAG, 0U, out_a,
                                           sizeof(out_a)),
                     (int) (sizeof(tag_a) - 1U));
    assert_int_equal(iolink_params_ctx_get(&params_b, IOLINK_IDX_APPLICATION_TAG, 0U, out_b,
                                           sizeof(out_b)),
                     (int) (sizeof(tag_b) - 1U));
    assert_memory_equal(out_a, tag_a, sizeof(tag_a) - 1U);
    assert_memory_equal(out_b, tag_b, sizeof(tag_b) - 1U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_device_context_api_initializes_two_configs),
        cmocka_unit_test(test_parameter_contexts_keep_writable_tags_isolated),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
