/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_events.c
 * @brief Unit tests for IO-Link Event handling
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>

#include "iolinki/events.h"

static void test_event_queue_flow(void **state)
{
    (void)state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    assert_false(iolink_events_pending(&ctx));

    iolink_event_trigger(&ctx, 0x1234, IOLINK_EVENT_TYPE_WARNING);
    assert_true(iolink_events_pending(&ctx));

    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, 0x1234);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_WARNING);
    assert_false(iolink_events_pending(&ctx));
}

static void test_event_queue_overflow(void **state)
{
    (void)state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    /* Fill queue (size 8 by default) */
    for (uint16_t i = 0U; i < 8U; i++) {
        iolink_event_trigger(&ctx, i, IOLINK_EVENT_TYPE_NOTIFICATION);
    }
    
    /* 9th event should trigger drop of 1st */
    iolink_event_trigger(&ctx, 0xFFFF, IOLINK_EVENT_TYPE_ERROR);

    iolink_event_t ev;
    iolink_events_pop(&ctx, &ev); /* Skip first */
    /* Verify we can pop others */
    assert_true(iolink_events_pending(&ctx));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_event_queue_flow),
        cmocka_unit_test(test_event_queue_overflow),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
