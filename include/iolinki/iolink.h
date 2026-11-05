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
    IOLINK_M_SEQ_TYPE_0 = 0U,   /**< M-Sequence Type 0: On-request data (ISDU) only. */
    IOLINK_M_SEQ_TYPE_1_1 = 1U, /**< Legacy Type 1_1 (interleaved). */
    IOLINK_M_SEQ_TYPE_1_2 = 2U, /**< Type 1_2: 2 OD octets, no process data (A.7). */
    IOLINK_M_SEQ_TYPE_1_V = 3U, /**< Type 1_V: 8 OD octets, no process data (A.8). */
    IOLINK_M_SEQ_TYPE_2_1 = 4U, /**< Type 2_1: 1 PD-in octet + 1 OD octet (A.9). */
    IOLINK_M_SEQ_TYPE_2_2 = 5U, /**< Type 2_2: 2 PD-in octets + 1 OD octet (A.10). */
    IOLINK_M_SEQ_TYPE_2_V = 6U, /**< Type 2_V: 0..32 PD in/out + 1/2 OD octets (A.14). */
} iolink_m_seq_type_t;

/**
 * @brief Return the On-request Data octet count for an M-sequence type.
 *
 * Table A.10: TYPE_1_2 carries two OD octets, TYPE_1_V eight, TYPE_2_V the
 * configured two-octet variant, and the remaining types one.
 * @param m_seq_type M-sequence type value.
 * @return OD width in octets.
 */
static inline uint8_t iolink_mseq_od_len(uint8_t m_seq_type)
{
    switch (m_seq_type) {
        case IOLINK_M_SEQ_TYPE_1_2:
            return 2U;
        case IOLINK_M_SEQ_TYPE_1_V:
            return 8U;
        case IOLINK_M_SEQ_TYPE_2_V:
            return 2U;
        default:
            return 1U;
    }
}

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
    uint8_t od_len; /**< OD width: 0 keeps the type default; TYPE_2_V accepts 1 or 2. */
} iolink_config_t;

/**
 * @brief Encode a cycle time in 0.1 ms units to a Table B.3 octet.
 *
 * Time base bits 7:6 and multiplier bits 5:0 per Table B.3. A value of 0 is
 * encoded as 0x00, which B.1.3 defines as "no MinCycleTime provided"; values
 * below the 0.4 ms minimum are clamped to 0.4 ms; values above 132.8 ms
 * saturate at 132.8 ms (time base 10, multiplier 63). Values between the
 * representable steps are rounded up, so the encoded minimum never claims a
 * cycle time shorter than the requested one.
 * @param units100us Cycle time in 100 us (0.1 ms) units.
 * @return Table B.3 encoded octet.
 */
static inline uint8_t iolink_cycle_time_encode(uint16_t units100us)
{
    if (units100us == 0U) {
        return 0x00U; /* B.1.3: no MinCycleTime provided. */
    }
    if (units100us < 4U) {
        units100us = 4U; /* Table B.3 minimum is 0.4 ms. */
    }
    if (units100us <= 63U) {
        return (uint8_t) units100us; /* 0.1 ms x multiplier: 0.4 .. 6.3 ms */
    }
    if (units100us <= 316U) {
        const uint16_t m = (uint16_t) ((units100us - 64U + 3U) / 4U); /* ceil to 0.4 ms steps */
        return (uint8_t) (0x40U | (uint8_t) m);
    }
    /* 32.0 ms + 1.6 ms x multiplier. 317..320 all round up to multiplier 0. */
    const uint32_t excess = (units100us > 320U) ? ((uint32_t) units100us - 320U) : 0U;
    uint32_t m = (excess + 15U) / 16U; /* ceil to 1.6 ms steps */
    if (m > 63U) {
        m = 63U; /* saturate at 132.8 ms */
    }
    return (uint8_t) (0x80U | (uint8_t) m);
}

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
