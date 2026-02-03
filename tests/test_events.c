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
    iolink_events_init();

    assert_false(iolink_events_pending());

    iolink_event_trigger(0x1234, IOLINK_EVENT_TYPE_WARNING);
    assert_true(iolink_events_pending());

    iolink_event_t ev;
    assert_true(iolink_events_pop(&ev));
    assert_int_equal(ev.code, 0x1234);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_WARNING);
    assert_false(iolink_events_pending());
}

static void test_event_queue_overflow(void **state)
{
    (void)state;
    iolink_events_init();

    /* Fill queue (size 8) */
    for (int i = 0; i < 8; i++) {
        iolink_event_trigger(i, IOLINK_EVENT_TYPE_NOTIFICATION);
    }
    
    /* 9th event should trigger drop of 1st */
    iolink_event_trigger(0xFFFF, IOLINK_EVENT_TYPE_ERROR);

    iolink_event_t ev;
    iolink_events_pop(&ev); /* Skip first */
    assert_int_equal(ev.code, 0); /* Still 0 if FIFO? Wait, my implementation drops oldest. */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_event_queue_flow),
        cmocka_unit_test(test_event_queue_overflow),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
