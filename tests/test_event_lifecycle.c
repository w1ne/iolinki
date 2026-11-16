/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_event_lifecycle.c
 * @brief Event Dispatcher regression tests (IO-Link V1.1.5 10.10.2, Table 58,
 *        Table B.13, Table B.14).
 *
 * Covers the appear/disappear lifecycle, single-shot Notifications, the 50 ms
 * minimum appear duration, the 1 s repeat interval, time zero and 32-bit
 * wrap-around, the frozen event memory and its confirmation, DeviceStatus,
 * DetailedDeviceStatus, STARTUP reset, NULL contexts and lock nesting.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <string.h>
#include <cmocka.h>

#include "iolinki/device_info.h"
#include "iolinki/dll.h"
#include "iolinki/events.h"
#include "iolinki/params.h"
#include "iolinki/platform.h"
#include "iolinki/time_utils.h"
#include "test_helpers.h"

/* Controlled clock for the DLL tests. */
static uint64_t g_now_us;

uint32_t iolink_time_get_ms(void)
{
    return (uint32_t) (g_now_us / 1000U);
}

uint64_t iolink_time_get_us(void)
{
    return g_now_us;
}

/* Lock instrumentation: every public call must take the lock without nesting. */
static int g_lock_depth;
static int g_lock_max_depth;

void iolink_critical_enter(void)
{
    g_lock_depth++;
    if (g_lock_depth > g_lock_max_depth) {
        g_lock_max_depth = g_lock_depth;
    }
}

void iolink_critical_exit(void)
{
    g_lock_depth--;
}

static int reset_lock(void** state)
{
    (void) state;
    g_lock_depth = 0;
    g_lock_max_depth = 0;
    g_now_us = 0U;
    return 0;
}

static int check_lock(void** state)
{
    (void) state;
    assert_int_equal(g_lock_depth, 0);
    assert_true(g_lock_max_depth <= 1);
    return 0;
}

/* Qualifiers: MODE | TYPE | SOURCE=device | INSTANCE=Application (A.6.4). */
#define Q_APPEAR_ERR 0xF4U
#define Q_APPEAR_WARN 0xE4U
#define Q_DISAPPEAR_ERR 0xB4U
#define Q_DISAPPEAR_WARN 0xA4U
#define Q_SINGLE_NOTE 0x54U

/** Queue entry @p i in FIFO order. */
static iolink_event_t queued(const iolink_events_ctx_t* ctx, uint8_t i)
{
    return ctx->queue[(ctx->head + i) % IOLINK_EVENT_QUEUE_SIZE];
}

/** Event memory slot @p n (1..6): qualifier and code. */
static void expect_slot(iolink_events_ctx_t* ctx, uint8_t n, uint8_t qualifier, uint16_t code)
{
    uint8_t base = (uint8_t) (3U * n - 2U);
    assert_int_equal(iolink_events_memory_read(ctx, base), qualifier);
    assert_int_equal(iolink_events_memory_read(ctx, (uint8_t) (base + 1U)), code >> 8);
    assert_int_equal(iolink_events_memory_read(ctx, (uint8_t) (base + 2U)), code & 0xFFU);
}

