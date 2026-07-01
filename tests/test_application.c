/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_application.c
 * @brief Unit tests for application-layer Process Data API
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/device.h"
#include "iolinki/application.h"
#include "test_helpers.h"

static void test_pd_input_update_flow(void** state)
{
    (void) state;
    iolink_test_device_t dev;

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, NULL, NULL), 0);

    uint8_t data[] = {0xAA, 0xBB};
    int res = iolink_device_pd_input_update(&dev.ctx, data, sizeof(data), true);
    assert_int_equal(res, 0);

    /* Check internal state via output read (as simple proxy) */
    /* Note: PD output reads FROM master, so this is not a direct mirror.
       We just verify the API doesn't crash here. */
}

static void test_pd_output_read_flow(void** state)
{
    (void) state;
    iolink_test_device_t dev;

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, NULL, NULL), 0);

    uint8_t buf[16];
    int res = iolink_device_pd_output_read(&dev.ctx, buf, sizeof(buf));
    /* Initial state should be 0 length or zeroed */
    assert_int_equal(res, 0);
}

static int g_cb_startup;
static int g_cb_preoperate;
static int g_cb_operate;
static int g_cb_pd_output;

static void cb_startup(void)
{
    g_cb_startup++;
}
static void cb_preoperate(void)
{
    g_cb_preoperate++;
}
static void cb_operate(void)
{
    g_cb_operate++;
}
static void cb_pd_output(uint8_t* data, uint8_t len)
{
    (void) data;
    (void) len;
    g_cb_pd_output++;
}

static void test_app_callbacks_lifecycle(void** state)
{
    (void) state;
    iolink_test_device_t dev;

    g_cb_startup = 0;
    g_cb_preoperate = 0;
    g_cb_operate = 0;
    g_cb_pd_output = 0;

    static const iolink_app_callbacks_t cbs = {
        .on_startup = cb_startup,
        .on_preoperate = cb_preoperate,
        .on_operate = cb_operate,
        .on_pd_output = cb_pd_output,
    };

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, NULL, &cbs), 0);

    /* Registered before init -> initial STARTUP announced. */
    assert_true(g_cb_startup >= 1);

    move_to_operate_ctx(&dev.ctx);

    /* PREOPERATE is traversed during the handshake; the state-change hook fires
       even though the transition is transient. */
    assert_true(g_cb_preoperate >= 1);

    if (iolink_device_get_state(&dev.ctx) == IOLINK_DLL_STATE_OPERATE) {
        assert_true(g_cb_operate >= 1);
        assert_true(g_cb_pd_output >= 1);
    }
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pd_input_update_flow),
        cmocka_unit_test(test_pd_output_read_flow),
        cmocka_unit_test(test_app_callbacks_lifecycle),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
