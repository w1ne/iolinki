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
 * @ingroup iolinki_events
 *
 * Implements the device event queue (enqueue/pop/peek), severity ranking and
 * event-code classification used to report diagnostics to the master.
 */

#include "iolinki/events.h"
#include "iolinki/platform.h"
#include "iolinki/utils.h"
#include <string.h>

void iolink_events_init(iolink_events_ctx_t* ctx)
{
    if (!iolink_ctx_zero(ctx, sizeof(iolink_events_ctx_t))) {
        return;
    }
}

/** @brief EventQualifier for an event type (A.6.4, Figure A.24). */
static uint8_t event_qualifier(iolink_event_type_t type)
{
    uint8_t q = 0xC0U; /* MODE = event appears */
    switch (type) {
        case IOLINK_EVENT_TYPE_NOTIFICATION:
            q |= (0x01U << 4);
            break;
        case IOLINK_EVENT_TYPE_WARNING:
            q |= (0x02U << 4);
            break;
        case IOLINK_EVENT_TYPE_ERROR:
            q |= (0x03U << 4);
            break;
        default:
            break;
    }
    q |= 0x02U; /* SOURCE = device, INSTANCE = DL */
    return q;
}

/** @brief Rebuild the Table 58 event memory from the oldest queued events.
 *
 * Up to six slots are populated; the StatusCode carries a bit per active slot.
 * The caller holds the critical section. */
static void event_memory_rebuild(iolink_events_ctx_t* ctx)
{
    (void) memset(ctx->memory, 0, sizeof(ctx->memory));
    uint8_t slots = ctx->count;
    if (slots > 6U) {
        slots = 6U;
    }
    for (uint8_t i = 0U; i < slots; i++) {
        uint8_t idx = (uint8_t) ((ctx->head + i) % IOLINK_EVENT_QUEUE_SIZE);
        const iolink_event_t* ev = &ctx->queue[idx];
        ctx->memory[1U + i * 3U] = event_qualifier(ev->type);
        ctx->memory[2U + i * 3U] = (uint8_t) (ev->code >> 8);
        ctx->memory[3U + i * 3U] = (uint8_t) (ev->code & 0xFFU);
    }
    /* StatusCode type 2: Event Details = 1, bits 0-5 = one bit per active slot
       (Figure A.22/A.23: bit 0 is Event 1, bit 1 is Event 2, ...). */
    uint8_t active = 0U;
    for (uint8_t i = 0U; i < slots; i++) {
        active |= (uint8_t) (1U << i);
    }
    ctx->memory[0] = (uint8_t) (0x80U | active);
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

    /* Table 60 T2/T3: write the memory, then set the Event flag. The memory is
       frozen on the first master read so a readout is consistent; events queued
       after that stay invisible until the StatusCode write confirmation. */
    if (!ctx->frozen) {
        event_memory_rebuild(ctx);
    }
    ctx->flag = true;

    iolink_critical_exit();
}

bool iolink_events_flag(const iolink_events_ctx_t* ctx)
{
    return ((ctx != NULL) && ctx->flag);
}

uint8_t iolink_events_memory_read(iolink_events_ctx_t* ctx, uint8_t addr)
{
    if ((ctx == NULL) || (addr >= sizeof(ctx->memory))) {
        return 0U;
    }
    iolink_critical_enter();
    /* First readout freezes the memory until the StatusCode confirmation. */
    if (!ctx->frozen && (ctx->count > 0U)) {
        event_memory_rebuild(ctx);
        ctx->frozen = true;
    }
    const uint8_t value = ctx->memory[addr];
    iolink_critical_exit();
    return value;
}

void iolink_events_memory_write(iolink_events_ctx_t* ctx, uint8_t addr, uint8_t value)
{
    (void) value;
    if ((ctx == NULL) || (addr != 0U)) {
        return;
    }

    iolink_critical_enter();
    /* Table 60 T5: confirmation releases the memory and clears the flag; any
       events queued while frozen become visible on the next readout. */
    ctx->frozen = false;
    ctx->flag = false;
    uint8_t acked = ctx->count;
    if (acked > 6U) {
        acked = 6U;
    }
    ctx->head = (uint8_t) ((ctx->head + acked) % IOLINK_EVENT_QUEUE_SIZE);
    ctx->count = (uint8_t) (ctx->count - acked);
    if (ctx->count > 0U) {
        event_memory_rebuild(ctx);
        ctx->frozen = true;
        ctx->flag = true;
    }
    else {
        (void) memset(ctx->memory, 0, sizeof(ctx->memory));
    }
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

iolink_event_type_t iolink_event_classify(uint16_t code)
{
    switch (code) {
        case IOLINK_EVENTCODE_NO_MALFUNCTION:
        case IOLINK_EVENTCODE_DS_UPLOAD_REQUEST:
            return IOLINK_EVENT_TYPE_NOTIFICATION;

        case IOLINK_EVENTCODE_TEMPERATURE_OVERRUN:
        case IOLINK_EVENTCODE_TEMPERATURE_UNDERRUN:
        case IOLINK_EVENTCODE_BATTERY_LOW:
        case IOLINK_EVENTCODE_SUPPLY_VOLTAGE_OVERRUN:
        case IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN:
        case IOLINK_EVENTCODE_SIMULATION_ACTIVE:
        case IOLINK_EVENTCODE_PV_RANGE_OVERRUN:
        case IOLINK_EVENTCODE_PV_RANGE_UNDERRUN:
        case IOLINK_EVENTCODE_MAINTENANCE_CLEANING:
        case IOLINK_EVENTCODE_MAINTENANCE_REFILL:
        case IOLINK_EVENTCODE_MAINTENANCE_WEAR:
            return IOLINK_EVENT_TYPE_WARNING;

        case IOLINK_EVENTCODE_GENERAL_MALFUNCTION:
        case IOLINK_EVENTCODE_TEMPERATURE_OVERLOAD:
        case IOLINK_EVENTCODE_HARDWARE_FAULT:
        case IOLINK_EVENTCODE_COMPONENT_MALFUNCTION:
        case IOLINK_EVENTCODE_NVM_LOSS:
        case IOLINK_EVENTCODE_POWER_SUPPLY_FAULT:
        case IOLINK_EVENTCODE_FUSE_BLOWN:
        case IOLINK_EVENTCODE_SOFTWARE_FAULT:
        case IOLINK_EVENTCODE_PARAMETER_ERROR:
        case IOLINK_EVENTCODE_PARAMETER_MISSING:
        case IOLINK_EVENTCODE_WIRE_BREAK:
        case IOLINK_EVENTCODE_SHORT_CIRCUIT:
        case IOLINK_EVENTCODE_GROUND_FAULT:
        case IOLINK_EVENTCODE_APPLICATION_FAULT:
        case IOLINK_EVENTCODE_MEASUREMENT_RANGE_EXCEEDED:
            return IOLINK_EVENT_TYPE_ERROR;

        default:
            /* Reserved / vendor-specific / unknown -> safe default. */
            return IOLINK_EVENT_TYPE_ERROR;
    }
}
