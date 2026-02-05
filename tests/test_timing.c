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
#include <unistd.h>

#include "iolinki/time_utils.h"
#include "iolinki/iolink.h"
#include "iolinki/crc.h"
#include "test_helpers.h"

static void test_time_get_ms(void **state)
{
    (void) state;
    uint32_t t1 = iolink_time_get_ms();
    usleep(10000); /* 10ms */
    uint32_t t2 = iolink_time_get_ms();

    assert_true(t2 >= t1 + 10);
    assert_true(t2 < t1 + 20); /* Allow some jitter */
}

static void test_time_get_us(void **state)
{
    (void) state;
    uint64_t t1 = iolink_time_get_us();
    usleep(1000); /* 1ms */
    uint64_t t2 = iolink_time_get_us();

    assert_true(t2 >= t1 + 1000);
    assert_true(t2 < t1 + 2000);
}

static void test_t_cycle_violation(void **state)
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

static void test_t_ren_violation(void **state)
{
    (void) state;
    iolink_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_1_1, .pd_in_len = 1, .pd_out_len = 1, .min_cycle_time = 0};

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_init(&g_phy_mock, &config);

    move_to_operate();

    /* Enable timing and force a low t_ren limit */
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

    iolink_dll_stats_t stats;
    iolink_get_dll_stats(&stats);
    assert_true(stats.t_ren_violations > 0U);
}

static void test_t_pd_delay(void **state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_0,
                              .pd_in_len = 0,
                              .pd_out_len = 0,
                              .min_cycle_time = 0,
                              .t_pd_us = 50000U};

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
    usleep(60000);

    /* Move to PREOPERATE (STARTUP consumes first byte) */
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, 0x00);
    will_return(mock_phy_recv_byte, 0);
    iolink_process();

    /* Now send a valid Type 0 frame and expect response */
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, mc);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, ck);
    will_return(mock_phy_recv_byte, 0);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2);
    will_return(mock_phy_send, 0);
    iolink_process();
}

static void test_t_byte_violation(void **state)
{
    (void) state;
    iolink_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_1_1, 
        .pd_in_len = 1, 
        .pd_out_len = 1, 
        .min_cycle_time = 0
    };

    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_init(&g_phy_mock, &config);

    move_to_operate();

    /* Enable timing enforcement */
    iolink_set_timing_enforcement(true);

    /* Send a frame byte-by-byte with excessive delay between bytes */
    uint8_t frame[5] = {0x80, 0x00, 0x00, 0x00, 0x00};
    frame[4] = iolink_crc6(frame, 4);

    /* Send first byte */
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, frame[0]);
    will_return(mock_phy_recv_byte, 0);
    iolink_process();

    /* Simulate excessive inter-byte delay (1ms = 1000 us) */
    /* This is much larger than t_byte_limit for COM2 (26us * 16 = 416us) */
    usleep(1000); /* 1ms delay - should trigger violation */

    /* Send second byte - should trigger inter-byte timeout */
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, frame[1]);
    will_return(mock_phy_recv_byte, 0);
    iolink_process();

    /* Check that inter-byte violation was detected */
    iolink_dll_stats_t stats;
    iolink_get_dll_stats(&stats);
    assert_true(stats.t_byte_violations > 0U);
    assert_true(stats.timing_errors > 0U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_time_get_ms),
        cmocka_unit_test(test_time_get_us),
        cmocka_unit_test(test_t_cycle_violation),
        cmocka_unit_test(test_t_ren_violation),
        cmocka_unit_test(test_t_pd_delay),
        cmocka_unit_test(test_t_byte_violation),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
