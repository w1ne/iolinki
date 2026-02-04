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
 * 
 * Defines the frame structure and capabilities of the communication cycle.
 * See IO-Link Interface and System Specification V1.1.2, section 7.3.
 */
typedef enum {
    IOLINK_M_SEQ_TYPE_0 = 0U,    /**< M-Sequence Type 0: On-request data (ISDU) only */
    IOLINK_M_SEQ_TYPE_1_1 = 1U,  /**< M-Sequence Type 1_1: PD (fixed) + OD (1 byte) */
    IOLINK_M_SEQ_TYPE_1_2 = 2U,  /**< M-Sequence Type 1_2: PD (fixed) + OD (1 byte) + ISDU */
    IOLINK_M_SEQ_TYPE_1_V = 3U,  /**< M-Sequence Type 1_V: PD (variable) + OD (1 byte) */
    IOLINK_M_SEQ_TYPE_2_1 = 4U,  /**< M-Sequence Type 2_1: PD (fixed) + OD (2 bytes) */
    IOLINK_M_SEQ_TYPE_2_2 = 5U,  /**< M-Sequence Type 2_2: PD (fixed) + OD (2 bytes) + ISDU */
    IOLINK_M_SEQ_TYPE_2_V = 6U,  /**< M-Sequence Type 2_V: PD (variable) + OD (2 bytes) + ISDU */
} iolink_m_seq_type_t;

/**
 * @brief IO-Link stack configuration
 * 
 * Holds parameters required to initialize the stack behavior.
 */
typedef struct {
    iolink_m_seq_type_t m_seq_type; /**< Primary M-sequence type supported by device */
    uint8_t min_cycle_time;         /**< Minimum cycle time in 0.1ms units (e.g. 20 = 2.0ms) */
    uint8_t pd_in_len;              /**< Process Data Input length (Device to Master) in bytes */
    uint8_t pd_out_len;             /**< Process Data Output length (Master to Device) in bytes */
} iolink_config_t;

/**
 * @brief Initialize the IO-Link stack
 * 
 * Configures the internal state machine, ISDU engine, and PHY interface.
 * 
 * @param phy Pointer to the PHY implementation API
 * @param config Pointer to stack configuration (copied internally)
 * @return int 0 on success, negative error code (e.g. -1 for NULL PHY)
 */
int iolink_init(const iolink_phy_api_t *phy, const iolink_config_t *config);

/**
 * @brief Process the IO-Link stack logic
 * 
 * Main execution entry point. Handles PHY byte collection, state transitions,
 * frame assembly, and response generation. Must be called in the main loop.
 */
void iolink_process(void);

#include "iolinki/events.h"
#include "iolinki/data_storage.h"

/**
 * @brief Get the events context of the stack
 * 
 * Used to trigger or check for pending diagnostic events.
 * 
 * @return iolink_events_ctx_t* Pointer to the internal events context
 */
iolink_events_ctx_t* iolink_get_events_ctx(void);

/**
 * @brief Get the data storage context of the stack
 * 
 * Used to manage Data Storage (DS) upload/download and synchronization.
 * 
 * @return iolink_ds_ctx_t* Pointer to the internal DS context
 */
iolink_ds_ctx_t* iolink_get_ds_ctx(void);

#endif // IOLINK_H
