/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_helpers.c
 * @brief Shared test utilities and mock implementations
 */

#include "test_helpers.h"
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "iolinki/crc.h"
#include "iolinki/protocol.h"

/* Test buffers */
uint8_t g_tx_buf[1024];
uint8_t g_rx_buf[1024];

/* Mock implementations */

static int g_mock_wakeup = 0;
static uint8_t g_mock_cq_state = 0U;
static uint32_t g_mock_send_delay_us = 0U;

int mock_phy_init(void* user)
{
    (void) user;
    return (int) mock();
}

void mock_phy_set_mode(void* user, iolink_phy_mode_t mode)
{
    (void) user;
    check_expected(mode);
}

void mock_phy_set_baudrate(void* user, iolink_baudrate_t baudrate)
{
    (void) user;
    check_expected(baudrate);
}

int mock_phy_send(void* user, const uint8_t* data, size_t len)
{
    (void) user;
    check_expected_ptr(data);
    check_expected(len);
    if (g_mock_send_delay_us > 0U) {
        usleep(g_mock_send_delay_us);
    }
    return (int) mock();
}

int mock_phy_recv_byte(void* user, uint8_t* byte)
{
    (void) user;
    int res = (int) mock();
    if (res > 0) {
        *byte = (uint8_t) mock();
    }
    return res;
}

int mock_phy_detect_wakeup(void* user)
{
    (void) user;
    int ret = g_mock_wakeup;
    g_mock_wakeup = 0;
    return ret;
}

void mock_phy_set_cq_line(void* user, uint8_t state)
{
    (void) user;
    g_mock_cq_state = state;
}

const iolink_phy_api_t g_phy_mock = {.user = NULL,
                                     .init = mock_phy_init,
                                     .set_mode = mock_phy_set_mode,
                                     .set_baudrate = mock_phy_set_baudrate,
                                     .send = mock_phy_send,
                                     .recv_byte = mock_phy_recv_byte,
                                     .detect_wakeup = mock_phy_detect_wakeup,
                                     .set_cq_line = mock_phy_set_cq_line,
                                     .get_voltage_mv = NULL,
                                     .is_short_circuit = NULL};

void setup_mock_phy(void)
{
    /* Use -1 for infinite expectations to avoid errors on earlier test exit. */
    expect_any_count(mock_phy_set_mode, mode, -1);
    expect_any_count(mock_phy_set_baudrate, baudrate, -1);

    /* NO default will_return here. Tests must provide them. */
    g_mock_wakeup = 0;
    g_mock_cq_state = 0U;
    g_mock_send_delay_us = 0U;
}

int iolink_test_device_init(iolink_test_device_t* dev, const iolink_config_t* stack,
                            const iolink_app_callbacks_t* callbacks)
{
    if (dev == NULL) {
        return -1;
    }

    (void) memset(dev, 0, sizeof(*dev));
    dev->cfg.phy = g_phy_mock;
    if (stack != NULL) {
        dev->cfg.stack = *stack;
    }
    else {
        dev->cfg.stack.m_seq_type = IOLINK_M_SEQ_TYPE_0;
        dev->cfg.stack.min_cycle_time = 0U;
    }
    dev->cfg.app_callbacks = callbacks;

    return iolink_device_init(&dev->ctx, &dev->cfg);
}

void move_to_operate_ctx(iolink_device_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }

    /* STARTUP -> PREOPERATE (via WakeUp -> AWAITING_COMM) */
    g_mock_wakeup = 1;
    iolink_device_process(ctx);

    /* Wait for T_DWU (assuming timing might be enforced) */
    usleep(200); /* > 54us T_DWU */

    /* PREOPERATE -> ESTAB_COM (on MC=0x0F + Correct CK) */
    uint8_t mc = IOLINK_MC_TRANSITION_COMMAND;
    uint8_t ck = test_frame_checksum(mc);

    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, mc);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, ck);
    will_return(mock_phy_recv_byte, 0);
    iolink_device_process(ctx);

    /* ESTAB_COM -> OPERATE (send first valid frame for configured type) */
    iolink_m_seq_type_t type = iolink_device_get_m_seq_type(ctx);
    uint8_t pd_out_len = iolink_device_get_pd_out_len(ctx);
    uint8_t pd_in_len = iolink_device_get_pd_in_len(ctx);
    uint8_t od_len = ((type == IOLINK_M_SEQ_TYPE_2_1) || (type == IOLINK_M_SEQ_TYPE_2_2) ||
                      (type == IOLINK_M_SEQ_TYPE_2_V))
                         ? 2U
                         : 1U;

    if (type == IOLINK_M_SEQ_TYPE_0) {
        uint8_t idle_mc = 0x00;
        uint8_t idle_ck = test_frame_checksum(idle_mc);
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, idle_mc);
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, idle_ck);
        will_return(mock_phy_recv_byte, 0);

        expect_any(mock_phy_send, data);
        expect_value(mock_phy_send, len, 2);
        will_return(mock_phy_send, 0);
        iolink_device_process(ctx);
        return;
    }

    uint8_t frame[64];
    memset(frame, 0, sizeof(frame));
    /* Figure A.2: master message is MC, CKT, data... with the A.1.6 checksum in
       the CKT octet (byte 1); there is no trailing checksum octet. */
    uint8_t frame_len = (uint8_t) (IOLINK_M_SEQ_HEADER_LEN + pd_out_len + od_len);
    frame[0] = 0x80;
    frame[1] = ((type == IOLINK_M_SEQ_TYPE_2_1) || (type == IOLINK_M_SEQ_TYPE_2_2) ||
                (type == IOLINK_M_SEQ_TYPE_2_V))
                   ? IOLINK_MSEQ_TYPE_2
                   : IOLINK_MSEQ_TYPE_1;
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, frame_len));

    for (uint8_t i = 0U; i < frame_len; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);

    uint8_t resp_len = (uint8_t) (pd_in_len + od_len + 1U);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, resp_len);
    will_return(mock_phy_send, 0);

    iolink_device_process(ctx);
}

