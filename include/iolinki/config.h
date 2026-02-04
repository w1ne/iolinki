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

#endif // IOLINK_CONFIG_H
