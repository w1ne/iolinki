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

/* Check the A.1.5 reply layout: [PD-in][OD] CKS with no leading status octet.
   expect_nothing=0 asserts the PD-valid flag (CKS bit 6 clear); 1 asserts the
   PD-invalid flag (CKS bit 6 set). The configured TYPE_2_2 reply is
   PD-in(2) + OD(1) + CKS, so the CKS is the fourth octet. */
static int check_reply_flags(const LargestIntegralType value,
                             const LargestIntegralType check_value_data)
{
    const uint8_t* data = (const uint8_t*) value;
    const uint8_t expected_invalid = (uint8_t) check_value_data;
    const uint8_t cks = data[3];
    const uint8_t invalid = (uint8_t) ((cks & 0x40U) >> 6);

    if (invalid != expected_invalid) {
        print_error("CKS 0x%02X: Expected PD-invalid %u, got %u\n", cks, expected_invalid, invalid);
        return 0;
    }
    return 1;
}

static void test_reply_layout_no_status_octet(void** state)
{
    (void) state;
    iolink_config_t config = {.pd_in_len = 2, .pd_out_len = 2, .m_seq_type = IOLINK_M_SEQ_TYPE_2_2};
    iolink_test_device_t dev;

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);
    move_to_operate_ctx(&dev.ctx);

    uint8_t frame[5] = {0x80, IOLINK_MSEQ_TYPE_2, 0x00, 0x00, 0x00};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 5));

    uint8_t input[2] = {0x11, 0x22};
    iolink_device_pd_input_update(&dev.ctx, input, 2, true);

    for (int i = 0; i < 5; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);

    /* 2 PD-in + 1 OD + CKS = 4 octets (TYPE_2_2 per Table A.10), PD valid so
       CKS bit 6 is clear. */
    expect_check(mock_phy_send, data, check_reply_flags, (void*) (uintptr_t) 0U);
    expect_value(mock_phy_send, len, 4);
    will_return(mock_phy_send, 0);
    iolink_device_process(&dev.ctx);
}

static void test_reply_flags_pd_invalid(void** state)
{
    (void) state;
    iolink_config_t config = {.pd_in_len = 2, .pd_out_len = 2, .m_seq_type = IOLINK_M_SEQ_TYPE_2_2};
    iolink_test_device_t dev;

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);
    move_to_operate_ctx(&dev.ctx);

    uint8_t frame[5] = {0x80, IOLINK_MSEQ_TYPE_2, 0x00, 0x00, 0x00};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 5));

    /* Mark the input Process Data as invalid; CKS bit 6 must be set. */
    iolink_device_pd_input_update(&dev.ctx, (const uint8_t*) "\x11\x22", 2, false);

    for (int i = 0; i < 5; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);

    expect_check(mock_phy_send, data, check_reply_flags, (void*) (uintptr_t) 1U);
    expect_value(mock_phy_send, len, 4);
    will_return(mock_phy_send, 0);
    iolink_device_process(&dev.ctx);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_reply_layout_no_status_octet),
        cmocka_unit_test(test_reply_flags_pd_invalid),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
