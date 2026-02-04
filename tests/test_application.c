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

#include "iolinki/iolink.h"
#include "iolinki/application.h"
#include "test_helpers.h"

static void test_pd_input_update_flow(void **state)
{
    (void) state;
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_init(&g_phy_mock, NULL);

    uint8_t data[] = {0xAA, 0xBB};
    int res = iolink_pd_input_update(data, sizeof(data), true);
    assert_int_equal(res, 0);

    /* Check internal state via output read (as simple proxy) */
    /* Note: iolink_pd_output_read reads FROM master, so this is not a direct mirror.
       We just verify the API doesn't crash here. */
}

static void test_pd_output_read_flow(void **state)
{
    (void) state;
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_init(&g_phy_mock, NULL);

    uint8_t buf[16];
    int res = iolink_pd_output_read(buf, sizeof(buf));
    /* Initial state should be 0 length or zeroed */
    assert_int_equal(res, 0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pd_input_update_flow),
        cmocka_unit_test(test_pd_output_read_flow),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
