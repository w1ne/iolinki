/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_dll.c
 * @brief Unit tests for Data Link Layer (DLL) state machine
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "iolinki/iolink.h"
#include "iolinki/dll.h"
#include "iolinki/crc.h"
#include "test_helpers.h"

static void test_dll_wakeup_to_preoperate(void** state)
{
    (void) state;
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_init(&g_phy_mock, NULL);
    iolink_set_timing_enforcement(true);

    /* Trigger wake-up */
    iolink_phy_mock_set_wakeup(1);
    iolink_process();
    assert_int_equal(iolink_get_state(), IOLINK_DLL_STATE_AWAITING_COMM);

    /* Wait for t_dwu to expire */
    usleep(200);

    /* Send valid Type 0 frame (idle) to enter PREOPERATE */
    uint8_t mc = 0x00;
    uint8_t ck = iolink_checksum_ck(mc, 0U);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, mc);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, ck);
    will_return(mock_phy_recv_byte, 0);

    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 4);
    will_return(mock_phy_send, 0);

    iolink_process();
    assert_int_equal(iolink_get_state(), IOLINK_DLL_STATE_PREOPERATE);
}

static void test_dll_preoperate_to_operate(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_1_1, .pd_in_len = 1, .pd_out_len = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_init(&g_phy_mock, &config);
    iolink_set_timing_enforcement(true);

    /* Wake-up */
    iolink_phy_mock_set_wakeup(1);
    iolink_process();
    usleep(200);

    /* PREOPERATE -> ESTAB_COM */
    uint8_t mc = IOLINK_MC_TRANSITION_COMMAND;
    uint8_t ck = iolink_checksum_ck(mc, 0U);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, mc);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, ck);
    will_return(mock_phy_recv_byte, 0);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 4);
    will_return(mock_phy_send, 0);
    iolink_process();
    assert_int_equal(iolink_get_state(), IOLINK_DLL_STATE_ESTAB_COM);

    /* ESTAB_COM -> OPERATE on first valid frame */
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
    assert_int_equal(iolink_get_state(), IOLINK_DLL_STATE_OPERATE);
}

static void test_dll_fallback_on_crc_errors(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_1_1, .pd_in_len = 1, .pd_out_len = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_init(&g_phy_mock, &config);
    iolink_set_timing_enforcement(true);

    /* Wake-up */
    iolink_phy_mock_set_wakeup(1);
    iolink_process();
    usleep(200);

    /* PREOPERATE -> ESTAB_COM */
    uint8_t mc = IOLINK_MC_TRANSITION_COMMAND;
    uint8_t ck = iolink_checksum_ck(mc, 0U);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, mc);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, ck);
    will_return(mock_phy_recv_byte, 0);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 4);
    will_return(mock_phy_send, 0);
    iolink_process();

    /* ESTAB_COM -> OPERATE */
    uint8_t ok_frame[5] = {0x80, 0x00, 0x00, 0x00, 0x00};
    ok_frame[4] = iolink_crc6(ok_frame, 4);
    for (int i = 0; i < 5; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, ok_frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 4);
    will_return(mock_phy_send, 0);
    iolink_process();
    assert_int_equal(iolink_get_state(), IOLINK_DLL_STATE_OPERATE);

    /* Inject CRC errors to trigger fallback */
    for (int r = 0; r < 3; r++) {
        uint8_t bad_frame[5] = {0x80, 0x00, 0x00, 0x00, 0xFF};
        for (int i = 0; i < 5; i++) {
            will_return(mock_phy_recv_byte, 1);
            will_return(mock_phy_recv_byte, bad_frame[i]);
        }
        will_return(mock_phy_recv_byte, 0);
        iolink_process();
    }

    /* Ensure there is exactly one mock value for the final process cycle check */
    /* will_return(mock_phy_recv_byte, 0); // Removed: in SIO mode we don't call recv_byte */

    /* Next process call applies fallback */
    iolink_process();

    assert_int_equal(iolink_get_state(), IOLINK_DLL_STATE_STARTUP);
    assert_int_equal(iolink_get_baudrate(), IOLINK_BAUDRATE_COM1);
}

static void test_dll_reject_transition_in_operate(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_1_1, .pd_in_len = 1, .pd_out_len = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_init(&g_phy_mock, &config);

    move_to_operate();
    assert_int_equal(iolink_get_state(), IOLINK_DLL_STATE_OPERATE);

    /* Master sends 0x0F (Transition) while in OPERATE
     * Type 1_1 frame for pd_out_len=1 is 5 bytes: MC, CKT, PD, OD, CK */
    uint8_t mc = IOLINK_MC_TRANSITION_COMMAND;
    uint8_t frame[5] = {mc, 0x00, 0x00, 0x00, 0x00};
    frame[4] = iolink_crc6(frame, 4);

    for (int i = 0; i < 5; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);

    iolink_process();

    /* Should NOT change state and should increment framing_errors */
    assert_int_equal(iolink_get_state(), IOLINK_DLL_STATE_OPERATE);
    iolink_dll_stats_t stats;
    iolink_get_dll_stats(&stats);
    assert_int_not_equal(stats.framing_errors, 0);
}

static void test_dll_reject_invalid_mc_channel(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_1_1, .pd_in_len = 1, .pd_out_len = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_init(&g_phy_mock, &config);

    move_to_operate();

    /* MC with reserved channel bits (e.g., 0x20 | 0x80) */
    uint8_t mc = 0xA0;
    uint8_t frame[5] = {mc, 0x00, 0x00, 0x00, 0x00};
    frame[4] = iolink_crc6(frame, 4);

    for (int i = 0; i < 5; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);

    iolink_process();

    iolink_dll_stats_t stats;
    iolink_get_dll_stats(&stats);
    assert_int_not_equal(stats.framing_errors, 0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_dll_wakeup_to_preoperate),
        cmocka_unit_test(test_dll_preoperate_to_operate),
        cmocka_unit_test(test_dll_fallback_on_crc_errors),
        cmocka_unit_test(test_dll_reject_transition_in_operate),
        cmocka_unit_test(test_dll_reject_invalid_mc_channel),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
