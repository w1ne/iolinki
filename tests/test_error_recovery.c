/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_error_recovery.c
 * @brief Unit tests for error detection and recovery
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/iolink.h"
#include "iolinki/events.h"
#include "iolinki/dll.h"
#include "test_helpers.h"

static void test_crc_error_recovery(void **state)
{
    (void)state;
    iolink_config_t config = { .m_seq_type = IOLINK_M_SEQ_TYPE_1_1, .pd_in_len = 1, .pd_out_len = 1 };
    
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_init(&g_phy_mock, &config);
    
    move_to_operate();

    /* 2. Simulate 3 CRC errors (Type 1_1 with 1-byte PD: MC, CKT, PD(1), OD(1), CK = 5 bytes) */
    for (int r = 0; r < 3; r++) {
        /* Provide the whole frame in the mock queue */
        will_return(mock_phy_recv_byte, 1); will_return(mock_phy_recv_byte, 0x80); /* MC */
        will_return(mock_phy_recv_byte, 1); will_return(mock_phy_recv_byte, 0x00); /* CKT */
        will_return(mock_phy_recv_byte, 1); will_return(mock_phy_recv_byte, 0x00); /* PD */
        will_return(mock_phy_recv_byte, 1); will_return(mock_phy_recv_byte, 0x00); /* OD */
        will_return(mock_phy_recv_byte, 1); will_return(mock_phy_recv_byte, 0xFF); /* Bad CRC */
        will_return(mock_phy_recv_byte, 0); /* End frame */
        
        /* One call to process the whole available frame */
        iolink_process();
    }

    /* 3. Check if error was detected (Event 0x5000 triggered after 3 retries) */
    assert_true(iolink_events_pending(iolink_get_events_ctx()));
}

static void test_communication_timeout(void **state)
{
    (void)state;
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_init(&g_phy_mock, NULL);
    
    /* Ensure no data available */
    will_return(mock_phy_recv_byte, 0);
    iolink_process();
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_crc_error_recovery),
        cmocka_unit_test(test_communication_timeout),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
