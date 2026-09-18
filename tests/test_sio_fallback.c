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

#include "iolinki/device.h"
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
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    /* Verify we're in SIO mode initially (default new behavior) */
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SIO);

    /* Move to OPERATE */
    move_to_operate_ctx(&dev.ctx);

    /* Verify we're in SDCI mode */
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SDCI);

    /* Inject CRC errors to trigger fallback threshold (3 is the stack's threshold) */
    for (int i = 0; i < 3; i++) {
        /* Send frame with bad CRC */
        uint8_t bad_frame[2] = {0x95, 0xFF}; /* Invalid CRC */

        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, bad_frame[0]);
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, bad_frame[1]);
        will_return(mock_phy_recv_byte, 0);

        iolink_device_process(&dev.ctx);
    }

    /* After 3 fallbacks, should be in SIO mode */
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SIO);
}

static void test_sio_recovery_on_stable_communication(void** state)
{
    (void) state;

    iolink_config_t config = {.pd_in_len = 0, .pd_out_len = 0, .m_seq_type = IOLINK_M_SEQ_TYPE_0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    /* Move to OPERATE */
    move_to_operate_ctx(&dev.ctx);

    /* Trigger SIO fallback by injecting errors (3 is the threshold) */
    for (int i = 0; i < 3; i++) {
        uint8_t bad_frame[2] = {0x95, 0xFF};
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, bad_frame[0]);
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, bad_frame[1]);
        will_return(mock_phy_recv_byte, 0);
        iolink_device_process(&dev.ctx);
    }

    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SIO);

    /* Now send valid frames to recover */

    /* 1. WakeUp (SIO -> AWAITING_COMM) */
    iolink_phy_mock_set_wakeup(1);
    iolink_device_process(&dev.ctx);
    usleep(200);

    /* 2. Transition (AWAITING_COMM handles first byte) */
    uint8_t mc = 0x0F;
    uint8_t ck = test_frame_checksum(mc);

    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, mc);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, ck);
    will_return(mock_phy_recv_byte, 0);

    iolink_device_process(&dev.ctx);

    /* 3. Send valid OPERATE frame */
    uint8_t idle_mc = 0x00;
    uint8_t idle_ck = test_frame_checksum(idle_mc);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, idle_mc);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, idle_ck);
    will_return(mock_phy_recv_byte, 0);

    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1); /* Type-0 write replies CKS only */
    will_return(mock_phy_send, 0);
    iolink_device_process(&dev.ctx);

    /* Should recover back to SDCI */
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SDCI);
}

static void test_t_dsio_returns_to_sio_without_message(void** state)
{
    (void) state;

    iolink_config_t config = {.pd_in_len = 0, .pd_out_len = 0, .m_seq_type = IOLINK_M_SEQ_TYPE_0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    /* A wake-up is seen: the device leaves SIO and waits for a message. */
    iolink_phy_mock_set_wakeup(1);
    iolink_device_process(&dev.ctx);
    iolink_phy_mock_set_wakeup(0);
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SDCI);

    /* No valid message arrives within T_DSIO (Table 47 T10): back to SIO. */
    usleep((IOLINK_T_DSIO_MS + 50U) * 1000U);
    iolink_device_process(&dev.ctx);

    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SIO);
}

static void test_fallback_command_switches_to_sio_after_t_fbd(void** state)
{
    (void) state;

    /* min_cycle_time is in 0.1 ms units: 10 -> 1 ms -> T_FBD = 3 ms. */
    iolink_config_t config = {
        .pd_in_len = 0, .pd_out_len = 0, .m_seq_type = IOLINK_M_SEQ_TYPE_0, .min_cycle_time = 10U};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    move_to_operate_ctx(&dev.ctx);
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SDCI);

    /* Page-channel Type-0 write of MasterCommand FALLBACK (0x5A, Table B.2). */
    uint8_t frame[3] = {0x20U, 0x00U, IOLINK_CMD_FALLBACK};
    frame[1] = iolink_checksum6(frame, 3U);
    for (size_t i = 0U; i < sizeof(frame); i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1); /* Type-0 write replies CKS only */
    will_return(mock_phy_send, 0);
    iolink_device_process(&dev.ctx);

    /* Still in SDCI until T_FBD has elapsed (device waits 3 MasterCycleTimes). */
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SDCI);
    usleep(10U * 1000U);
    iolink_device_process(&dev.ctx);
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_SIO);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_sio_fallback_on_repeated_errors),
        cmocka_unit_test(test_sio_recovery_on_stable_communication),
        cmocka_unit_test(test_t_dsio_returns_to_sio_without_message),
        cmocka_unit_test(test_fallback_command_switches_to_sio_after_t_fbd),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
