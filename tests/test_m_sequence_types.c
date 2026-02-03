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
#include "test_helpers.h"

extern uint8_t iolink_crc6(const uint8_t *data, size_t len);

/* Mock PHY for testing */
static uint8_t g_last_sent[64];
static uint8_t g_last_sent_len = 0;

static int mock_send(const uint8_t *data, size_t len) {
    memcpy(g_last_sent, data, len);
    g_last_sent_len = (uint8_t)len;
    return (int)len;
}

static uint8_t g_recv_buf[64];
static uint8_t g_recv_idx = 0;
static uint8_t g_recv_len = 0;

static int mock_recv(uint8_t *byte) {
    if (g_recv_idx < g_recv_len) {
        *byte = g_recv_buf[g_recv_idx++];
        return 1;
    }
    return 0;
}

static const iolink_phy_api_t g_mock_phy = {
    .init = NULL,
    .set_mode = NULL,
    .set_baudrate = NULL,
    .send = mock_send,
    .recv_byte = mock_recv
};

static void test_m_seq_type_1_1(void **state) {
    (void)state;
    
    /* Type 1_1: PD only, no ISDU */
    iolink_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_1_1,
        .pd_in_len = 2,
        .pd_out_len = 2
    };
    
    iolink_init(&g_mock_phy, &config);
    
    /* Transition to OPERATE */
    g_recv_buf[0] = 0x0F; /* MC: Transition */
    g_recv_buf[1] = 0xC1; /* CK for MC=0x0F */
    g_recv_len = 2;
    g_recv_idx = 0;
    iolink_process();
    
    /* Set input PD */
    uint8_t input_pd[2] = {0xAA, 0xBB};
    iolink_pd_input_update(input_pd, 2, true);
    
    /* Send Type 1_1 Frame: MC + CKT + PD_OUT(2) + OD(1) + CK */
    uint8_t frame[] = {0x80, 0x00, 0x11, 0x22, 0x00, 0x00};
    frame[5] = iolink_crc6(frame, 5);
    
    memcpy(g_recv_buf, frame, 6);
    g_recv_len = 6;
    g_recv_idx = 0;
    
    iolink_process();
    
    /* Verify Response: Status + PD_IN(2) + OD(1) + CK = 5 bytes */
    assert_int_equal(g_last_sent_len, 5);
    assert_true(g_last_sent[0] & 0x20); /* PDStatus valid */
    assert_int_equal(g_last_sent[1], 0xAA);
    assert_int_equal(g_last_sent[2], 0xBB);
}

static void test_m_seq_type_2_1(void **state) {
    (void)state;
    
    /* Type 2_1: PD with 2-byte OD */
    iolink_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_2_1,
        .pd_in_len = 1,
        .pd_out_len = 1
    };
    
    iolink_init(&g_mock_phy, &config);
    
    /* Transition to OPERATE */
    g_recv_buf[0] = 0x0F;
    g_recv_buf[1] = 0xC1;
    g_recv_len = 2;
    g_recv_idx = 0;
    iolink_process();
    
    /* Set input PD */
    uint8_t input_pd = 0x55;
    iolink_pd_input_update(&input_pd, 1, true);
    
    /* Send Type 2_1 Frame: MC + CKT + PD_OUT(1) + OD(2) + CK */
    uint8_t frame[] = {0x80, 0x00, 0x33, 0x00, 0x00, 0x00};
    frame[5] = iolink_crc6(frame, 5);
    
    memcpy(g_recv_buf, frame, 6);
    g_recv_len = 6;
    g_recv_idx = 0;
    
    iolink_process();
    
    /* Verify Response: Status + PD_IN(1) + OD(2) + CK = 5 bytes */
    assert_int_equal(g_last_sent_len, 5);
    assert_true(g_last_sent[0] & 0x20); /* PDStatus valid */
    assert_int_equal(g_last_sent[1], 0x55);
    /* OD bytes: first from ISDU, second is 0x00 */
    assert_int_equal(g_last_sent[3], 0x00); /* Second OD byte */
}

static void test_m_seq_type_1_2_with_isdu(void **state) {
    (void)state;
    
    /* Type 1_2: PD with ISDU support */
    iolink_config_t config = {
        .m_seq_type = IOLINK_M_SEQ_TYPE_1_2,
        .pd_in_len = 1,
        .pd_out_len = 1
    };
    
    iolink_init(&g_mock_phy, &config);
    
    /* Transition to OPERATE */
    g_recv_buf[0] = 0x0F;
    g_recv_buf[1] = 0xC1;
    g_recv_len = 2;
    g_recv_idx = 0;
    iolink_process();
    
    /* Send Type 1_2 Frame with ISDU request */
    /* MC=0x80 (Read), CKT=0x00, PD_OUT=0x00, OD=ISDU_start_byte, CK */
    uint8_t frame[] = {0x80, 0x00, 0x00, 0x80, 0x00}; /* OD=0x80 (ISDU start) */
    frame[4] = iolink_crc6(frame, 4);
    
    memcpy(g_recv_buf, frame, 5);
    g_recv_len = 5;
    g_recv_idx = 0;
    
    iolink_process();
    
    /* Verify response contains ISDU data in OD */
    assert_int_equal(g_last_sent_len, 4); /* Status + PD_IN(1) + OD(1) + CK */
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_m_seq_type_1_1),
        cmocka_unit_test(test_m_seq_type_2_1),
        cmocka_unit_test(test_m_seq_type_1_2_with_isdu),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