void iolink_phy_mock_reset(void)
{
    g_mock_wakeup = 0;
    g_mock_cq_state = 0U;
    g_mock_send_delay_us = 0U;
}

void iolink_phy_mock_set_wakeup(int detected)
{
    g_mock_wakeup = detected;
}

uint8_t iolink_phy_mock_get_cq_state(void)
{
    return g_mock_cq_state;
}

void iolink_phy_mock_set_send_delay_us(uint32_t delay_us)
{
    g_mock_send_delay_us = delay_us;
}

/* Mock Storage for Data Storage (DS) testing */
#include "iolinki/data_storage.h"
#define DS_MOCK_SIZE 128
static uint8_t g_ds_mock_buf[DS_MOCK_SIZE];

int ds_mock_read(uint32_t addr, uint8_t* buf, size_t len)
{
    if (addr + len > DS_MOCK_SIZE) return -1;
    memcpy(buf, &g_ds_mock_buf[addr], len);
    return 0;
}

int ds_mock_write(uint32_t addr, const uint8_t* buf, size_t len)
{
    if (addr + len > DS_MOCK_SIZE) return -1;
    memcpy(&g_ds_mock_buf[addr], buf, len);
    return 0;
}

const iolink_ds_storage_api_t g_ds_storage_mock = {
    .read = ds_mock_read, .write = ds_mock_write, .erase = NULL};

void iolink_ds_mock_reset(void)
{
    memset(g_ds_mock_buf, 0, DS_MOCK_SIZE);
}

uint8_t* iolink_ds_mock_get_buf(void)
{
    return g_ds_mock_buf;
}

void iolink_nvm_mock_cleanup(void)
{
    remove("iolink_nvm.bin");
}

/* ISDU V1.1.5 Interleaved Format Helpers */

static uint8_t helper_chkpdu(const uint8_t* octets, size_t len)
{
    uint8_t c = 0U;
    for (size_t i = 0U; i < len; i++) {
        c ^= octets[i];
    }
    return c;
}

/** @brief Feed one complete ISDU request over the spec transport (C3). */
static void helper_send_isdu(iolink_isdu_ctx_t* ctx, uint8_t* buf, size_t total)
{
    buf[total - 1U] = 0x00U;
    buf[total - 1U] = helper_chkpdu(buf, total);
    iolink_isdu_od_write(ctx, IOLINK_FLOWCTRL_START, buf, (uint8_t) total);
}

int isdu_send_read_request(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex)
{
    uint8_t buf[8];
    size_t n = 0U;

    if (index > 0xFFU) {
        /* 16-bit Index and 8-bit Subindex (I-Service 0xB, Length 0x5). */
        buf[n++] = 0xB5U;
        buf[n++] = (uint8_t) (index >> 8);
        buf[n++] = (uint8_t) (index & 0xFFU);
        buf[n++] = subindex;
    }
    else if (subindex != 0U) {
        /* 8-bit Index and 8-bit Subindex (I-Service 0xA, Length 0x4). */
        buf[n++] = 0xA4U;
        buf[n++] = (uint8_t) index;
        buf[n++] = subindex;
    }
    else {
        /* 8-bit Index (I-Service 0x9, Length 0x3). */
        buf[n++] = 0x93U;
        buf[n++] = (uint8_t) index;
    }

    helper_send_isdu(ctx, buf, n + 1U);
    return 1;
}

int isdu_send_write_request(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex,
                            const uint8_t* data, uint8_t data_len)
{
    uint8_t buf[IOLINK_ISDU_BUFFER_SIZE];
    size_t n = 0U;

    /* Octet count without the ExtLength octet. */
    size_t base_total;
    if (index > 0xFFU) {
        base_total = (size_t) data_len + 1U /* I-Service */ + 2U /* index */ + 1U /* sub */ + 1U;
    }
    else if (subindex != 0U) {
        base_total = (size_t) data_len + 1U + 1U /* index */ + 1U /* sub */ + 1U;
    }
    else {
        base_total = (size_t) data_len + 1U + 1U /* index */ + 1U;
    }

    const bool ext = (base_total > 15U);
    const size_t total = ext ? (base_total + 1U) : base_total;
    const uint8_t base = (index > 0xFFU) ? 0x30U : ((subindex != 0U) ? 0x20U : 0x10U);
    buf[n++] = (uint8_t) (base | (ext ? 0x01U : (uint8_t) total));
    if (ext) {
        buf[n++] = (uint8_t) total;
    }
    if (index > 0xFFU) {
        buf[n++] = (uint8_t) (index >> 8);
        buf[n++] = (uint8_t) (index & 0xFFU);
        buf[n++] = subindex;
    }
    else {
        buf[n++] = (uint8_t) index;
        if (subindex != 0U) {
            buf[n++] = subindex;
        }
    }
    for (uint8_t i = 0U; i < data_len; i++) {
        buf[n++] = data[i];
    }

    helper_send_isdu(ctx, buf, total);
    return 1;
}

int isdu_collect_response(iolink_isdu_ctx_t* ctx, uint8_t* buffer, size_t buffer_size)
{
    size_t idx = 0;
    uint8_t byte;

    if (ctx == NULL || ctx->state != ISDU_STATE_RESPONSE_READY) return -1;

    while (idx < buffer_size && iolink_isdu_get_response_byte(ctx, &byte) > 0) {
        buffer[idx++] = byte;
    }

    return (int) idx;
}
