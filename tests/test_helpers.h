/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_helpers.h
 * @brief Shared test utilities and mock implementations
 */

#ifndef TEST_HELPERS_H_
#define TEST_HELPERS_H_

#include <stdint.h>
#include <stddef.h>
#include "iolinki/iolink.h"
#include "iolinki/phy.h"
#include "iolinki/crc.h"
#include "iolinki/protocol.h"

/* Test buffers */
extern uint8_t g_tx_buf[1024];
extern uint8_t g_rx_buf[1024];

/* Mock implementations (exported for CMocka symbols) */
int mock_phy_init(void);
void mock_phy_set_mode(iolink_phy_mode_t mode);
void mock_phy_set_baudrate(iolink_baudrate_t baudrate);
int mock_phy_send(const uint8_t *data, size_t len);
int mock_phy_recv_byte(uint8_t *byte);
int mock_phy_detect_wakeup(void);
void mock_phy_set_cq_line(uint8_t state);

/* Mock PHY driver API */
extern const iolink_phy_api_t g_phy_mock;

/* Helper to setup mock expectations */
void setup_mock_phy(void);
void iolink_phy_mock_reset(void);
void move_to_operate(void);
void iolink_phy_mock_set_wakeup(int detected);
uint8_t iolink_phy_mock_get_cq_state(void);
void iolink_phy_mock_set_send_delay_us(uint32_t delay_us);

/* Mock Storage for Data Storage (DS) testing */
#include "iolinki/data_storage.h"
extern const iolink_ds_storage_api_t g_ds_storage_mock;
void iolink_ds_mock_reset(void);
uint8_t* iolink_ds_mock_get_buf(void);

#endif  // TEST_HELPERS_H_
