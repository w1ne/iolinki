/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_app_pd.c
 * @brief Unit tests for Process Data Application functionality (reply layout)
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/device.h"
#include "iolinki/application.h"
#include "iolinki/crc.h"
#include "iolinki/protocol.h"
#include "test_helpers.h"

/* Figures A.9/A.10: the Device reply to a READ M-sequence is [OD][PD-in] CKS
   with no leading status octet. PD validity lives in CKS bit 6 (1 = invalid)
   and there is no toggle bit (A.1.5). The configured TYPE_2_2 reply is
   OD(1) + PD-in(2) + CKS, and the byte-exact arrays pin that order. */

static void test_reply_layout_od_first_no_status_octet(void** state)
{
    (void) state;
    iolink_config_t config = {.pd_in_len = 2, .pd_out_len = 0, .m_seq_type = IOLINK_M_SEQ_TYPE_2_2};
    iolink_test_device_t dev;

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);
    move_to_operate_ctx(&dev.ctx);

    uint8_t frame[2] = {0x80, IOLINK_MSEQ_TYPE_2}; /* read request: no OD */
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 2));

    uint8_t input[2] = {0x11, 0x22};
    iolink_device_pd_input_update(&dev.ctx, input, 2, true);

    for (int i = 0; i < 2; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);

    /* MC 0x80 is a READ M-sequence, so the reply is OD(1), PD-in(2), CKS:
       0x00 0x11 0x22 0x2D. The process channel read serves no OD octet
       (0x00); CKS bit 6 is clear because the PD is valid (A.1.6). */
    uint8_t expected[] = {0x00, 0x11, 0x22, 0x2D};
    expect_memory(mock_phy_send, data, expected, sizeof(expected));
    expect_value(mock_phy_send, len, sizeof(expected));
    will_return(mock_phy_send, 0);
    iolink_device_process(&dev.ctx);
}

static void test_reply_flags_pd_invalid(void** state)
{
    (void) state;
    iolink_config_t config = {.pd_in_len = 2, .pd_out_len = 0, .m_seq_type = IOLINK_M_SEQ_TYPE_2_2};
    iolink_test_device_t dev;

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);
    move_to_operate_ctx(&dev.ctx);

    uint8_t frame[2] = {0x80, IOLINK_MSEQ_TYPE_2}; /* read request: no OD */
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 2));

    /* Mark the input Process Data as invalid; CKS bit 6 must be set. */
    iolink_device_pd_input_update(&dev.ctx, (const uint8_t*) "\x11\x22", 2, false);

    for (int i = 0; i < 2; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);

    /* Same READ reply layout, with the PD-invalid flag in CKS bit 6:
       0x40 | 0x35 = 0x75. */
    uint8_t expected[] = {0x00, 0x11, 0x22, 0x75};
    expect_memory(mock_phy_send, data, expected, sizeof(expected));
    expect_value(mock_phy_send, len, sizeof(expected));
    will_return(mock_phy_send, 0);
    iolink_device_process(&dev.ctx);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_reply_layout_od_first_no_status_octet),
        cmocka_unit_test(test_reply_flags_pd_invalid),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
