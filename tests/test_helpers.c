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
#include "iolinki/crc.h"
#include "iolinki/protocol.h"

/* Test buffers */
uint8_t g_tx_buf[1024];
uint8_t g_rx_buf[1024];

/* Mock implementations */

int mock_phy_init(void)
{
    return (int)mock();
}

void mock_phy_set_mode(iolink_phy_mode_t mode)
{
    check_expected(mode);
}

void mock_phy_set_baudrate(iolink_baudrate_t baudrate)
{
    check_expected(baudrate);
}

int mock_phy_send(const uint8_t *data, size_t len)
{
    check_expected_ptr(data);
    check_expected(len);
    return (int)mock();
}

int mock_phy_recv_byte(uint8_t *byte)
{
    int res = (int)mock();
    if (res > 0) {
        *byte = (uint8_t)mock();
    }
    return res;
}

const iolink_phy_api_t g_phy_mock = {
    .init = mock_phy_init,
    .set_mode = mock_phy_set_mode,
    .set_baudrate = mock_phy_set_baudrate,
    .send = mock_phy_send,
    .recv_byte = mock_phy_recv_byte
};

void setup_mock_phy(void)
{
    /* Use -1 for infinite expectations to avoid errors on earlier test exit. */
    expect_any_count(mock_phy_set_mode, mode, -1);
    expect_any_count(mock_phy_set_baudrate, baudrate, -1);
    
    /* NO default will_return here. Tests must provide them. */
}

void move_to_operate(void)
{
    /* STARTUP -> PREOPERATE (on first byte) */
    will_return(mock_phy_recv_byte, 1);     /* res=1 */
    will_return(mock_phy_recv_byte, 0x00);  /* byte=0x00 */
    will_return(mock_phy_recv_byte, 0);     /* res=0 (end frame) */
    iolink_process();
    
    /* PREOPERATE -> OPERATE (on MC=0x0F + Correct CK) */
    uint8_t mc = IOLINK_MC_TRANSITION_COMMAND;
    uint8_t ck = iolink_checksum_ck(mc, 0U);
    
    will_return(mock_phy_recv_byte, 1); 
    will_return(mock_phy_recv_byte, mc);
    will_return(mock_phy_recv_byte, 1); 
    will_return(mock_phy_recv_byte, ck);
    will_return(mock_phy_recv_byte, 0); 
    iolink_process();
}

void iolink_phy_mock_reset(void)
{
}

/* Mock Storage for Data Storage (DS) testing */
#include "iolinki/data_storage.h"
#define DS_MOCK_SIZE 128
static uint8_t g_ds_mock_buf[DS_MOCK_SIZE];

int ds_mock_read(uint32_t addr, uint8_t *buf, size_t len)
{
    if (addr + len > DS_MOCK_SIZE) return -1;
    memcpy(buf, &g_ds_mock_buf[addr], len);
    return 0;
}

int ds_mock_write(uint32_t addr, const uint8_t *buf, size_t len)
{
    if (addr + len > DS_MOCK_SIZE) return -1;
    memcpy(&g_ds_mock_buf[addr], buf, len);
    return 0;
}

const iolink_ds_storage_api_t g_ds_storage_mock = {
    .read = ds_mock_read,
    .write = ds_mock_write,
    .erase = NULL
};

void iolink_ds_mock_reset(void)
{
    memset(g_ds_mock_buf, 0, DS_MOCK_SIZE);
}

uint8_t* iolink_ds_mock_get_buf(void)
{
    return g_ds_mock_buf;
}
