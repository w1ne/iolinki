/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_m_sequence_types.c
 * @brief Unit tests for M-sequence Types 1_1, 1_2, 2_1, 2_2
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

static void test_m_seq_type_1_1(void **state) {
    (void)state;
    iolink_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_1_1,
        .pd_in_len = 2,
        .pd_out_len = 2
    };
    
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_init(&g_phy_mock, &config);
    
    move_to_operate();
    
    uint8_t input_pd[2] = {0xAA, 0xBB};
    iolink_pd_input_update(input_pd, 2, true);
    
    /* Type 1_1 with 2-byte PD: Req = MC, CKT, PD(2), OD(1), CK = 6 bytes */
    uint8_t frame[] = {0x80, 0x00, 0x11, 0x22, 0x00, 0x00};
    frame[5] = iolink_crc6(frame, 5);
    
    for (int i=0; i<6; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);
    
    /* Resp = Stat, PD(2), OD(1), CK = 5 bytes */
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 5);
    will_return(mock_phy_send, 0);
    
    iolink_process();
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_m_seq_type_1_1),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
