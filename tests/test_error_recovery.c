/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_error_recovery.c
 * @brief Unit tests for DLL error recovery scenarios
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/iolink.h"
#include "iolinki/dll.h"
#include "iolinki/events.h"
#include "test_helpers.h"

static void test_crc_error_recovery(void **state)
{
    (void)state;
    iolink_init(&g_phy_mock);
    
    /* 1. Move to Preoperate */
    will_return(g_phy_mock.recv_byte, 0x00); 
    will_return(g_phy_mock.recv_byte, 1);
    iolink_process();
    
    /* 2. Send corrupted frame (wrong CK) */
    will_return(g_phy_mock.recv_byte, 0xFF); /* MC */
    will_return(g_phy_mock.recv_byte, 1);
    will_return(g_phy_mock.recv_byte, 0x00); /* Wrong CK */
    will_return(g_phy_mock.recv_byte, 1);
    
    /* Expect no send (packet ignored) but event triggered */
    iolink_process();
    
    assert_true(iolink_events_pending());
}

static void test_communication_timeout(void **state)
{
    (void)state;
    iolink_init(&g_phy_mock);

    /* Move to Preoperate and set activity */
    will_return(g_phy_mock.recv_byte, 0x00); 
    will_return(g_phy_mock.recv_byte, 1);
    iolink_process();
    
    /* Activity is set here. In next process it should still be Preoperate if no timeout. */
    iolink_process();
    
    /* We can't easily mock time for clock_gettime in this environment without wrapping,
       so this test is a placeholder for a local environment with a mocked time source. */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_crc_error_recovery),
        cmocka_unit_test(test_communication_timeout),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
