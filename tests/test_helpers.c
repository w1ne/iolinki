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

/* Test buffers */
uint8_t g_tx_buf[1024];
uint8_t g_rx_buf[1024];

/* Mock implementations */

static int mock_phy_init(void)
{
    return (int)mock();
}

static void mock_phy_set_mode(iolink_phy_mode_t mode)
{
    check_expected(mode);
}

static void mock_phy_set_baudrate(iolink_baudrate_t baudrate)
{
    check_expected(baudrate);
}

static int mock_phy_send(const uint8_t *data, size_t len)
{
    check_expected_ptr(data);
    check_expected(len);
    return (int)mock();
}

static int mock_phy_recv_byte(uint8_t *byte)
{
    check_expected_ptr(byte);
    *byte = (uint8_t)mock();
    return (int)mock();
}

const iolink_phy_api_t g_phy_mock = {
    .init = mock_phy_init,
    .set_mode = mock_phy_set_mode,
    .set_baudrate = mock_phy_set_baudrate,
    .send = mock_phy_send,
    .recv_byte = mock_phy_recv_byte
};

void iolink_phy_mock_reset(void)
{
    /* Placeholder - CMocka reset logic is usually handled by setup/teardown */
}

/* Mock Storage for Data Storage (DS) testing */
#include "iolinki/data_storage.h"
#define DS_MOCK_SIZE 128
static uint8_t g_ds_mock_buf[DS_MOCK_SIZE];

static int ds_mock_read(uint32_t addr, uint8_t *buf, size_t len)
{
    if (addr + len > DS_MOCK_SIZE) return -1;
    memcpy(buf, &g_ds_mock_buf[addr], len);
    return 0;
}

static int ds_mock_write(uint32_t addr, const uint8_t *buf, size_t len)
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
