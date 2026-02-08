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

int mock_phy_init(void)
{
    return (int) mock();
}

void mock_phy_set_mode(iolink_phy_mode_t mode)
{
    check_expected(mode);
}

void mock_phy_set_baudrate(iolink_baudrate_t baudrate)
{
    check_expected(baudrate);
}

int mock_phy_send(const uint8_t* data, size_t len)
{
    check_expected_ptr(data);
    check_expected(len);
    if (g_mock_send_delay_us > 0U) {
        usleep(g_mock_send_delay_us);
    }
    return (int) mock();
}

int mock_phy_recv_byte(uint8_t* byte)
{
    int res = (int) mock();
    if (res > 0) {
        *byte = (uint8_t) mock();
    }
    return res;
}

int mock_phy_detect_wakeup(void)
{
    int ret = g_mock_wakeup;
    g_mock_wakeup = 0;
    return ret;
}

void mock_phy_set_cq_line(uint8_t state)
{
    g_mock_cq_state = state;
}

const iolink_phy_api_t g_phy_mock = {.init = mock_phy_init,
                                     .set_mode = mock_phy_set_mode,
                                     .set_baudrate = mock_phy_set_baudrate,
                                     .send = mock_phy_send,
                                     .recv_byte = mock_phy_recv_byte,
                                     .detect_wakeup = mock_phy_detect_wakeup,
                                     .set_cq_line = mock_phy_set_cq_line};

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

void move_to_operate(void)
{
    /* STARTUP -> PREOPERATE (via WakeUp -> AWAITING_COMM) */

    /* 1. Send WakeUp to switch to SDCI */
    g_mock_wakeup = 1; /* iolink_phy_mock_set_wakeup(1) */
    iolink_process();

    /* 2. Wait for T_DWU (assuming timing might be enforced) */
    usleep(200); /* > 54us T_DWU */

    /* 3. Send Transition Command immediately (AWAITING_COMM handles first byte)
       This avoids needing to handle the response from an Idle frame. */

    /* PREOPERATE -> ESTAB_COM (on MC=0x0F + Correct CK) */
    uint8_t mc = IOLINK_MC_TRANSITION_COMMAND;
    uint8_t ck = iolink_checksum_ck(mc, 0U);

    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, mc);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, ck);
    will_return(mock_phy_recv_byte, 0);
    iolink_process();

    /* ESTAB_COM -> OPERATE (send first valid frame for configured type) */
    iolink_m_seq_type_t type = iolink_get_m_seq_type();
    uint8_t pd_out_len = iolink_get_pd_out_len();
    uint8_t pd_in_len = iolink_get_pd_in_len();
    uint8_t od_len = ((type == IOLINK_M_SEQ_TYPE_2_1) || (type == IOLINK_M_SEQ_TYPE_2_2) ||
                      (type == IOLINK_M_SEQ_TYPE_2_V))
                         ? 2U
                         : 1U;

    if (type == IOLINK_M_SEQ_TYPE_0) {
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
        return;
    }

    uint8_t frame[64];
    memset(frame, 0, sizeof(frame));
    uint8_t frame_len = (uint8_t) (IOLINK_M_SEQ_HEADER_LEN + pd_out_len + od_len + 1U);
    frame[0] = 0x80;
    frame[1] = 0x00;
    frame[frame_len - 1U] = iolink_crc6(frame, (uint8_t) (frame_len - 1U));

    for (uint8_t i = 0U; i < frame_len; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);

    uint8_t resp_len = (uint8_t) (1U + pd_in_len + od_len + 1U);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, resp_len);
    will_return(mock_phy_send, 0);

    iolink_process();
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

