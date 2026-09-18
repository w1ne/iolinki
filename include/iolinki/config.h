/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_CONFIG_H
#define IOLINK_CONFIG_H

/**
 * @file config.h
 * @brief Compile-time configuration for iolinki stack.
 *
 * This file defines default values for buffer sizes and queue depths.
 * To override these defaults, define the macros in your build system
 * or in a private config header included before this file.
 */

/**
 * @defgroup iolinki_config Compile-time Configuration
 * @brief Overridable buffer sizes, queue depths and timing defaults.
 * @{
 */

/* -------------------------------------------------------------------------
 * ISDU Configuration
 * ------------------------------------------------------------------------- */

/**
 * @brief Max size of ISDU buffer (Service Data).
 * Limits the maximum size of a single ISDU read/write transaction.
 * Default: 256 bytes (sufficient for standard params).
 */
#ifndef IOLINK_ISDU_BUFFER_SIZE
#define IOLINK_ISDU_BUFFER_SIZE 256U
#endif

/* -------------------------------------------------------------------------
 * Events Configuration
 * ------------------------------------------------------------------------- */

/**
 * @brief Size of the diagnostic Event Queue.
 * Start small for resource constrained devices.
 * Default: 4
 */
#ifndef IOLINK_EVENT_QUEUE_SIZE
#define IOLINK_EVENT_QUEUE_SIZE 4U
#endif

/* -------------------------------------------------------------------------
 * Process Data Configuration
 * ------------------------------------------------------------------------- */

/**
 * @brief Max Input Process Data (Device -> Master) size in bytes.
 * V1.1 Spec supports up to 32 bytes allowed in standard M-sequences.
 * Default: 32
 */
#ifndef IOLINK_PD_IN_MAX_SIZE
#define IOLINK_PD_IN_MAX_SIZE 32U
#endif

/**
 * @brief Max Output Process Data (Master -> Device) size in bytes.
 * V1.1 Spec supports up to 32 bytes allowed in standard M-sequences.
 * Default: 32
 */
#ifndef IOLINK_PD_OUT_MAX_SIZE
#define IOLINK_PD_OUT_MAX_SIZE 32U
#endif

/* -------------------------------------------------------------------------
 * Timing Configuration
 * ------------------------------------------------------------------------- */

/**
 * @brief Enable timing enforcement by default.
 * Set to 1 to enforce t_ren / t_cycle, 0 to only measure.
 */
#ifndef IOLINK_TIMING_ENFORCE_DEFAULT
#define IOLINK_TIMING_ENFORCE_DEFAULT 0U
#endif

/**
 * @brief Wake-up pulse duration (T_WU) in microseconds.
 *
 * Spec Table 10: the wake-up request is a current pulse; this stack models the
 * pulse width used by the PHY as 80 us. (T_DWU is a separate 30..50 ms
 * retry delay and is not modelled by the device.)
 */
#ifndef IOLINK_T_WU_US
#define IOLINK_T_WU_US 80U
#endif

/**
 * @brief Response time limit (T_REN) in microseconds.
 *
 * Spec Table 10: a single value of at most 500 us, independent of the baudrate.
 */
#ifndef IOLINK_T_REN_US
#define IOLINK_T_REN_US 500U
#endif

/**
 * @brief Fallback to SIO after a failed wake-up without a valid message (T_DSIO).
 *
 * Spec Table 47 T10: 60..300 ms, default 300 ms.
 */
#ifndef IOLINK_T_DSIO_MS
#define IOLINK_T_DSIO_MS 300U
#endif

/* -------------------------------------------------------------------------
 * On-Request Data (OD) Configuration
 * ------------------------------------------------------------------------- */

/**
 * @brief Maximum On-Request Data size in bytes.
 * Type 1: 1 byte, Type 2: 2 bytes, Type 2_V extended: 4 bytes.
 * Default: 4 bytes (supports all types including extended Type 2_V)
 */
#ifndef IOLINK_OD_MAX_SIZE
#define IOLINK_OD_MAX_SIZE 4U
#endif

/**
 * @brief OD Event Mode: Single (0) or Multiple (1) event mode.
 * Single mode: Only one event transmitted per OD cycle.
 * Multiple mode: Multiple events can be queued in OD.
 * Default: 0 (Single event mode)
 */
#ifndef IOLINK_OD_EVENT_MODE
#define IOLINK_OD_EVENT_MODE 0U
#endif

/** @} */ /* end of iolinki_config */

#endif  // IOLINK_CONFIG_H
