/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_PHY_VIRTUAL_H
#define IOLINK_PHY_VIRTUAL_H

#include "iolinki/phy.h"

/**
 * @file phy_virtual.h
 * @brief Virtual PHY implementation for simulation
 */

/**
 * @brief Get the virtual PHY provider
 * 
 * This PHY communicates with a virtual IO-Link Master over a network socket.
 * 
 * @return const iolink_phy_api_t* 
 */
/**
 * @brief Set the serial port for virtual PHY
 * @param port TTY path (e.g., "/dev/pts/5")
 */
void iolink_phy_virtual_set_port(const char *port);

const iolink_phy_api_t* iolink_phy_virtual_get(void);

#endif // IOLINK_PHY_VIRTUAL_H
