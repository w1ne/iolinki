/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file events.c
 * @brief IO-Link Event Handling
 */

#include "iolinki/events.h"
#include "iolinki/platform.h"
#include "iolinki/utils.h"

void iolink_events_init(iolink_events_ctx_t* ctx)
{
    if (!iolink_ctx_zero(ctx, sizeof(iolink_events_ctx_t))) {
        return;
    }
}

void iolink_event_trigger(iolink_events_ctx_t* ctx, uint16_t code, iolink_event_type_t type)
{
    if (ctx == NULL) {
        return;
    }

    iolink_critical_enter();

    if (ctx->count >= IOLINK_EVENT_QUEUE_SIZE) {
        /* Drop oldest (inline to avoid recursive lock) */
        ctx->head = (uint8_t) ((ctx->head + 1U) % IOLINK_EVENT_QUEUE_SIZE);
        ctx->count--;
    }

    ctx->queue[ctx->tail].code = code;
    ctx->queue[ctx->tail].type = type;
    ctx->tail = (uint8_t) ((ctx->tail + 1U) % IOLINK_EVENT_QUEUE_SIZE);
    ctx->count++;

    iolink_critical_exit();
}

bool iolink_events_pending(const iolink_events_ctx_t* ctx)
{
    /* Single byte read is typically atomic, avoiding lock for perf */
    return ((ctx != NULL) && (ctx->count > 0U));
}

bool iolink_events_pop(iolink_events_ctx_t* ctx, iolink_event_t* event)
{
    if ((ctx == NULL) || (event == NULL)) {
        return false;
    }

    bool ret = false;
    iolink_critical_enter();

    if (ctx->count > 0U) {
        *event = ctx->queue[ctx->head];
        ctx->head = (uint8_t) ((ctx->head + 1U) % IOLINK_EVENT_QUEUE_SIZE);
        ctx->count--;
        ret = true;
    }

    iolink_critical_exit();
    return ret;
}

bool iolink_events_peek(const iolink_events_ctx_t* ctx, iolink_event_t* event)
{
    if ((ctx == NULL) || (event == NULL)) {
        return false;
    }

    /* Single read is atomic for small structs, but use critical section for safety */
    iolink_critical_enter();

    bool ret = false;
    if (ctx->count > 0U) {
        *event = ctx->queue[ctx->head];
        ret = true;
    }

    iolink_critical_exit();
    return ret;
}

uint8_t iolink_events_get_highest_severity(iolink_events_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->count == 0U)) {
        return 0U; /* OK */
    }

    uint8_t highest_msp = 0U;
    iolink_critical_enter();
    for (uint8_t i = 0U; i < ctx->count; i++) {
        uint8_t idx = (uint8_t) ((ctx->head + i) % IOLINK_EVENT_QUEUE_SIZE);
        iolink_event_type_t type = ctx->queue[idx].type;

        uint8_t severity = 0U;
        switch (type) {
            case IOLINK_EVENT_TYPE_NOTIFICATION:
                severity = 1U;
                break; /* Maintenance */
            case IOLINK_EVENT_TYPE_WARNING:
                severity = 2U;
                break; /* Out of Spec */
            case IOLINK_EVENT_TYPE_ERROR:
                severity = 3U;
                break; /* Failure */
            default:
                severity = 0U;
                break;
        }

        if (severity > highest_msp) {
            highest_msp = severity;
        }
    }
    iolink_critical_exit();
    return highest_msp;
}

uint8_t iolink_events_get_all(iolink_events_ctx_t* ctx, iolink_event_t* out_events,
                              uint8_t max_count)
{
    if ((ctx == NULL) || (out_events == NULL) || (max_count == 0U)) {
        return 0U;
    }

    uint8_t copied = 0U;
    iolink_critical_enter();
    uint8_t count = ctx->count;
    uint8_t to_copy = (count < max_count) ? count : max_count;

    for (uint8_t i = 0U; i < to_copy; i++) {
        uint8_t idx = (uint8_t) ((ctx->head + i) % IOLINK_EVENT_QUEUE_SIZE);
        out_events[i] = ctx->queue[idx];
        copied++;
    }
    iolink_critical_exit();
    return copied;
}
