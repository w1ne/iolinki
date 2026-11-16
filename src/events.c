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
#include "iolinki/time_utils.h"
#include "iolinki/utils.h"
#include <string.h>

/* qflags bits. */
#define EVQ_PRESENTED 0x01U /* Shown in the frozen event memory */
#define EVQ_LIFECYCLE 0x02U /* Queued by the appear/disappear dispatcher */

/* iolink_event_cond_t state. */
#define COND_FREE 0U
#define COND_ACTIVE 1U
#define COND_RECENT 2U /* Inactive; kept only for the repeat-interval check */

/* iolink_event_cond_t flags. */
#define COND_APPEAR_SENT 0x01U    /* "appears" queued, "disappears" not yet */
#define COND_CLEAR_REQ 0x02U      /* Clear requested, "disappears" deferred */
#define COND_HAS_APPEAR 0x04U     /* appear_ms is valid and within the repeat window */
#define COND_HAS_DISAPPEAR 0x08U  /* disappear_ms is valid and within the repeat window */

void iolink_events_init(iolink_events_ctx_t* ctx)
{
    if (!iolink_ctx_zero(ctx, sizeof(iolink_events_ctx_t))) {
        return;
    }
}

/** @brief EventQualifier for an event (A.6.4, Figure A.24). */
static uint8_t event_qualifier(iolink_event_type_t type, iolink_event_mode_t mode)
{
    uint8_t q;
    switch (mode) {
        case IOLINK_EVENT_MODE_SINGLE_SHOT:
            q = 0x40U;
            break;
        case IOLINK_EVENT_MODE_DISAPPEARS:
            q = 0x80U;
            break;
        default:
            q = 0xC0U; /* MODE = event appears (also legacy records) */
            break;
    }
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
    q |= 0x04U; /* SOURCE = device (bit 3 = 0), INSTANCE = Application (Table A.17) */
    return q;
}

/** @brief Elapsed ms from @p then to @p now; correct across a 32-bit wrap. */
static uint32_t elapsed_ms(uint32_t now, uint32_t then)
{
    return (uint32_t) (now - then);
}

static void count_dropped(iolink_events_ctx_t* ctx)
{
    if (ctx->dropped < UINT32_MAX) {
        ctx->dropped++;
    }
}

/** @brief Physical queue index of logical position @p i. */
static uint8_t queue_slot(const iolink_events_ctx_t* ctx, uint8_t i)
{
    return (uint8_t) (((uint16_t) ctx->head + i) % IOLINK_EVENT_QUEUE_SIZE);
}

/** @brief Memory priority of a queued event: errors before warnings (10.10.2). */
static uint8_t event_priority(iolink_event_type_t type)
{
    if (type == IOLINK_EVENT_TYPE_ERROR) {
        return 0U;
    }
    if (type == IOLINK_EVENT_TYPE_WARNING) {
        return 1U;
    }
    return 2U;
}

/** @brief Select up to six queued events for the memory: by priority, then FIFO.
 *  Writes physical queue indices to @p sel. The caller holds the lock. */
static uint8_t event_memory_select(const iolink_events_ctx_t* ctx,
                                   uint8_t sel[IOLINK_EVENT_MEMORY_SLOTS])
{
    uint8_t n = 0U;
    for (uint8_t prio = 0U; prio < 3U; prio++) {
        for (uint8_t i = 0U; (i < ctx->count) && (n < IOLINK_EVENT_MEMORY_SLOTS); i++) {
            uint8_t idx = queue_slot(ctx, i);
            if (event_priority(ctx->queue[idx].type) == prio) {
                sel[n] = idx;
                n++;
            }
        }
    }
    return n;
}

/** @brief Rebuild the Table 58 event memory from the queue.
 *
 * Up to six slots are populated, errors first; the StatusCode carries a bit
 * per active slot. With @p freeze the selected entries are marked presented,
 * so the StatusCode confirmation removes exactly those entries.
 * The caller holds the critical section. */
