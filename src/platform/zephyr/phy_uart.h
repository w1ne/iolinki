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
 * @file phy_uart.h
 * @brief Zephyr UART-backed IO-Link PHY driver.
 *
 * Devicetree contract:
 *  - The UART peripheral is selected through the "zephyr,iolink-uart" chosen
 *    node. An "iolink-uart" alias is accepted as a fallback when the chosen
 *    node is absent.
 *  - The selected UART node must be "okay" and should set "current-speed" to
 *    match the active IO-Link COM baudrate (e.g. 38400 for COM2).
 *
 * The driver uses Zephyr's interrupt-driven UART API and stores received bytes
 * in a ring buffer. Transmission is performed with uart_poll_out().
 *
 * The UART PHY targets links where COM mode is already established by an
 * IO-Link transceiver front-end. Wake-up pulse generation/detection on the C/Q
 * line is not electrically possible over a plain UART, so detect_wakeup() is
 * not implemented for this PHY.
 */

/**
 * @brief Bind and initialize the UART PHY against a specific UART device.
 *
 * Most applications should call iolink_phy_uart_init_default() instead, which
 * resolves the device from the devicetree "zephyr,iolink-uart" chosen node.
 *
 * @param uart_dev Ready Zephyr UART device, or NULL.
 * @return 0 on success, negative error code on failure.
 */
int iolink_phy_uart_init(const struct device* uart_dev);

/**
 * @brief Initialize the UART PHY from the devicetree chosen/alias node.
 *
 * Resolves the UART from "zephyr,iolink-uart" (chosen) or "iolink-uart"
 * (alias) and binds the PHY to it.
 *
 * @return 0 on success, negative error code if no node is defined or the
 *         device is not ready.
 */
int iolink_phy_uart_init_default(void);

/**
 * @brief Get the PHY interface for the UART driver.
 * @return Pointer to the PHY API structure.
 */
const iolink_phy_api_t* iolink_phy_uart_get(void);

#endif /* IOLINK_PHY_UART_H */