static void test_appear_disappear_lifecycle(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    assert_true(iolink_event_appear_at(&ctx, IOLINK_EVENTCODE_SHORT_CIRCUIT,
                                       IOLINK_EVENT_TYPE_ERROR, 1000U));
    assert_int_equal(ctx.count, 1U);
    assert_int_equal(queued(&ctx, 0).mode, IOLINK_EVENT_MODE_APPEARS);
    assert_true(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_get_device_status(&ctx), IOLINK_DEVICE_STATUS_FAILURE);

    /* Same root cause again: disregarded. */
    assert_true(iolink_event_appear_at(&ctx, IOLINK_EVENTCODE_SHORT_CIRCUIT,
                                       IOLINK_EVENT_TYPE_ERROR, 1005U));
    assert_int_equal(ctx.count, 1U);

    /* Clear after 10 ms: deferred, condition still active. */
    assert_true(iolink_event_disappear_at(&ctx, IOLINK_EVENTCODE_SHORT_CIRCUIT, 1010U));
    assert_int_equal(ctx.count, 1U);
    assert_int_equal(iolink_events_get_device_status(&ctx), IOLINK_DEVICE_STATUS_FAILURE);
    uint8_t buf[12];
    assert_int_equal(iolink_events_get_detailed_status(&ctx, buf, sizeof(buf)), 3U);

    iolink_events_process_at(&ctx, 1049U);
    assert_int_equal(ctx.count, 1U);
    iolink_events_process_at(&ctx, 1050U);
    assert_int_equal(ctx.count, 2U);
    assert_int_equal(queued(&ctx, 1).mode, IOLINK_EVENT_MODE_DISAPPEARS);
    assert_int_equal(iolink_events_get_device_status(&ctx), IOLINK_DEVICE_STATUS_OK);
    assert_int_equal(iolink_events_get_detailed_status(&ctx, buf, sizeof(buf)), 0U);

    /* Memory: "appears" then "disappears" for the same code. */
    assert_int_equal(iolink_events_memory_read(&ctx, 0U), 0x83U);
    expect_slot(&ctx, 1U, Q_APPEAR_ERR, IOLINK_EVENTCODE_SHORT_CIRCUIT);
    expect_slot(&ctx, 2U, Q_DISAPPEAR_ERR, IOLINK_EVENTCODE_SHORT_CIRCUIT);

    /* Unknown code and second clear are no-ops. */
    assert_false(iolink_event_disappear_at(&ctx, 0x1234U, 1100U));
    assert_false(iolink_event_disappear_at(&ctx, IOLINK_EVENTCODE_SHORT_CIRCUIT, 1100U));
    assert_int_equal(iolink_events_dropped(&ctx), 0U);
}

static void test_clear_after_min_duration_is_immediate(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    assert_true(iolink_event_appear_at(&ctx, 0x8C10U, IOLINK_EVENT_TYPE_WARNING, 200U));
    assert_true(iolink_event_disappear_at(&ctx, 0x8C10U, 250U));
    assert_int_equal(ctx.count, 2U);
    assert_int_equal(queued(&ctx, 1).mode, IOLINK_EVENT_MODE_DISAPPEARS);
    expect_slot(&ctx, 2U, Q_DISAPPEAR_WARN, 0x8C10U);
}

static void test_notification_single_shot_and_repeat(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    /* Errors/Warnings only for appear; a Notification is single shot. */
    assert_false(iolink_event_appear_at(&ctx, 0x0000U, IOLINK_EVENT_TYPE_NOTIFICATION, 0U));
    assert_int_equal(ctx.count, 0U);

    assert_true(iolink_event_notify_at(&ctx, 0x8CA1U, 5000U));
    assert_int_equal(queued(&ctx, 0).mode, IOLINK_EVENT_MODE_SINGLE_SHOT);
    expect_slot(&ctx, 1U, Q_SINGLE_NOTE, 0x8CA1U);
    /* Notifications do not change DeviceStatus or DetailedDeviceStatus. */
    assert_int_equal(iolink_events_get_device_status(&ctx), IOLINK_DEVICE_STATUS_OK);
    uint8_t buf[6];
    assert_int_equal(iolink_events_get_detailed_status(&ctx, buf, sizeof(buf)), 0U);

    /* Same code within 1 s: suppressed and counted. */
    assert_false(iolink_event_notify_at(&ctx, 0x8CA1U, 5999U));
    assert_int_equal(iolink_events_dropped(&ctx), 1U);
    assert_int_equal(ctx.count, 1U);

    /* Another code is independent. */
    assert_true(iolink_event_notify_at(&ctx, 0x8CA2U, 5999U));

    assert_true(iolink_event_notify_at(&ctx, 0x8CA1U, 6000U));
    assert_int_equal(ctx.count, 3U);
}

