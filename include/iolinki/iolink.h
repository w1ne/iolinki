/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_H
#define IOLINK_H

#include <stdint.h>
#include "iolinki/phy.h"
#include "iolinki/application.h"

/**
 * @file iolink.h
 * @brief Main header for iolinki IO-Link stack
 */

/**
 * @brief IO-Link M-sequence types
 */
typedef enum {
    IOLINK_M_SEQ_TYPE_0,    /**< On-request data only */
    IOLINK_M_SEQ_TYPE_1_1,  /**< PD (fixed) + OD (1 byte) */
    IOLINK_M_SEQ_TYPE_1_2,  /**< PD (fixed) + OD (1 byte) + ISDU */
    IOLINK_M_SEQ_TYPE_1_V,  /**< PD (variable) + OD (1 byte) */
    IOLINK_M_SEQ_TYPE_2_1,  /**< PD (fixed) + OD (2 bytes) */
    IOLINK_M_SEQ_TYPE_2_2,  /**< PD (fixed) + OD (2 bytes) + ISDU */
    IOLINK_M_SEQ_TYPE_2_V,  /**< PD (variable) + OD (1 byte) + ISDU */
} iolink_m_seq_type_t;

/**
 * @brief IO-Link stack configuration
 */
typedef struct {
    iolink_m_seq_type_t m_seq_type; /**< M-sequence type support */
    uint8_t min_cycle_time;         /**< Minimum cycle time (encoded per spec) */
    uint8_t pd_in_len;              /**< Process Data Input length (bytes) */
    uint8_t pd_out_len;             /**< Process Data Output length (bytes) */
} iolink_config_t;

/**
 * @brief Initialize the IO-Link stack
 * 
 * @param phy Pointer to the PHY implementation to use
 * @param config Pointer to stack configuration (copied internally)
 * @return int 0 on success, negative error code otherwise
 */
int iolink_init(const iolink_phy_api_t *phy, const iolink_config_t *config);

/**
 * @brief Process the IO-Link stack logic
 * 
 * This must be called periodically (e.g. every 1ms).
 */
void iolink_process(void);

#endif // IOLINK_H
