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

/**
 * @file iolink.h
 * @brief Shared IO-Link stack types
 */

/**
 * @defgroup iolinki_core Core Types and Baudrates
 * @brief Shared IO-Link enumerations, configuration and callback types.
 * @{
 */

/**
 * @brief IO-Link M-sequence types
 *
 * Defines the frame structure and capabilities of the communication cycle.
 * See IO-Link Interface and System Specification V1.1.2, section 7.3.
 */
typedef enum
{
    IOLINK_M_SEQ_TYPE_0 = 0U,   /**< M-Sequence Type 0: On-request data (ISDU) only */
    IOLINK_M_SEQ_TYPE_1_1 = 1U, /**< M-Sequence Type 1_1: PD (fixed) + OD (1 byte) */
    IOLINK_M_SEQ_TYPE_1_2 = 2U, /**< M-Sequence Type 1_2: PD (fixed) + OD (1 byte) + ISDU */
    IOLINK_M_SEQ_TYPE_1_V = 3U, /**< M-Sequence Type 1_V: PD (variable) + OD (1 byte) */
    IOLINK_M_SEQ_TYPE_2_1 = 4U, /**< M-Sequence Type 2_1: PD (fixed) + OD (2 bytes) */
    IOLINK_M_SEQ_TYPE_2_2 = 5U, /**< M-Sequence Type 2_2: PD (fixed) + OD (2 bytes) + ISDU */
    IOLINK_M_SEQ_TYPE_2_V = 6U, /**< M-Sequence Type 2_V: PD (variable) + OD (2 bytes) + ISDU */
} iolink_m_seq_type_t;

/**
 * @brief IO-Link stack configuration
 *
 * Holds parameters required to initialize the stack behavior.
 */
typedef struct
{
    iolink_m_seq_type_t m_seq_type; /**< Primary M-sequence type supported by device */
    uint8_t min_cycle_time;         /**< Minimum cycle time in 0.1ms units (e.g. 20 = 2.0ms) */
    uint8_t pd_in_len;              /**< Process Data Input length (Device to Master) in bytes */
    uint8_t pd_out_len;             /**< Process Data Output length (Master to Device) in bytes */
    uint32_t t_pd_us;               /**< Power-on delay (t_pd) in microseconds */
} iolink_config_t;

/**
 * @brief Reset request type delivered to the application reset handler.
 */
typedef enum
{
    IOLINK_RESET_DEVICE = 0,     /**< System Command 0x80 (Device Reset) */
    IOLINK_RESET_APPLICATION = 1 /**< System Command 0x81 (Application Reset) */
} iolink_reset_type_t;

/**
 * @brief Application callback invoked when the Master requests a reset.
 *
 * @param type IOLINK_RESET_DEVICE or IOLINK_RESET_APPLICATION
 */
typedef void (*iolink_reset_handler_t)(iolink_reset_type_t type);

/** @} */ /* end of iolinki_core */

#endif  // IOLINK_H