static void test_reappear_respects_repeat_interval(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    assert_true(iolink_event_appear_at(&ctx, 0x8C10U, IOLINK_EVENT_TYPE_WARNING, 0U));
    assert_true(iolink_event_disappear_at(&ctx, 0x8C10U, 100U));
    assert_int_equal(ctx.count, 2U);

    /* Re-appears 500 ms after the last "appears": status is active at once,
       the Event is held back until 1 s after the previous report. */
    assert_true(iolink_event_appear_at(&ctx, 0x8C10U, IOLINK_EVENT_TYPE_WARNING, 500U));
    assert_int_equal(ctx.count, 2U);
    assert_int_equal(iolink_events_get_device_status(&ctx), IOLINK_DEVICE_STATUS_OUT_OF_SPEC);
    iolink_events_process_at(&ctx, 999U);
    assert_int_equal(ctx.count, 2U);
    iolink_events_process_at(&ctx, 1000U);
    assert_int_equal(ctx.count, 3U);
    assert_int_equal(queued(&ctx, 2).mode, IOLINK_EVENT_MODE_APPEARS);

    /* "disappears" is also rate limited: last one at 100 ms. */
    assert_true(iolink_event_disappear_at(&ctx, 0x8C10U, 1060U));
    assert_int_equal(ctx.count, 3U);
    iolink_events_process_at(&ctx, 1099U);
    assert_int_equal(ctx.count, 3U);
    iolink_events_process_at(&ctx, 1100U);
    assert_int_equal(ctx.count, 4U);
    assert_int_equal(queued(&ctx, 3).mode, IOLINK_EVENT_MODE_DISAPPEARS);
}

static void test_reappear_cancels_pending_clear(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    assert_true(iolink_event_appear_at(&ctx, 0x7700U, IOLINK_EVENT_TYPE_ERROR, 10U));
    assert_true(iolink_event_disappear_at(&ctx, 0x7700U, 20U));
    assert_true(iolink_event_appear_at(&ctx, 0x7700U, IOLINK_EVENT_TYPE_ERROR, 30U));
    iolink_events_process_at(&ctx, 500U);
    assert_int_equal(ctx.count, 1U); /* No "disappears", no second "appears" */
    assert_int_equal(iolink_events_get_device_status(&ctx), IOLINK_DEVICE_STATUS_FAILURE);
}

static void test_time_zero(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    /* t = 0 is a valid timestamp, not "never". */
    assert_true(iolink_event_appear_at(&ctx, 0x7700U, IOLINK_EVENT_TYPE_ERROR, 0U));
    assert_int_equal(ctx.count, 1U);
    assert_true(iolink_event_disappear_at(&ctx, 0x7700U, 0U));
    iolink_events_process_at(&ctx, 0U);
    iolink_events_process_at(&ctx, 49U);
    assert_int_equal(ctx.count, 1U);
    iolink_events_process_at(&ctx, 50U);
    assert_int_equal(ctx.count, 2U);

    /* Notification at t = 0 is rate limited against itself. */
    assert_true(iolink_event_notify_at(&ctx, 0x8CA1U, 0U));
    assert_false(iolink_event_notify_at(&ctx, 0x8CA1U, 0U));
}

static void test_time_rollover(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    /* Appear 32 ms before the 32-bit wrap, clear 16 ms after it (48 ms). */
    assert_true(iolink_event_appear_at(&ctx, 0x7700U, IOLINK_EVENT_TYPE_ERROR, 0xFFFFFFE0U));
    assert_true(iolink_event_disappear_at(&ctx, 0x7700U, 0x00000010U));
    assert_int_equal(ctx.count, 1U);
    iolink_events_process_at(&ctx, 0x00000011U);
    assert_int_equal(ctx.count, 1U);
    iolink_events_process_at(&ctx, 0x00000012U); /* 50 ms elapsed */
    assert_int_equal(ctx.count, 2U);

    /* Repeat interval across the wrap. */
    assert_true(iolink_event_notify_at(&ctx, 0x8CA1U, 0xFFFFFF00U));
    assert_false(iolink_event_notify_at(&ctx, 0x8CA1U, 0x000002E7U)); /* 999 ms */
    assert_true(iolink_event_notify_at(&ctx, 0x8CA1U, 0x000002E8U));  /* 1000 ms */

    /* A condition active for longer than half the 32-bit range still clears
       at once if the dispatcher was serviced in between. */
    iolink_events_init(&ctx);
    assert_true(iolink_event_appear_at(&ctx, 0x4210U, IOLINK_EVENT_TYPE_WARNING, 100U));
    iolink_events_process_at(&ctx, 2000U);
    assert_true(iolink_event_disappear_at(&ctx, 0x4210U, 100U + 0x80000000U + 0x80000000U + 10U));
    assert_int_equal(ctx.count, 2U);
}

