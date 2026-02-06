/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include "iolinki/iolink.h"
#include "iolinki/dll.h"
#include "iolinki/phy.h"
#include "iolinki/crc.h"
#include "test_helpers.h"

static void test_sio_fallback_on_repeated_errors(void** state)
{
    (void) state;

    iolink_config_t config = {.pd_in_len = 0, .pd_out_len = 0, .m_seq_type = IOLINK_M_SEQ_TYPE_0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_init(&g_phy_mock, &config);

    /* Verify we're in SIO mode initially (default new behavior) */
    assert_int_equal(iolink_get_phy_mode(), IOLINK_PHY_MODE_SIO);

    /* Move to OPERATE */
    move_to_operate();

    /* Verify we're in SDCI mode */
    assert_int_equal(iolink_get_phy_mode(), IOLINK_PHY_MODE_SDCI);

    /* Inject CRC errors to trigger fallback threshold (3 is the stack's threshold) */
    for (int i = 0; i < 3; i++) {
        /* Send frame with bad CRC */
        uint8_t bad_frame[2] = {0x95, 0xFF}; /* Invalid CRC */

        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, bad_frame[0]);
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, bad_frame[1]);
        will_return(mock_phy_recv_byte, 0);

        iolink_process();
    }

    /* After 3 fallbacks, should be in SIO mode */
    assert_int_equal(iolink_get_phy_mode(), IOLINK_PHY_MODE_SIO);
}

static void test_sio_recovery_on_stable_communication(void** state)
{
    (void) state;

    iolink_config_t config = {.pd_in_len = 0, .pd_out_len = 0, .m_seq_type = IOLINK_M_SEQ_TYPE_0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_init(&g_phy_mock, &config);

    /* Move to OPERATE */
    move_to_operate();

    /* Trigger SIO fallback by injecting errors (3 is the threshold) */
    for (int i = 0; i < 3; i++) {
        uint8_t bad_frame[2] = {0x95, 0xFF};
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, bad_frame[0]);
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, bad_frame[1]);
        will_return(mock_phy_recv_byte, 0);
        iolink_process();
    }

    assert_int_equal(iolink_get_phy_mode(), IOLINK_PHY_MODE_SIO);

    /* Now send valid frames to recover */

    /* 1. WakeUp (SIO -> AWAITING_COMM) */
    iolink_phy_mock_set_wakeup(1);
    iolink_process();
    usleep(200);

    /* 2. Transition (AWAITING_COMM handles first byte) */
    uint8_t mc = 0x0F;
    uint8_t ck = iolink_checksum_ck(mc, 0U);

    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, mc);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, ck);
    will_return(mock_phy_recv_byte, 0);

    iolink_process();

    /* 3. Send valid OPERATE frame */
    uint8_t idle_mc = 0x00;
    uint8_t idle_ck = iolink_checksum_ck(idle_mc, 0U);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, idle_mc);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, idle_ck);
    will_return(mock_phy_recv_byte, 0);

    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2);
    will_return(mock_phy_send, 0);
    iolink_process();

    /* Should recover back to SDCI */
    assert_int_equal(iolink_get_phy_mode(), IOLINK_PHY_MODE_SDCI);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_sio_fallback_on_repeated_errors),
        cmocka_unit_test(test_sio_recovery_on_stable_communication),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
