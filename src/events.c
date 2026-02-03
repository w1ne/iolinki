/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/events.h"
#include "iolinki/platform.h"
#include <string.h>

void iolink_events_init(iolink_events_ctx_t *ctx)
{
    if (ctx) {
        memset(ctx, 0, sizeof(iolink_events_ctx_t));
    }
}

void iolink_event_trigger(iolink_events_ctx_t *ctx, uint16_t code, iolink_event_type_t type)
{
    if (!ctx) return;

    iolink_critical_enter();

    if (ctx->count >= IOLINK_EVENT_QUEUE_SIZE) {
        /* Drop oldest (inline to avoid recursive lock) */
         ctx->head = (uint8_t)((ctx->head + 1) % IOLINK_EVENT_QUEUE_SIZE);
         ctx->count--;
    }

    ctx->queue[ctx->tail].code = code;
    ctx->queue[ctx->tail].type = type;
    ctx->tail = (uint8_t)((ctx->tail + 1) % IOLINK_EVENT_QUEUE_SIZE);
    ctx->count++;
    
    iolink_critical_exit();
}

bool iolink_events_pending(iolink_events_ctx_t *ctx)
{
    /* Single byte read is typically atomic, avoiding lock for perf */
    return (ctx && ctx->count > 0);
}

bool iolink_events_pop(iolink_events_ctx_t *ctx, iolink_event_t *event)
{
    if (!ctx) return false;
    
    bool ret = false;
    iolink_critical_enter();

    if (ctx->count > 0) {
        *event = ctx->queue[ctx->head];
        ctx->head = (uint8_t)((ctx->head + 1) % IOLINK_EVENT_QUEUE_SIZE);
        ctx->count--;
        ret = true;
    }
    
    iolink_critical_exit();
    return ret;
}