static void test_memory_freeze_and_exact_ack_with_priority(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    /* Seven conditions, mixed priority, in this arrival order. */
    const uint16_t w1 = 0x8C10U, e1 = 0x7700U, w2 = 0x8C30U, e2 = 0x7710U;
    const uint16_t w3 = 0x4210U, e3 = 0x7711U, w4 = 0x4220U, e4 = 0x5000U;
    assert_true(iolink_event_appear_at(&ctx, w1, IOLINK_EVENT_TYPE_WARNING, 0U));
    assert_true(iolink_event_appear_at(&ctx, e1, IOLINK_EVENT_TYPE_ERROR, 0U));
    assert_true(iolink_event_appear_at(&ctx, w2, IOLINK_EVENT_TYPE_WARNING, 0U));
    assert_true(iolink_event_appear_at(&ctx, e2, IOLINK_EVENT_TYPE_ERROR, 0U));
    assert_true(iolink_event_appear_at(&ctx, w3, IOLINK_EVENT_TYPE_WARNING, 0U));
    assert_true(iolink_event_appear_at(&ctx, e3, IOLINK_EVENT_TYPE_ERROR, 0U));
    assert_true(iolink_event_appear_at(&ctx, w4, IOLINK_EVENT_TYPE_WARNING, 0U));
    assert_int_equal(ctx.count, 7U);

    /* First read freezes six slots: errors first, then warnings (FIFO). */
    assert_int_equal(iolink_events_memory_read(&ctx, 0U), 0xBFU);
    expect_slot(&ctx, 1U, Q_APPEAR_ERR, e1);
    expect_slot(&ctx, 2U, Q_APPEAR_ERR, e2);
    expect_slot(&ctx, 3U, Q_APPEAR_ERR, e3);
    expect_slot(&ctx, 4U, Q_APPEAR_WARN, w1);
    expect_slot(&ctx, 5U, Q_APPEAR_WARN, w2);
    expect_slot(&ctx, 6U, Q_APPEAR_WARN, w3);

    /* A higher-priority Event arrives while frozen: memory is unchanged. */
    assert_true(iolink_event_appear_at(&ctx, e4, IOLINK_EVENT_TYPE_ERROR, 10U));
    assert_int_equal(ctx.count, 8U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0U), 0xBFU);
    expect_slot(&ctx, 1U, Q_APPEAR_ERR, e1);
    expect_slot(&ctx, 6U, Q_APPEAR_WARN, w3);

    /* Confirmation removes exactly the six presented Events. w4 (never shown)
       and e4 (arrived while frozen) remain and are presented next. */
    iolink_events_memory_write(&ctx, 0U, 0x00U);
    assert_int_equal(ctx.count, 2U);
    assert_true(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0U), 0x83U);
    expect_slot(&ctx, 1U, Q_APPEAR_ERR, e4);
    expect_slot(&ctx, 2U, Q_APPEAR_WARN, w4);

    iolink_events_memory_write(&ctx, 0U, 0x00U);
    assert_int_equal(ctx.count, 0U);
    assert_false(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0U), 0x00U);

    /* All eight conditions are still active (no "disappears" yet). */
    uint8_t buf[64];
    assert_int_equal(iolink_events_get_detailed_status(&ctx, buf, sizeof(buf)), 24U);
    assert_int_equal(iolink_events_dropped(&ctx), 0U);
}

static void test_disappear_while_frozen_survives_ack(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    assert_true(iolink_event_appear_at(&ctx, 0x7700U, IOLINK_EVENT_TYPE_ERROR, 0U));
    assert_int_equal(iolink_events_memory_read(&ctx, 0U), 0x81U);
    assert_true(iolink_event_disappear_at(&ctx, 0x7700U, 60U));
    assert_int_equal(iolink_events_memory_read(&ctx, 0U), 0x81U);

    iolink_events_memory_write(&ctx, 0U, 0x00U);
    assert_int_equal(ctx.count, 1U);
    assert_true(iolink_events_flag(&ctx));
    expect_slot(&ctx, 1U, Q_DISAPPEAR_ERR, 0x7700U);
}