static void event_memory_rebuild(iolink_events_ctx_t* ctx, bool freeze)
{
    uint8_t sel[IOLINK_EVENT_MEMORY_SLOTS];
    (void) memset(ctx->memory, 0, sizeof(ctx->memory));
    uint8_t slots = event_memory_select(ctx, sel);
    uint8_t active = 0U;
    for (uint8_t i = 0U; i < slots; i++) {
        const iolink_event_t* ev = &ctx->queue[sel[i]];
        ctx->memory[1U + i * 3U] = event_qualifier(ev->type, ev->mode);
        ctx->memory[2U + i * 3U] = (uint8_t) (ev->code >> 8);
        ctx->memory[3U + i * 3U] = (uint8_t) (ev->code & 0xFFU);
        /* StatusCode type 2 (Figure A.22/A.23): bit 0 is Event 1, ... */
        active |= (uint8_t) (1U << i);
        if (freeze) {
            ctx->qflags[sel[i]] |= EVQ_PRESENTED;
        }
    }
    if (slots > 0U) {
        ctx->memory[0] = (uint8_t) (0x80U | active);
    }
    if (freeze) {
        ctx->frozen = true;
    }
}

/** @brief Remove the oldest entry that is neither presented nor owned by a
 *  condition. Returns false if every entry is protected. Caller holds lock. */
static bool queue_evict_oldest(iolink_events_ctx_t* ctx)
{
    for (uint8_t i = 0U; i < ctx->count; i++) {
        uint8_t idx = queue_slot(ctx, i);
        if ((ctx->qflags[idx] & (EVQ_PRESENTED | EVQ_LIFECYCLE)) == 0U) {
            /* Shift the newer entries down one position. */
            for (uint8_t j = i; (uint8_t) (j + 1U) < ctx->count; j++) {
                uint8_t dst = queue_slot(ctx, j);
                uint8_t src = queue_slot(ctx, (uint8_t) (j + 1U));
                ctx->queue[dst] = ctx->queue[src];
                ctx->qflags[dst] = ctx->qflags[src];
            }
            ctx->count--;
            ctx->tail = queue_slot(ctx, ctx->count);
            ctx->qflags[ctx->tail] = 0U;
            return true;
        }
    }
    return false;
}

/** @brief Append an event and raise the Event flag (Table 60 T2/T3).
 *
 * A full queue evicts the oldest unprotected legacy entry when @p evict is
 * set; entries shown in the frozen memory and condition Events are never
 * evicted. Caller holds the lock. */
static bool queue_push(iolink_events_ctx_t* ctx, uint16_t code, iolink_event_type_t type,
                       iolink_event_mode_t mode, uint8_t qflag, bool evict)
{
    if (ctx->count >= IOLINK_EVENT_QUEUE_SIZE) {
        if (!evict || !queue_evict_oldest(ctx)) {
            return false;
        }
        count_dropped(ctx);
    }
    ctx->queue[ctx->tail].code = code;
    ctx->queue[ctx->tail].type = type;
    ctx->queue[ctx->tail].mode = mode;
    ctx->qflags[ctx->tail] = qflag;
    ctx->tail = (uint8_t) ((ctx->tail + 1U) % IOLINK_EVENT_QUEUE_SIZE);
    ctx->count++;

    /* The memory is frozen on the first master read so a readout is
       consistent; events queued after that stay invisible until the
       StatusCode write confirmation. */
    if (!ctx->frozen) {
        event_memory_rebuild(ctx, false);
    }
    ctx->flag = true;
    return true;
}

