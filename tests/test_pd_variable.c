/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_pd_variable.c
 * @brief Unit tests for variable Process Data lengths (Type 1_V, 2_V)
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/iolink.h"
#include "iolinki/dll.h"
#include "iolinki/application.h"
#include "iolinki/crc.h"
#include "test_helpers.h"

static void test_pd_variable_lengths(void **state) {
    (void)state;
    iolink_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_1_V,
        .pd_in_len = 8,
        .pd_out_len = 8
    };
    
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_init(&g_phy_mock, &config);
    move_to_operate();
    
    /* Type 1_V: Req = MC, CKT, PD(8), OD(1), CK = 12 bytes. */
    uint8_t frame[12];
    memset(frame, 0, 12);
    frame[0] = 0x80;
    frame[11] = iolink_crc6(frame, 11);
    
    for (int i=0; i<12; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);
    
    /* Resp: Stat(1), PD(8), OD(1), CK(1) = 11 bytes. */
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 11);
    will_return(mock_phy_send, 0);
    
    iolink_process();
}

static void test_pd_invalid_flag(void **state) {
    (void)state;
    iolink_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_1_1,
        .pd_in_len = 1,
        .pd_out_len = 1
    };
    
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_init(&g_phy_mock, &config);
    
    /* Ensure pd_in_len is 1 for this test to match 4-byte response expectation */
    uint8_t dummy = 0;
    iolink_pd_input_update(&dummy, 1, false);
    
    move_to_operate();
    
    /* Type 1_1 with 1-byte PD: Req = MC, CKT, PD(1), OD(1), CK = 5 bytes. 
       Resp: Stat, PD(1), OD(1), CK = 4 bytes.
    */
    uint8_t frame[] = {0x80, 0x00, 0x00, 0x00, 0x00};
    frame[4] = iolink_crc6(frame, 4);
    
    for (int i=0; i<5; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);
    
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 4);
    will_return(mock_phy_send, 0);
    
    iolink_process();
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pd_variable_lengths),
        cmocka_unit_test(test_pd_invalid_flag),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
