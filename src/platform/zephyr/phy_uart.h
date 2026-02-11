/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_PHY_UART_H
#define IOLINK_PHY_UART_H

#include "iolinki/phy.h"
#include <zephyr/device.h>

/**
 * @brief Initialize the UART-based PHY driver
 * @param uart_dev Zephyr device structure for the UART peripheral
 * @return 0 on success, negative on error
 */
int iolink_phy_uart_init(const struct device *uart_dev);

/**
 * @brief Get the PHY interface for the UART driver
 * @return Pointer to the PHY API structure
 */
const iolink_phy_api_t* iolink_phy_uart_get(void);

#endif /* IOLINK_PHY_UART_H */
