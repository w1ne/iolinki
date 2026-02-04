/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_PHY_GENERIC_H
#define IOLINK_PHY_GENERIC_H

#include "iolinki/phy.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file phy_generic.h
 * @brief Reference PHY template for real hardware ports.
 *
 * This template provides stub implementations that must be adapted
 * to the target transceiver and MCU/SoC peripherals.
 */

/**
 * @brief Get the generic PHY API (template).
 *
 * @return const iolink_phy_api_t* Pointer to template PHY API
 */
const iolink_phy_api_t* iolink_phy_generic_get(void);

#ifdef __cplusplus
}
#endif

#endif  // IOLINK_PHY_GENERIC_H
