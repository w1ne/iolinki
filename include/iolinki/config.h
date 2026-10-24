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
 *
 * Every active Error/Warning occupies one entry for its "appears" and one for
 * its "disappears", so a device that reports many concurrent causes should set
 * this to at least twice IOLINK_EVENT_PENDING_MAX. The Table 58 event memory
 * presents at most min(IOLINK_EVENT_QUEUE_SIZE, 6) entries.
 * Default: 4
 */
#ifndef IOLINK_EVENT_QUEUE_SIZE
#define IOLINK_EVENT_QUEUE_SIZE 4U
#endif

/**
 * @brief Maximum number of simultaneously active (appears/disappears) events.
 *
 * Each active Error/Warning occupies one lifecycle slot. Slots that only wait
 * out the 1 s repeat cooldown are reused first. When every slot holds a live
 * event, a further Error/Warning is dropped and counted by
 * iolink_events_get_dropped_count().
 * Default: 8
 */
#ifndef IOLINK_EVENT_PENDING_MAX
#define IOLINK_EVENT_PENDING_MAX 8U
#endif

/**
 * @brief Minimum appearance time for an Error/Warning event (A.6.4).
 *
 * A disappearance is not reported until this long after the appearance.
 */
#ifndef IOLINK_EVENT_MIN_APPEAR_MS
#define IOLINK_EVENT_MIN_APPEAR_MS 50U
#endif

/**
 * @brief Minimum repeat interval for the same EventCode and EventQualifier
 * (10.10.2).
 *
 * A deferred appearance is queued by iolink_events_process() once this
 * interval has elapsed; it is never discarded.
 */
#ifndef IOLINK_EVENT_MIN_REPEAT_MS
#define IOLINK_EVENT_MIN_REPEAT_MS 1000U
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
 * Table A.10: 1 byte for most types, 2 for TYPE_1_2/TYPE_2_V, 8 for TYPE_1_V.
 * Default: 8 bytes (supports every streaming M-sequence type)
 */
#ifndef IOLINK_OD_MAX_SIZE
#define IOLINK_OD_MAX_SIZE 8U
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