static void test_queue_full_policy(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    for (uint16_t i = 0U; i < IOLINK_EVENT_QUEUE_SIZE; i++) {
        iolink_event_trigger(&ctx, (uint16_t) (0x1800U + i), IOLINK_EVENT_TYPE_WARNING);
    }
    /* Freeze: the six oldest are presented and protected. */
    assert_int_equal(iolink_events_memory_read(&ctx, 0U), 0xBFU);

    /* Legacy overflow evicts the oldest unpresented entry and counts it. */
    iolink_event_trigger(&ctx, 0x18F0U, IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(ctx.count, IOLINK_EVENT_QUEUE_SIZE);
    assert_int_equal(iolink_events_dropped(&ctx), 1U);
    expect_slot(&ctx, 1U, Q_APPEAR_WARN, 0x1800U);

    /* A condition does not evict: its "appears" waits, status is active. */
    assert_true(iolink_event_appear_at(&ctx, 0x7700U, IOLINK_EVENT_TYPE_ERROR, 0U));
    assert_int_equal(ctx.count, IOLINK_EVENT_QUEUE_SIZE);
    assert_int_equal(iolink_events_get_device_status(&ctx), IOLINK_DEVICE_STATUS_FAILURE);

    /* Confirmation removes the six presented entries only. */
    iolink_events_memory_write(&ctx, 0U, 0x00U);
    assert_int_equal(ctx.count, IOLINK_EVENT_QUEUE_SIZE - 6U);
    iolink_event_t ev = queued(&ctx, (uint8_t) (ctx.count - 1U));
    assert_int_equal(ev.code, 0x18F0U);

    /* The deferred "appears" is queued on the next service call. */
    iolink_events_process_at(&ctx, 1U);
    ev = queued(&ctx, (uint8_t) (ctx.count - 1U));
    assert_int_equal(ev.code, 0x7700U);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_APPEARS);
}

static void test_condition_table_full(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    for (uint16_t i = 0U; i < IOLINK_EVENT_CONDITION_SLOTS; i++) {
        assert_true(iolink_event_appear_at(&ctx, (uint16_t) (0x8D00U + i),
                                           IOLINK_EVENT_TYPE_WARNING, 0U));
    }
    assert_false(iolink_event_appear_at(&ctx, 0x8DFFU, IOLINK_EVENT_TYPE_ERROR, 0U));
    assert_int_equal(iolink_events_dropped(&ctx), 1U);
}

static void test_device_status_values(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    assert_int_equal(iolink_events_get_device_status(&ctx), 0U);

    assert_true(iolink_event_appear_at(&ctx, 0x8C10U, IOLINK_EVENT_TYPE_WARNING, 0U));
    assert_int_equal(iolink_events_get_device_status(&ctx), 2U); /* Out-of-spec */

    /* Table B.13: Maintenance (priority 3) dominates Out-of-spec (priority 4). */
    assert_true(iolink_event_appear_at(&ctx, IOLINK_EVENTCODE_MAINTENANCE_CLEANING,
                                       IOLINK_EVENT_TYPE_WARNING, 0U));
    assert_int_equal(iolink_events_get_device_status(&ctx), 1U);

    assert_true(iolink_event_appear_at(&ctx, IOLINK_EVENTCODE_SIMULATION_ACTIVE,
                                       IOLINK_EVENT_TYPE_WARNING, 0U));
    assert_int_equal(iolink_events_get_device_status(&ctx), 3U); /* Functional check */

    assert_true(iolink_event_appear_at(&ctx, 0x5000U, IOLINK_EVENT_TYPE_ERROR, 0U));
    assert_int_equal(iolink_events_get_device_status(&ctx), 4U); /* Failure */

    /* Clearing walks back down through the priorities. */
    assert_true(iolink_event_disappear_at(&ctx, 0x5000U, 100U));
    assert_int_equal(iolink_events_get_device_status(&ctx), 3U);
    assert_true(iolink_event_disappear_at(&ctx, IOLINK_EVENTCODE_SIMULATION_ACTIVE, 100U));
    assert_int_equal(iolink_events_get_device_status(&ctx), 1U);
    assert_true(iolink_event_disappear_at(&ctx, IOLINK_EVENTCODE_MAINTENANCE_CLEANING, 100U));
    assert_int_equal(iolink_events_get_device_status(&ctx), 2U);
    assert_true(iolink_event_disappear_at(&ctx, 0x8C10U, 100U));
    assert_int_equal(iolink_events_get_device_status(&ctx), 0U);

    assert_int_equal(iolink_event_device_status(0x0000U, IOLINK_EVENT_TYPE_NOTIFICATION), 0U);
}

