/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_dll.c
 * @brief Unit tests for Data Link Layer (DLL) state machine
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/iolink.h"
#include "iolinki/dll.h"
#include "test_helpers.h"

static void test_dll_startup_to_preoperate(void **state)
{
    (void)state;
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_init(&g_phy_mock, NULL);

    /* 1. Send any byte from master to trigger transition from STARTUP to PREOPERATE */
    will_return(mock_phy_recv_byte, 1);     /* res=1 */
    will_return(mock_phy_recv_byte, 0x00);  /* byte=0x00 */
    will_return(mock_phy_recv_byte, 0);     /* res=0 (end) */
    
    iolink_process();
}

static void test_dll_preoperate_to_operate(void **state)
{
    (void)state;
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_init(&g_phy_mock, NULL);

    /* Move to PREOPERATE */
    will_return(mock_phy_recv_byte, 1); will_return(mock_phy_recv_byte, 0x00); 
    will_return(mock_phy_recv_byte, 0); 
    iolink_process();
    
    /* Move to OPERATE: MC=0x0F, CK=0x0D */
    will_return(mock_phy_recv_byte, 1); will_return(mock_phy_recv_byte, 0x0F);
    will_return(mock_phy_recv_byte, 0); 
    iolink_process();
    
    will_return(mock_phy_recv_byte, 1); will_return(mock_phy_recv_byte, 0x0D);
    will_return(mock_phy_recv_byte, 0); 
    iolink_process();
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_dll_startup_to_preoperate),
        cmocka_unit_test(test_dll_preoperate_to_operate),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