void iolink_event_trigger(iolink_events_ctx_t* ctx, uint16_t code, iolink_event_type_t type)
{
    if (ctx == NULL) {
        return;
    }

    iolink_critical_enter();
    if (!queue_push(ctx, code, type, IOLINK_EVENT_MODE_LEGACY, 0U, true)) {
        count_dropped(ctx);
    }
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
        event_memory_rebuild(ctx, true);
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
    /* A confirmation without a prior readout acknowledges what the memory
       shows at this moment. */
    if (!ctx->frozen && (ctx->count > 0U)) {
        event_memory_rebuild(ctx, true);
    }
    /* Table 60 T5: remove exactly the presented entries, keep events that
       arrived while the memory was frozen (in queue order). */
    uint8_t kept = 0U;
    for (uint8_t i = 0U; i < ctx->count; i++) {
        uint8_t src = queue_slot(ctx, i);
        if ((ctx->qflags[src] & EVQ_PRESENTED) == 0U) {
            uint8_t dst = queue_slot(ctx, kept);
            if (dst != src) {
                ctx->queue[dst] = ctx->queue[src];
                ctx->qflags[dst] = ctx->qflags[src];
            }
            kept++;
        }
    }
    for (uint8_t i = kept; i < ctx->count; i++) {
        ctx->qflags[queue_slot(ctx, i)] = 0U;
    }
    ctx->count = kept;
    ctx->tail = queue_slot(ctx, kept);
    ctx->frozen = false;
    ctx->flag = false;

    /* Release the memory and present the remaining events at once. */
    if (ctx->count > 0U) {
        event_memory_rebuild(ctx, true);
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
        ctx->qflags[ctx->head] = 0U;
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

/* -------------------------------------------------------------------------
 * Event Dispatcher: appear/disappear lifecycle (10.10.2)
 * ------------------------------------------------------------------------- */

static iolink_event_cond_t* cond_find(iolink_events_ctx_t* ctx, uint16_t code)
{
    for (uint8_t i = 0U; i < IOLINK_EVENT_CONDITION_SLOTS; i++) {
        if ((ctx->conds[i].state != COND_FREE) && (ctx->conds[i].code == code)) {
            return &ctx->conds[i];
        }
    }
    return NULL;
}

/** @brief Get a free slot; reuse a recent (inactive) slot if none is free. */
static iolink_event_cond_t* cond_alloc(iolink_events_ctx_t* ctx)
{
    iolink_event_cond_t* recent = NULL;
    for (uint8_t i = 0U; i < IOLINK_EVENT_CONDITION_SLOTS; i++) {
        if (ctx->conds[i].state == COND_FREE) {
            return &ctx->conds[i];
        }
        if ((recent == NULL) && (ctx->conds[i].state == COND_RECENT)) {
            recent = &ctx->conds[i];
        }
    }
    if (recent != NULL) {
        (void) memset(recent, 0, sizeof(*recent));
    }
    return recent;
}

/** @brief True if the timestamp guarded by @p has_flag allows a new report. */
static bool cond_repeat_ok(const iolink_event_cond_t* c, uint8_t has_flag, uint32_t then,
                           uint32_t now_ms)
{
    return ((c->flags & has_flag) == 0U) ||
           (elapsed_ms(now_ms, then) >= (uint32_t) IOLINK_EVENT_REPEAT_MS);
}

/** @brief Queue "appears" for an active condition when allowed. Caller holds lock. */
static void cond_try_appear(iolink_events_ctx_t* ctx, iolink_event_cond_t* c, uint32_t now_ms)
{
    if (!cond_repeat_ok(c, COND_HAS_APPEAR, c->appear_ms, now_ms)) {
        return;
    }
    if (queue_push(ctx, c->code, (iolink_event_type_t) c->type, IOLINK_EVENT_MODE_APPEARS,
                   EVQ_LIFECYCLE, false)) {
        c->flags |= (uint8_t) (COND_APPEAR_SENT | COND_HAS_APPEAR);
        c->appear_ms = now_ms;
    }
}

/** @brief Queue "disappears" for a cleared condition when allowed. Caller holds lock. */
static void cond_try_disappear(iolink_events_ctx_t* ctx, iolink_event_cond_t* c,
                               uint32_t now_ms)
{
    if (((c->flags & COND_HAS_APPEAR) != 0U) &&
        (elapsed_ms(now_ms, c->appear_ms) < (uint32_t) IOLINK_EVENT_MIN_APPEAR_MS)) {
        return;
    }
    if (!cond_repeat_ok(c, COND_HAS_DISAPPEAR, c->disappear_ms, now_ms)) {
        return;
    }
    if (queue_push(ctx, c->code, (iolink_event_type_t) c->type, IOLINK_EVENT_MODE_DISAPPEARS,
                   EVQ_LIFECYCLE, false)) {
        c->flags = (uint8_t) ((c->flags & COND_HAS_APPEAR) | COND_HAS_DISAPPEAR);
        c->disappear_ms = now_ms;
        c->state = COND_RECENT;
    }
}

bool iolink_event_appear_at(iolink_events_ctx_t* ctx, uint16_t code, iolink_event_type_t type,
                            uint32_t now_ms)
{
    if ((ctx == NULL) ||
        ((type != IOLINK_EVENT_TYPE_WARNING) && (type != IOLINK_EVENT_TYPE_ERROR))) {
        return false;
    }

    bool ok = true;
    iolink_critical_enter();
    iolink_event_cond_t* c = cond_find(ctx, code);
    if ((c != NULL) && (c->state == COND_ACTIVE)) {
        /* Same root cause: disregarded. A pending clear is withdrawn because
           the condition is present again. */
        c->flags &= (uint8_t) ~COND_CLEAR_REQ;
    }
    else {
        if (c == NULL) {
            c = cond_alloc(ctx);
        }
        if (c == NULL) {
            count_dropped(ctx);
            ok = false;
        }
        else {
            c->code = code;
            c->type = (uint8_t) type;
            c->state = COND_ACTIVE;
            c->flags &= (uint8_t) (COND_HAS_APPEAR | COND_HAS_DISAPPEAR);
            cond_try_appear(ctx, c, now_ms);
        }
    }
    iolink_critical_exit();
    return ok;
}

bool iolink_event_disappear_at(iolink_events_ctx_t* ctx, uint16_t code, uint32_t now_ms)
{
    if (ctx == NULL) {
        return false;
    }

    bool found = false;
    iolink_critical_enter();
    iolink_event_cond_t* c = cond_find(ctx, code);
    if ((c != NULL) && (c->state == COND_ACTIVE)) {
        found = true;
        if ((c->flags & COND_APPEAR_SENT) == 0U) {
            /* The master never saw "appears": withdraw without an Event. */
            c->flags &= (uint8_t) (COND_HAS_APPEAR | COND_HAS_DISAPPEAR);
            c->state = COND_RECENT;
            count_dropped(ctx);
        }
        else {
            c->flags |= COND_CLEAR_REQ;
            cond_try_disappear(ctx, c, now_ms);
        }
    }
    iolink_critical_exit();
    return found;
}

bool iolink_event_notify_at(iolink_events_ctx_t* ctx, uint16_t code, uint32_t now_ms)
{
    if (ctx == NULL) {
        return false;
    }

    bool queued = false;
    iolink_critical_enter();
    iolink_event_cond_t* c = cond_find(ctx, code);
    if (c == NULL) {
        c = cond_alloc(ctx);
    }
    if ((c != NULL) && (c->state != COND_ACTIVE) &&
        cond_repeat_ok(c, COND_HAS_APPEAR, c->appear_ms, now_ms)) {
        if (queue_push(ctx, code, IOLINK_EVENT_TYPE_NOTIFICATION, IOLINK_EVENT_MODE_SINGLE_SHOT,
                       0U, true)) {
            c->code = code;
            c->type = (uint8_t) IOLINK_EVENT_TYPE_NOTIFICATION;
            c->state = COND_RECENT;
            c->flags = COND_HAS_APPEAR;
            c->appear_ms = now_ms;
            queued = true;
        }
    }
    if (!queued) {
        count_dropped(ctx);
    }
    iolink_critical_exit();
    return queued;
}

void iolink_events_process_at(iolink_events_ctx_t* ctx, uint32_t now_ms)
{
    if (ctx == NULL) {
        return;
    }

    iolink_critical_enter();
    for (uint8_t i = 0U; i < IOLINK_EVENT_CONDITION_SLOTS; i++) {
        iolink_event_cond_t* c = &ctx->conds[i];
        if (c->state == COND_ACTIVE) {
            if ((c->flags & COND_APPEAR_SENT) == 0U) {
                cond_try_appear(ctx, c, now_ms);
            }
            else if ((c->flags & COND_CLEAR_REQ) != 0U) {
                cond_try_disappear(ctx, c, now_ms);
            }
            else {
                /* Active and reported: nothing to do. */
            }
        }
        /* Forget timestamps once their window has passed (both windows for
           "appears": repeat and minimum duration), so that a long active
           period cannot alias after a 32-bit wrap. */
        if (((c->flags & COND_HAS_APPEAR) != 0U) &&
            (elapsed_ms(now_ms, c->appear_ms) >= (uint32_t) IOLINK_EVENT_REPEAT_MS) &&
            (elapsed_ms(now_ms, c->appear_ms) >= (uint32_t) IOLINK_EVENT_MIN_APPEAR_MS)) {
            c->flags &= (uint8_t) ~COND_HAS_APPEAR;
        }
        if (((c->flags & COND_HAS_DISAPPEAR) != 0U) &&
            (elapsed_ms(now_ms, c->disappear_ms) >= (uint32_t) IOLINK_EVENT_REPEAT_MS)) {
            c->flags &= (uint8_t) ~COND_HAS_DISAPPEAR;
        }
        if ((c->state == COND_RECENT) &&
            ((c->flags & (COND_HAS_APPEAR | COND_HAS_DISAPPEAR)) == 0U)) {
            (void) memset(c, 0, sizeof(*c));
        }
    }
    iolink_critical_exit();
}

bool iolink_event_appear(iolink_events_ctx_t* ctx, uint16_t code, iolink_event_type_t type)
{
    return iolink_event_appear_at(ctx, code, type, iolink_time_get_ms());
}

bool iolink_event_disappear(iolink_events_ctx_t* ctx, uint16_t code)
{
    return iolink_event_disappear_at(ctx, code, iolink_time_get_ms());
}

bool iolink_event_notify(iolink_events_ctx_t* ctx, uint16_t code)
{
    return iolink_event_notify_at(ctx, code, iolink_time_get_ms());
}

void iolink_events_process(iolink_events_ctx_t* ctx)
{
    iolink_events_process_at(ctx, iolink_time_get_ms());
}

void iolink_events_startup_reset(iolink_events_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }

    iolink_critical_enter();
    /* 10.10.2: queued Events are discarded when communication is
       interrupted; the memory and the Event flag are released. */
    ctx->head = 0U;
    ctx->tail = 0U;
    ctx->count = 0U;
    ctx->frozen = false;
    ctx->flag = false;
    (void) memset(ctx->qflags, 0, sizeof(ctx->qflags));
    (void) memset(ctx->memory, 0, sizeof(ctx->memory));
    for (uint8_t i = 0U; i < IOLINK_EVENT_CONDITION_SLOTS; i++) {
        iolink_event_cond_t* c = &ctx->conds[i];
        if (c->state != COND_ACTIVE) {
            continue;
        }
        if ((c->flags & COND_CLEAR_REQ) != 0U) {
            /* Cleared while its "disappears" was still deferred: the new
               session never sees this condition. */
            c->state = COND_RECENT;
        }
        /* Active conditions report "appears" again after communication
           resumes (subject to the repeat interval). */
        c->flags &= (uint8_t) ~(COND_APPEAR_SENT | COND_CLEAR_REQ);
    }
    iolink_critical_exit();
}

uint8_t iolink_event_device_status(uint16_t code, iolink_event_type_t type)
{
    if (type == IOLINK_EVENT_TYPE_ERROR) {
        return IOLINK_DEVICE_STATUS_FAILURE;
    }
    if (type != IOLINK_EVENT_TYPE_WARNING) {
        return IOLINK_DEVICE_STATUS_OK;
    }
    switch (code) {
        case IOLINK_EVENTCODE_SIMULATION_ACTIVE:
            return IOLINK_DEVICE_STATUS_FUNCTIONAL_CHECK;
        case IOLINK_EVENTCODE_MAINTENANCE_CLEANING:
        case IOLINK_EVENTCODE_MAINTENANCE_REFILL:
        case IOLINK_EVENTCODE_MAINTENANCE_WEAR:
            return IOLINK_DEVICE_STATUS_MAINTENANCE;
        default:
            return IOLINK_DEVICE_STATUS_OUT_OF_SPEC;
    }
}

/** @brief Table B.13 rank of a DeviceStatus value: higher dominates. */
static uint8_t device_status_rank(uint8_t status)
{
    static const uint8_t rank[5] = {0U, 2U, 1U, 3U, 4U};
    return (status < 5U) ? rank[status] : 0U;
}

/** @brief True for a queued legacy Warning/Error (not owned by a condition). */
static bool queue_entry_is_legacy_status(const iolink_events_ctx_t* ctx, uint8_t idx)
{
    const iolink_event_t* ev = &ctx->queue[idx];
    return ((ctx->qflags[idx] & EVQ_LIFECYCLE) == 0U) &&
           ((ev->mode == IOLINK_EVENT_MODE_LEGACY) || (ev->mode == IOLINK_EVENT_MODE_APPEARS)) &&
           ((ev->type == IOLINK_EVENT_TYPE_WARNING) || (ev->type == IOLINK_EVENT_TYPE_ERROR));
}

uint8_t iolink_events_get_device_status(iolink_events_ctx_t* ctx)
{
    if (ctx == NULL) {
        return IOLINK_DEVICE_STATUS_OK;
    }

    uint8_t best = IOLINK_DEVICE_STATUS_OK;
    iolink_critical_enter();
    for (uint8_t i = 0U; i < IOLINK_EVENT_CONDITION_SLOTS; i++) {
        const iolink_event_cond_t* c = &ctx->conds[i];
        if (c->state == COND_ACTIVE) {
            uint8_t st = iolink_event_device_status(c->code, (iolink_event_type_t) c->type);
            if (device_status_rank(st) > device_status_rank(best)) {
                best = st;
            }
        }
    }
    for (uint8_t i = 0U; i < ctx->count; i++) {
        uint8_t idx = queue_slot(ctx, i);
        if (queue_entry_is_legacy_status(ctx, idx)) {
            uint8_t st = iolink_event_device_status(ctx->queue[idx].code, ctx->queue[idx].type);
            if (device_status_rank(st) > device_status_rank(best)) {
                best = st;
            }
        }
    }
    iolink_critical_exit();
    return best;
}

uint16_t iolink_events_get_detailed_status(iolink_events_ctx_t* ctx, uint8_t* out,
                                           uint16_t capacity)
{
    if ((ctx == NULL) || (out == NULL)) {
        return 0U;
    }

    uint16_t len = 0U;
    iolink_critical_enter();
    for (uint8_t i = 0U; i < IOLINK_EVENT_CONDITION_SLOTS; i++) {
        const iolink_event_cond_t* c = &ctx->conds[i];
        if ((c->state == COND_ACTIVE) && ((uint16_t) (capacity - len) >= 3U)) {
            out[len] = event_qualifier((iolink_event_type_t) c->type, IOLINK_EVENT_MODE_APPEARS);
            out[len + 1U] = (uint8_t) (c->code >> 8);
            out[len + 2U] = (uint8_t) (c->code & 0xFFU);
            len = (uint16_t) (len + 3U);
        }
    }
    for (uint8_t i = 0U; i < ctx->count; i++) {
        uint8_t idx = queue_slot(ctx, i);
        if (queue_entry_is_legacy_status(ctx, idx) && ((uint16_t) (capacity - len) >= 3U)) {
            const iolink_event_t* ev = &ctx->queue[idx];
            out[len] = event_qualifier(ev->type, IOLINK_EVENT_MODE_APPEARS);
            out[len + 1U] = (uint8_t) (ev->code >> 8);
            out[len + 2U] = (uint8_t) (ev->code & 0xFFU);
            len = (uint16_t) (len + 3U);
        }
    }
    iolink_critical_exit();
    return len;
}

uint32_t iolink_events_dropped(const iolink_events_ctx_t* ctx)
{
    return (ctx != NULL) ? ctx->dropped : 0U;
}