static void test_detailed_status_no_stale_and_bounded(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    assert_true(iolink_event_appear_at(&ctx, 0x7700U, IOLINK_EVENT_TYPE_ERROR, 0U));
    assert_true(iolink_event_appear_at(&ctx, 0x8C10U, IOLINK_EVENT_TYPE_WARNING, 0U));
    assert_true(iolink_event_appear_at(&ctx, 0x5000U, IOLINK_EVENT_TYPE_ERROR, 0U));

    uint8_t buf[16];
    (void) memset(buf, 0xA5, sizeof(buf));
    assert_int_equal(iolink_events_get_detailed_status(&ctx, buf, sizeof(buf)), 9U);
    const uint8_t all[9] = {Q_APPEAR_ERR, 0x77U, 0x00U, Q_APPEAR_WARN, 0x8CU,
                            0x10U,        Q_APPEAR_ERR, 0x50U, 0x00U};
    assert_memory_equal(buf, all, sizeof(all));

    /* Clear the middle entry: the list has no stale record after it. */
    assert_true(iolink_event_disappear_at(&ctx, 0x8C10U, 100U));
    (void) memset(buf, 0xA5, sizeof(buf));
    assert_int_equal(iolink_events_get_detailed_status(&ctx, buf, sizeof(buf)), 6U);
    const uint8_t two[6] = {Q_APPEAR_ERR, 0x77U, 0x00U, Q_APPEAR_ERR, 0x50U, 0x00U};
    assert_memory_equal(buf, two, sizeof(two));
    assert_int_equal(buf[6], 0xA5U);

    /* Capacity of 5 octets: one whole record, nothing beyond the bound. */
    (void) memset(buf, 0xA5, sizeof(buf));
    assert_int_equal(iolink_events_get_detailed_status(&ctx, buf, 5U), 3U);
    assert_int_equal(buf[3], 0xA5U);
    assert_int_equal(buf[4], 0xA5U);
    assert_int_equal(iolink_events_get_detailed_status(&ctx, buf, 2U), 0U);
}

static void test_isdu_status_indices(void** state)
{
    (void) state;
    iolink_isdu_ctx_t isdu;
    iolink_events_ctx_t events;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_events_init(&events);

    assert_true(iolink_event_appear_at(&events, 0x7700U, IOLINK_EVENT_TYPE_ERROR, 0U));
    assert_true(iolink_event_appear_at(&events, 0x8C10U, IOLINK_EVENT_TYPE_WARNING, 0U));
    assert_true(iolink_event_disappear_at(&events, 0x7700U, 60U));

    uint8_t resp[16];
    iolink_isdu_init(&isdu);
    isdu.event_ctx = &events;
    assert_int_equal(isdu_send_read_request(&isdu, IOLINK_IDX_DEVICE_STATUS, 0x00), 1);
    iolink_isdu_process(&isdu);
    assert_int_equal(isdu_collect_response(&isdu, resp, sizeof(resp)), 1);
    assert_int_equal(resp[0], IOLINK_DEVICE_STATUS_OUT_OF_SPEC);

    iolink_isdu_init(&isdu);
    isdu.event_ctx = &events;
    assert_int_equal(isdu_send_read_request(&isdu, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0x00), 1);
    iolink_isdu_process(&isdu);
    assert_int_equal(isdu_collect_response(&isdu, resp, sizeof(resp)), 3);
    assert_int_equal(resp[0], Q_APPEAR_WARN);
    assert_int_equal(resp[1], 0x8CU);
    assert_int_equal(resp[2], 0x10U);
}

