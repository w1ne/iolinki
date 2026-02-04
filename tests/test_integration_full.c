/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_integration_full.c
 * @brief Full-stack integration test for iolinki
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "iolinki/iolink.h"
#include "iolinki/dll.h"
#include "iolinki/isdu.h"
#include "iolinki/events.h"
#include "iolinki/application.h"
#include "iolinki/data_storage.h"
#include "iolinki/crc.h"
#include "test_helpers.h"

static void test_full_stack_lifecycle(void **state)
{
    (void) state;
    iolink_phy_mock_reset();
    iolink_ds_mock_reset();

    /* Initialize stack with mock PHY and mock storage */
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_init(&g_phy_mock, NULL);
    iolink_ds_init(iolink_get_ds_ctx(), &g_ds_storage_mock);

    /*** STEP 1: STARTUP -> PREOPERATE ***/
    will_return(mock_phy_recv_byte, 1);    /* res=1 */
    will_return(mock_phy_recv_byte, 0x00); /* byte=0x00 (Wakeup) */
    will_return(mock_phy_recv_byte, 0);    /* res=0 (end frame) */
    iolink_process();

    /*** STEP 2: PREOPERATE (ISDU Read Index 0x10 - Vendor Name) ***/
    /* Master Sends: MC=0xBB (Read Index 0x10) + CK */
    uint8_t mc = 0xBB;
    uint8_t ck = iolink_checksum_ck(mc, 0);

    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, mc);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, ck);
    will_return(mock_phy_recv_byte, 0);

    /* Device Responds: OD + CK = 2 bytes */
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2);
    will_return(mock_phy_send, 0);
    iolink_process();

    /*** STEP 3: EVENT TRIGGERING ***/
    iolink_events_ctx_t *evt_ctx = iolink_get_events_ctx();
    iolink_event_trigger(evt_ctx, 0x1234, IOLINK_EVENT_TYPE_WARNING);
    assert_true(iolink_events_pending(evt_ctx));

    /* Next cycle: Master sends Idle MC=0x00, CK=0x00 */
    uint8_t idle_mc = 0x00;
    uint8_t idle_ck = iolink_checksum_ck(idle_mc, 0);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, idle_mc);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, idle_ck);
    will_return(mock_phy_recv_byte, 0);

    /* Device sends response with Event bit set */
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2);
    will_return(mock_phy_send, 0);
    iolink_process();

    assert_true(iolink_events_pending(evt_ctx));
}

static void test_full_stack_timing_enforcement(void **state)
{
    (void) state;
    iolink_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_1_1, .pd_in_len = 1, .pd_out_len = 1, .min_cycle_time = 50};

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_init(&g_phy_mock, &config);

    move_to_operate();

    iolink_set_timing_enforcement(true);
    iolink_set_t_ren_limit_us(100);
    iolink_phy_mock_set_send_delay_us(500);

    uint8_t frame[5] = {0x80, 0x00, 0x00, 0x00, 0x00};
    frame[4] = iolink_crc6(frame, 4);

    for (int i = 0; i < 5; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 4);
    will_return(mock_phy_send, 0);
    iolink_process();

    for (int i = 0; i < 5; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 4);
    will_return(mock_phy_send, 0);
    iolink_process();

    iolink_dll_stats_t stats;
    iolink_get_dll_stats(&stats);
    assert_true(stats.t_ren_violations > 0U);
    assert_true(stats.t_cycle_violations > 0U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_full_stack_lifecycle),
        cmocka_unit_test(test_full_stack_timing_enforcement),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
