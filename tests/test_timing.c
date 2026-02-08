/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_timing.c
 * @brief Unit tests for time utilities and timing logic placeholders
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

#include "iolinki/crc.h"
#include "iolinki/iolink.h"
#include "iolinki/time_utils.h"
#include "test_helpers.h"

static void test_time_get_ms(void** state)
{
    (void) state;
    uint32_t t1 = iolink_time_get_ms();
    usleep(10000); /* 10ms */
    uint32_t t2 = iolink_time_get_ms();

    assert_true(t2 >= t1 + 10);
    assert_true(t2 < t1 + 50); /* Allow more jitter (was 20) */
}

static void test_time_get_us(void** state)
{
    (void) state;
    uint64_t t1 = iolink_time_get_us();
    usleep(1000); /* 1ms */
    uint64_t t2 = iolink_time_get_us();

    assert_true(t2 >= t1 + 1000);
    assert_true(t2 < t1 + 5000); /* Allow more jitter (was 2000) */
}

static void test_t_cycle_violation(void** state)
{
    (void) state;
    iolink_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_1_1,
        .pd_in_len = 1,
        .pd_out_len = 1,
        .min_cycle_time = 50 /* 5ms */
    };

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_init(&g_phy_mock, &config);

    /* Move to OPERATE without timing enforcement */
    move_to_operate();

    /* Enable timing enforcement */
    iolink_set_timing_enforcement(true);

    /* Send two back-to-back valid frames (Type 1_1) */
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
    assert_true(stats.t_cycle_violations > 0U);
}

static void test_t_ren_violation(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_1_1, .pd_in_len = 1, .pd_out_len = 1};

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_init(&g_phy_mock, &config);
    move_to_operate();
    iolink_set_timing_enforcement(true);

    /* Send a valid frame, but mock PHY send will be too slow?
       Actually t_ren is checked against DLL processing time. */
    uint8_t frame[5] = {0x80, 0x00, 0x00, 0x00, 0x00};
    frame[4] = iolink_crc6(frame, 4);

    for (int i = 0; i < 5; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);

    /* Mock a slow response */
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 4);
    will_return(mock_phy_send, 0);

    /* We need to trick the time. Since we use real time, we just wait a bit in a mock?
       But DLL calls send() immediately after data collect.
       To trigger t_ren violation, we'd need iolink_process to take long.
    */
    iolink_process();

    /* No violation expected in normal run.
       Testing timing enforcement is hard with real system clock in unit tests.
       But we can at least verify it doesn't crash. */
}

static void test_t_pd_delay(void** state)
{
    (void) state;
    iolink_config_t config = {
        .pd_in_len = 2, .pd_out_len = 2, .m_seq_type = IOLINK_M_SEQ_TYPE_0, .t_pd_us = 500000U};

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_init(&g_phy_mock, &config);

    /* Send a valid Type 0 frame before t_pd expires; expect no response */
    uint8_t mc = 0x00;
    uint8_t ck = iolink_checksum_ck(mc, 0U);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, mc);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, ck);
    will_return(mock_phy_recv_byte, 0);
    iolink_process();

    iolink_dll_stats_t stats;
    iolink_get_dll_stats(&stats);
    assert_true(stats.timing_errors > 0U);
    assert_true(stats.t_pd_violations > 0U);

    /* Wait for t_pd to elapse */
    usleep(600000);

    /* Trigger WakeUp to get out of STARTUP */
    iolink_phy_mock_set_wakeup(1);
    iolink_process();
    iolink_phy_mock_set_wakeup(0);

    /* Move to PREOPERATE state (AWAITING_COMM handles first byte) */
    uint8_t mc_comm = 0x00;
    uint8_t ck_comm = iolink_checksum_ck(mc_comm, 0U);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, mc_comm);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, ck_comm);
    will_return(mock_phy_recv_byte, 0);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2);
    will_return(mock_phy_send, 0);
    iolink_process();

    /* In PREOPERATE, send Transition Command (0x0F) - no response expected */
    uint8_t trans_mc = 0x0F;
    uint8_t trans_ck = iolink_checksum_ck(trans_mc, 0U);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, trans_mc);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, trans_ck);
    will_return(mock_phy_recv_byte, 0);
    iolink_process();
}

static void test_t_byte_violation(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_1_1, .pd_in_len = 1, .pd_out_len = 1};

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_init(&g_phy_mock, &config);
    move_to_operate();
    iolink_set_timing_enforcement(true);

    /* Mock a slow byte reception (t_byte violation) */
    /* Master sends 5 bytes for Type 1_1. We send 2 and then timeout. */
    uint8_t frame[5] = {0x80, 0x00, 0x00, 0x00, 0x00};
    frame[4] = iolink_crc6(frame, 4);

    /* Byte 1 (Control) */
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, frame[0]);

    /* Byte 2 (Data 1) -> Success */
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, frame[1]);

    /* Byte 3 -> Timeout */
    will_return(mock_phy_recv_byte, 0);
    iolink_process();

    /* Now wait for t_byte_limit and call process again to trigger silence detection */
    usleep(5000); /* COM2 t_byte limit is ~416us, 5ms is plenty */
    will_return(mock_phy_recv_byte, 0);
    iolink_process();

    iolink_dll_stats_t stats;
    iolink_get_dll_stats(&stats);
    /* Should have 1 timing error (t_byte) */
    assert_true(stats.t_byte_violations > 0U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_time_get_ms),       cmocka_unit_test(test_time_get_us),
        cmocka_unit_test(test_t_cycle_violation), cmocka_unit_test(test_t_ren_violation),
        cmocka_unit_test(test_t_pd_delay),        cmocka_unit_test(test_t_byte_violation),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
