/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_app_pd.c
 * @brief Unit tests for Process Data Application functionality (Toggle Bit)
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/iolink.h"
#include "iolinki/application.h"
#include "iolinki/crc.h"
#include "iolinki/protocol.h"
#include "test_helpers.h"

/* Custom checker to validate Status Byte (first byte of response) */
static int check_status_byte(const LargestIntegralType value,
                             const LargestIntegralType check_value_data)
{
    const uint8_t* data = (const uint8_t*) value;
    uint8_t expected_flags = (uint8_t) check_value_data;

    /* Byte 0 is Status. Check logic:
       Event(0x80) | Reserved/Toggle(0x40) | Valid(0x20) | DeviceStatus(0x1F)
       We expect Valid=1 (0x20) + DeviceStatus=OK(0x00).
       We verify Toggle bit (0x40) matches expected_flags. */

    uint8_t status = data[0];
    uint8_t toggle = status & IOLINK_OD_STATUS_PD_TOGGLE;
    uint8_t expected_toggle = expected_flags & IOLINK_OD_STATUS_PD_TOGGLE;

    if ((status & IOLINK_OD_STATUS_PD_VALID) != IOLINK_OD_STATUS_PD_VALID) {
        print_error("Status Byte 0x%02X: Expected PD_VALID (0x20) to be set\n", status);
        return 0;
    }

    if (toggle != expected_toggle) {
        print_error("Status Byte 0x%02X: Expected Toggle 0x%02X, got 0x%02X\n", status,
                    expected_toggle, toggle);
        return 0;
    }

    return 1;
}

static void test_pd_toggle_bit(void** state)
{
    (void) state;
    iolink_config_t config = {.pd_in_len = 2, .pd_out_len = 2, .m_seq_type = IOLINK_M_SEQ_TYPE_2_2};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_init(&g_phy_mock, &config);
    move_to_operate();

    /* Helper variables for frame simulation */
    uint8_t frame[7] = {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    frame[6] = iolink_crc6(frame, 6);

    /* 1. Initial State: Valid=False. Toggle should be 0 (default header init).
       Update: Actually, if Valid=False, Toggle might verify as anything.
       Let's Start with Valid=True to force a known state.
    */
    uint8_t input[2] = {0x11, 0x22};

    /* Update 1: Valid=True. Flip 0->1. Toggle bit should be 1 (0x40). */
    iolink_pd_input_update(input, 2, true);

    /* Simulate Frame */
    for (int i = 0; i < 7; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);

    /* Expect Toggle Bit SET (0x40) */
    expect_check(mock_phy_send, data, check_status_byte,
                 (void*) (uintptr_t) IOLINK_OD_STATUS_PD_TOGGLE);
    expect_value(mock_phy_send, len, 6);
    will_return(mock_phy_send, 0);
    iolink_process();

    /* Update 2: Valid=True. Flip 1->0. Toggle bit should be 0 (0x00). */
    iolink_pd_input_update(input, 2, true);

    /* Simulate Frame */
    for (int i = 0; i < 7; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);

    /* Expect Toggle Bit CLEARED (0x00) */
    expect_check(mock_phy_send, data, check_status_byte, (void*) (uintptr_t) 0x00);
    expect_value(mock_phy_send, len, 6);
    will_return(mock_phy_send, 0);
    iolink_process();

    /* Update 3: Valid=True. Flip 0->1. Toggle bit should be 0x40. */
    iolink_pd_input_update(input, 2, true);

    /* Simulate Frame */
    for (int i = 0; i < 7; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);

    /* Expect Toggle Bit SET (0x40) */
    expect_check(mock_phy_send, data, check_status_byte,
                 (void*) (uintptr_t) IOLINK_OD_STATUS_PD_TOGGLE);
    expect_value(mock_phy_send, len, 6);
    will_return(mock_phy_send, 0);
    iolink_process();
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_pd_toggle_bit),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
