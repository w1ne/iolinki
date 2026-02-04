/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_EVENTS_H
#define IOLINK_EVENTS_H

#include <stdint.h>
#include <stdbool.h>
#include "iolinki/config.h"

/**
 * @file events.h
 * @brief IO-Link Event Handling
 */

/**
 * @brief IO-Link Event Severity Levels
 */
typedef enum
{
    IOLINK_EVENT_TYPE_NOTIFICATION = 0U, /**< Information only, no action required */
    IOLINK_EVENT_TYPE_WARNING = 1U,      /**< Potential issue, operation continues */
    IOLINK_EVENT_TYPE_ERROR = 2U         /**< Critical failure, operation might be degraded */
} iolink_event_type_t;

/**
 * @brief Standard IO-Link Event Codes (Spec V1.1.2)
 */
#define IOLINK_EVENT_COMM_CRC 0x1801U     /**< CRC error in communication */
#define IOLINK_EVENT_COMM_TIMEOUT 0x1802U /**< Timeout in communication */
#define IOLINK_EVENT_COMM_FRAMING 0x1803U /**< Framing error in communication */
#define IOLINK_EVENT_COMM_TIMING 0x1804U  /**< Timing violation in communication */

/**
 * @brief Event Descriptor
 *
 * Represents a single IO-Link diagnostic event.
 */
typedef struct
{
    uint16_t code;            /**< 16-bit IO-Link EventCode (per spec or device-specific) */
    iolink_event_type_t type; /**< Severity level */
} iolink_event_t;

/**
 * @brief Events Engine Context
 *
 * Manages the internal FIFO queue of pending events to be read by the Master.
 */
typedef struct
{
    iolink_event_t queue[IOLINK_EVENT_QUEUE_SIZE]; /**< Event FIFO buffer */
    uint8_t head;                                  /**< Queue head index */
    uint8_t tail;                                  /**< Queue tail index */
    uint8_t count;                                 /**< Number of events currently in queue */
} iolink_events_ctx_t;

/**
 * @brief Initialize the event engine
 *
 * Resets the queue and internal counters.
 *
 * @param ctx Event context to initialize
 */
void iolink_events_init(iolink_events_ctx_t *ctx);

/**
 * @brief Trigger a new diagnostic event
 *
 * Adds an event to the internal queue. If the queue is full, the newest event
 * is typically dropped or the oldest overwritten (depending on implementation).
 *
 * @param ctx Event context
 * @param code 16-bit IO-Link EventCode
 * @param type Severity level
 */
void iolink_event_trigger(iolink_events_ctx_t *ctx, uint16_t code, iolink_event_type_t type);

/**
 * @brief Check if any events are pending for Master retrieval
 *
 * @param ctx Event context
 * @return true if one or more events are in the queue
 */
bool iolink_events_pending(const iolink_events_ctx_t *ctx);

/**
 * @brief Pop the oldest event from the queue
 *
 * Typically called by the ISDU engine when satisfying a read request for Index 2.
 *
 * @param ctx Event context
 * @param event [out] Pointer to store the popped event details
 * @return true if an event was successfully popped, false if queue was empty
 */
bool iolink_events_pop(iolink_events_ctx_t *ctx, iolink_event_t *event);

/**
 * @brief Peek at the oldest event without removing it from the queue
 *
 * Used for OD event content to check the next event code.
 *
 * @param ctx Event context
 * @param event [out] Pointer to store the event details
 * @return true if an event was available, false if queue was empty
 */
bool iolink_events_peek(const iolink_events_ctx_t *ctx, iolink_event_t *event);

/**
 * @brief Get the highest severity level currently in the event queue
 *
 * Maps to IO-Link Device Status (0=OK, 1=Maintenance, 2=Out of Spec, 3=Failure)
 *
 * @param ctx Event context
 * @return uint8_t Highest severity level (0-3)
 */
uint8_t iolink_events_get_highest_severity(iolink_events_ctx_t *ctx);

/**
 * @brief Copy all pending events to a buffer without popping them
 *
 * @param ctx Event context
 * @param out_events [out] Buffer to store event copies
 * @param max_count Maximum number of events to copy
 * @return uint8_t Number of events copied
 */
uint8_t iolink_events_get_all(iolink_events_ctx_t *ctx, iolink_event_t *out_events,
                              uint8_t max_count);

#endif  // IOLINK_EVENTS_H
