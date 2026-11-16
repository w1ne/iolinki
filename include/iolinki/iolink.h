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
 * Encode the IO-Link advertised minimum cycle time from units of 100 us.
 * The Direct Parameter octet has three linear intervals:
 * 0.1 ms (0.4..6.3 ms), 0.4 ms (6.4..31.6 ms),
 * and 1.6 ms (32.0..132.8 ms). Round up to a representable period.
 */
static inline uint8_t iolink_cycle_time_encode(uint16_t ticks)
{
    if (ticks == 0U) return 0U; /* no declared minimum */
    if (ticks <= 3U) return 4U; /* minimum 0.4 ms */
    if (ticks <= 63U) return (uint8_t) ticks;

    if (ticks <= 316U) {
        const uint16_t steps = (uint16_t) ((ticks - 64U + 3U) / 4U);
        return (uint8_t) (0x40U | steps);
    }

    const uint32_t later = ticks > 320U ? ticks - 320U : 0U;
    uint32_t steps = (later + 15U) / 16U;
    if (steps > 63U) steps = 63U;
    return (uint8_t) (0x80U | steps);
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
    /** OD width: 0 keeps the type default. TYPE_2_V accepts 1 or 2 with nonzero PD.
     * OD1 advertises code4 and requires at least 3 PD octets in one direction. */
    uint8_t od_len;
} iolink_config_t;

/**
 * @brief Reset request type delivered to the application reset handler.
 */
typedef enum
{
    IOLINK_RESET_DEVICE = 0,          /**< System Command 0x80 (Device Reset) */
    IOLINK_RESET_APPLICATION = 1,     /**< System Command 0x81 (Application Reset) */
    IOLINK_RESET_RESTORE_FACTORY = 2, /**< System Command 0x82: reset application parameters */
    IOLINK_RESET_BACK_TO_BOX = 3      /**< System Command 0x83: disable outputs, stop */
} iolink_reset_type_t;

/**
 * @brief Application callback invoked when the Master requests a reset.
 *
 * - IOLINK_RESET_APPLICATION: called before the ISDU response (10.7.3); set
 *   the technology specific parameters to their defaults.
 * - IOLINK_RESET_RESTORE_FACTORY: called before the ISDU response, after the
 *   stack has restored its own parameters (10.7.4); reset application
 *   parameters.
 * - IOLINK_RESET_DEVICE: called after the ISDU response was read and at least
 *   3 further M-sequences (10.7.2); perform the warm start.
 * - IOLINK_RESET_BACK_TO_BOX: called after the ISDU response was read and at
 *   least 3 further M-sequences (10.7.5); disable all output drivers. The
 *   stack then stops communication until the next power cycle.
 *
 * If no handler is registered, SystemCommands 0x80, 0x81 and 0x83 are refused
 * with FUNC_NOTAVAIL.
 *
 * @param type Reset request type
 */
typedef void (*iolink_reset_handler_t)(iolink_reset_type_t type);

/** @} */ /* end of iolinki_core */

#endif  // IOLINK_H
