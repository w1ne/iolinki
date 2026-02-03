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

typedef enum {
    IOLINK_EVENT_TYPE_NOTIFICATION = 0,
    IOLINK_EVENT_TYPE_WARNING      = 1,
    IOLINK_EVENT_TYPE_ERROR        = 2
} iolink_event_type_t;

typedef struct {
    uint16_t code;
    iolink_event_type_t type;
} iolink_event_t;


typedef struct {
    iolink_event_t queue[IOLINK_EVENT_QUEUE_SIZE];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
} iolink_events_ctx_t;

/**
 * @brief Initialize event engine
 * @param ctx Event context
 */
void iolink_events_init(iolink_events_ctx_t *ctx);

/**
 * @brief Trigger a new event
 * @param ctx Event context
 * @param code 16-bit IO-Link EventCode
 * @param type severity level
 */
void iolink_event_trigger(iolink_events_ctx_t *ctx, uint16_t code, iolink_event_type_t type);

/**
 * @brief Check if any events are pending for Master reading
 * @param ctx Event context
 * @return true if events are in queue, false otherwise
 */
bool iolink_events_pending(iolink_events_ctx_t *ctx);

/**
 * @brief Pop oldest event for ISDU Index 2 reading
 * @param ctx Event context
 * @param event Pointer to store event details
 * @return true if event popped, false if queue empty
 */
bool iolink_events_pop(iolink_events_ctx_t *ctx, iolink_event_t *event);

#endif // IOLINK_EVENTS_H
