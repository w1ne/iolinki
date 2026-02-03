/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_dll.c
 * @brief Unit tests for DLL state machine
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
    
    iolink_init(&g_phy_mock);
    
    /* 1. In Startup state, should transition to Preoperate on first byte */
    will_return(g_phy_mock.recv_byte, 0x00); /* Byte value */
    will_return(g_phy_mock.recv_byte, 1);    /* Success */
    
    iolink_process();
    
    /* Check internal state (could be done via a getter if added, 
       but for now we'll just verify no crash and future behavior) */
}

static void test_dll_preoperate_m_seq_type0(void **state)
{
    (void)state;
    
    iolink_init(&g_phy_mock);
    
    /* Move to Preoperate */
    will_return(g_phy_mock.recv_byte, 0x00); 
    will_return(g_phy_mock.recv_byte, 1);
    iolink_process();
    
    /* Send M-Sequence Type 0 (MC=0x00, CK=0x11 approx) 
       MC=0x00, CKT=0 -> CK=0x11 (based on previous iolink_crc6 result) 
       Actually let's use the real calculated CK.
    */
    
    /* MC byte */
    will_return(g_phy_mock.recv_byte, 0x00);
    will_return(g_phy_mock.recv_byte, 1);
    
    /* CK byte */
    will_return(g_phy_mock.recv_byte, 0x11); /* Placeholder for correct CK */
    will_return(g_phy_mock.recv_byte, 1);

    /* After 2 bytes, it should try to send response */
    expect_any(g_phy_mock.send, data);
    expect_value(g_phy_mock.send, len, 2);
    will_return(g_phy_mock.send, 2);

    /* End of recv_byte loop */
    will_return(g_phy_mock.recv_byte, 0);
    
    iolink_process();
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_dll_startup_to_preoperate),
        cmocka_unit_test(test_dll_preoperate_m_seq_type0),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