int isdu_send_read_request(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex)
{
    int ret;

    /* Control: Start, Seq=0 (Not Last) */
    ret = iolink_isdu_collect_byte(ctx, 0x80);
    if (ret != 0) return ret;

    /* Data: Read service (0x80 = Read, Length=0) */
    ret = iolink_isdu_collect_byte(ctx, 0x80);
    if (ret != 0) return ret;

    /* Control: Seq=1 */
    ret = iolink_isdu_collect_byte(ctx, 0x01);
    if (ret != 0) return ret;

    /* Data: Index high byte */
    ret = iolink_isdu_collect_byte(ctx, (uint8_t) (index >> 8));
    if (ret != 0) return ret;

    /* Control: Seq=2 */
    ret = iolink_isdu_collect_byte(ctx, 0x02);
    if (ret != 0) return ret;

    /* Data: Index low byte */
    ret = iolink_isdu_collect_byte(ctx, (uint8_t) (index & 0xFF));
    if (ret != 0) return ret;

    /* Control: Last, Seq=3 */
    ret = iolink_isdu_collect_byte(ctx, 0x43);
    if (ret != 0) return ret;

    /* Data: Subindex (last byte) */
    ret = iolink_isdu_collect_byte(ctx, subindex);
    return ret;
}

int isdu_send_write_request(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex,
                            const uint8_t* data, uint8_t data_len)
{
    int ret;
    uint8_t seq = 0;

    /* Control: Start, Seq=0 */
    uint8_t ctrl = 0x80; /* Start bit */
    /* Note: For Write, we always have Index/Subindex following, so first byte is never Last */
    ret = iolink_isdu_collect_byte(ctx, ctrl);
    if (ret != 0) return ret;
    seq++;

    /* Data: Write service */
    uint8_t service_byte;
    if (data_len <= 15) {
        service_byte = 0x90 | data_len; /* Write, embedded length */
    }
    else {
        service_byte = 0x9F; /* Write, extended length */
    }
    ret = iolink_isdu_collect_byte(ctx, service_byte);
    if (ret != 0) return ret;

    /* If extended length, send it */
    if (data_len > 15) {
        /* Control */
        ret = iolink_isdu_collect_byte(ctx, seq & 0x3F);
        if (ret != 0) return ret;
        seq++;

        /* Data: Extended length */
        ret = iolink_isdu_collect_byte(ctx, data_len);
        if (ret != 0) return ret;
    }

    /* Control */
    ret = iolink_isdu_collect_byte(ctx, seq & 0x3F);
    if (ret != 0) return ret;
    seq++;

    /* Data: Index high */
    ret = iolink_isdu_collect_byte(ctx, (uint8_t) (index >> 8));
    if (ret != 0) return ret;

    /* Control */
    ret = iolink_isdu_collect_byte(ctx, seq & 0x3F);
    if (ret != 0) return ret;
    seq++;

    /* Data: Index low */
    ret = iolink_isdu_collect_byte(ctx, (uint8_t) (index & 0xFF));
    if (ret != 0) return ret;

    /* Control */
    ret = iolink_isdu_collect_byte(ctx, seq & 0x3F);
    if (ret != 0) return ret;
    seq++;

    /* Data: Subindex */
    ret = iolink_isdu_collect_byte(ctx, subindex);
    if (ret != 0) return ret;

    /* Send data bytes */
    for (uint8_t i = 0; i < data_len; i++) {
        /* Control */
        uint8_t c = seq & 0x3F;
        if (i == data_len - 1) c |= 0x40; /* Last bit */
        ret = iolink_isdu_collect_byte(ctx, c);
        if (ret != 0) return ret;
        seq = (seq + 1) & 0x3F;

        /* Data */
        ret = iolink_isdu_collect_byte(ctx, data[i]);
        if (ret != 0) return ret;
    }

    return ret;
}

int isdu_collect_response(iolink_isdu_ctx_t* ctx, uint8_t* buffer, size_t buffer_size)
{
    size_t idx = 0;
    uint8_t byte;

    if (ctx == NULL || ctx->state != ISDU_STATE_RESPONSE_READY) return -1;

    /* Collect alternating Control and Data bytes */
    while (idx < buffer_size && ctx->state == ISDU_STATE_RESPONSE_READY) {
        /* Get control byte */
        if (iolink_isdu_get_response_byte(ctx, &byte) <= 0) {
            break;
        }

        /* Get data byte */
        if (iolink_isdu_get_response_byte(ctx, &byte) <= 0) {
            /* If this was a 0-length response, we might get 0 here after its only control byte.
             * But for data-carrying responses, this should return the data byte. */
            break;
        }

        buffer[idx++] = byte;
    }

    return (int) idx;
}