static void test_startup_reset(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    assert_true(iolink_event_appear_at(&ctx, 0x7700U, IOLINK_EVENT_TYPE_ERROR, 0U));
    assert_true(iolink_event_appear_at(&ctx, 0x8C10U, IOLINK_EVENT_TYPE_WARNING, 0U));
    assert_true(iolink_event_disappear_at(&ctx, 0x8C10U, 10U)); /* deferred */
    iolink_event_trigger(&ctx, 0x1801U, IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(iolink_events_memory_read(&ctx, 0U), 0x87U);

    iolink_events_startup_reset(&ctx);
    assert_false(iolink_events_pending(&ctx));
    assert_false(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0U), 0x00U);
    /* The active Error stays active; the cleared Warning is withdrawn. */
    assert_int_equal(iolink_events_get_device_status(&ctx), IOLINK_DEVICE_STATUS_FAILURE);
    uint8_t buf[12];
    assert_int_equal(iolink_events_get_detailed_status(&ctx, buf, sizeof(buf)), 3U);

    /* "appears" is reported again, not sooner than 1 s after the last one. */
    iolink_events_process_at(&ctx, 999U);
    assert_int_equal(ctx.count, 0U);
    iolink_events_process_at(&ctx, 1000U);
    assert_int_equal(ctx.count, 1U);
    assert_int_equal(queued(&ctx, 0).code, 0x7700U);
    assert_int_equal(queued(&ctx, 0).mode, IOLINK_EVENT_MODE_APPEARS);
}

static void test_null_context(void** state)
{
    (void) state;
    uint8_t buf[6];
    iolink_event_t ev;

    iolink_events_init(NULL);
    iolink_event_trigger(NULL, 0x1801U, IOLINK_EVENT_TYPE_ERROR);
    assert_false(iolink_event_appear_at(NULL, 0x7700U, IOLINK_EVENT_TYPE_ERROR, 0U));
    assert_false(iolink_event_disappear_at(NULL, 0x7700U, 0U));
    assert_false(iolink_event_notify_at(NULL, 0x8CA1U, 0U));
    assert_false(iolink_event_appear(NULL, 0x7700U, IOLINK_EVENT_TYPE_ERROR));
    assert_false(iolink_event_disappear(NULL, 0x7700U));
    assert_false(iolink_event_notify(NULL, 0x8CA1U));
    iolink_events_process_at(NULL, 0U);
    iolink_events_process(NULL);
    iolink_events_startup_reset(NULL);
    iolink_events_memory_write(NULL, 0U, 0U);
    assert_int_equal(iolink_events_memory_read(NULL, 0U), 0U);
    assert_false(iolink_events_flag(NULL));
    assert_false(iolink_events_pending(NULL));
    assert_false(iolink_events_pop(NULL, &ev));
    assert_false(iolink_events_peek(NULL, &ev));
    assert_int_equal(iolink_events_get_device_status(NULL), 0U);
    assert_int_equal(iolink_events_get_detailed_status(NULL, buf, sizeof(buf)), 0U);
    assert_int_equal(iolink_events_dropped(NULL), 0U);
    assert_null(iolink_device_get_events_ctx(NULL));
    assert_false(iolink_event_appear(iolink_device_get_events_ctx(NULL), 0x7700U,
                                     IOLINK_EVENT_TYPE_ERROR));

    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);
    assert_int_equal(iolink_events_get_detailed_status(&ctx, NULL, 6U), 0U);
}

static void test_dropped_counter_saturates(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    ctx.dropped = UINT32_MAX - 1U;
    assert_true(iolink_event_notify_at(&ctx, 0x8CA1U, 0U));
    assert_false(iolink_event_notify_at(&ctx, 0x8CA1U, 1U));
    assert_false(iolink_event_notify_at(&ctx, 0x8CA1U, 2U));
    assert_int_equal(iolink_events_dropped(&ctx), UINT32_MAX);
}

/* Minimal PHY without wake-up detection: the DLL starts in SDCI. */
static int dll_recv_none(void* user, uint8_t* byte)
{
    (void) user;
    (void) byte;
    return 0;
}

static void test_dll_services_deferred_clear_and_startup(void** state)
{
    (void) state;
    const iolink_phy_api_t phy = {.recv_byte = dll_recv_none};
    iolink_dll_ctx_t dll;
    g_now_us = 5000000U; /* 5 s */
    iolink_dll_init(&dll, &phy);

    iolink_events_ctx_t* ev = &dll.events;
    assert_true(iolink_event_appear(ev, 0x7700U, IOLINK_EVENT_TYPE_ERROR));
    g_now_us += 10000U;
    assert_true(iolink_event_disappear(ev, 0x7700U));
    assert_int_equal(ev->count, 1U);

    /* The DLL process loop services the deferred clear. */
    g_now_us += 39000U; /* 49 ms */
    iolink_dll_process(&dll);
    assert_int_equal(ev->count, 1U);
    g_now_us += 1000U; /* 50 ms */
    iolink_dll_process(&dll);
    assert_int_equal(ev->count, 2U);
    assert_int_equal(queued(ev, 1).mode, IOLINK_EVENT_MODE_DISAPPEARS);

    /* Link inactivity drops the DLL to STARTUP: queued Events are discarded,
       the active condition stays active. */
    assert_true(iolink_event_appear(ev, 0x5000U, IOLINK_EVENT_TYPE_ERROR));
    dll.state = IOLINK_DLL_STATE_OPERATE;
    dll.last_activity_ms = iolink_time_get_ms();
    g_now_us += 1001000U;
    iolink_dll_process(&dll);
    assert_int_equal(dll.state, IOLINK_DLL_STATE_STARTUP);
    assert_false(iolink_events_flag(ev));
    assert_int_equal(iolink_events_get_device_status(ev), IOLINK_DEVICE_STATUS_FAILURE);

    /* Once the repeat interval has passed, "appears" is reported again. */
    iolink_dll_process(&dll);
    assert_int_equal(ev->count, 1U);
    assert_int_equal(queued(ev, 0).code, 0x5000U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_appear_disappear_lifecycle, reset_lock, check_lock),
        cmocka_unit_test_setup_teardown(test_clear_after_min_duration_is_immediate, reset_lock,
                                        check_lock),
        cmocka_unit_test_setup_teardown(test_notification_single_shot_and_repeat, reset_lock,
                                        check_lock),
        cmocka_unit_test_setup_teardown(test_reappear_respects_repeat_interval, reset_lock,
                                        check_lock),
        cmocka_unit_test_setup_teardown(test_reappear_cancels_pending_clear, reset_lock,
                                        check_lock),
        cmocka_unit_test_setup_teardown(test_time_zero, reset_lock, check_lock),
        cmocka_unit_test_setup_teardown(test_time_rollover, reset_lock, check_lock),
        cmocka_unit_test_setup_teardown(test_memory_freeze_and_exact_ack_with_priority,
                                        reset_lock, check_lock),
        cmocka_unit_test_setup_teardown(test_disappear_while_frozen_survives_ack, reset_lock,
                                        check_lock),
        cmocka_unit_test_setup_teardown(test_queue_full_policy, reset_lock, check_lock),
        cmocka_unit_test_setup_teardown(test_condition_table_full, reset_lock, check_lock),
        cmocka_unit_test_setup_teardown(test_device_status_values, reset_lock, check_lock),
        cmocka_unit_test_setup_teardown(test_detailed_status_no_stale_and_bounded, reset_lock,
                                        check_lock),
        cmocka_unit_test_setup_teardown(test_isdu_status_indices, reset_lock, check_lock),
        cmocka_unit_test_setup_teardown(test_startup_reset, reset_lock, check_lock),
        cmocka_unit_test_setup_teardown(test_null_context, reset_lock, check_lock),
        cmocka_unit_test_setup_teardown(test_dropped_counter_saturates, reset_lock, check_lock),
        cmocka_unit_test_setup_teardown(test_dll_services_deferred_clear_and_startup,
                                        reset_lock, check_lock),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
