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
    (void) state;
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
    (void) state;
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

static void test_standard_codes(void **state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    iolink_event_trigger(&ctx, IOLINK_EVENT_COMM_CRC, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_trigger(&ctx, IOLINK_EVENT_COMM_TIMEOUT, IOLINK_EVENT_TYPE_ERROR);

    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, IOLINK_EVENT_COMM_CRC);
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, IOLINK_EVENT_COMM_TIMEOUT);
}

static void test_phy_diagnostic_codes(void **state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    iolink_event_trigger(&ctx, IOLINK_EVENT_PHY_VOLTAGE_FAULT, IOLINK_EVENT_TYPE_WARNING);
    iolink_event_trigger(&ctx, IOLINK_EVENT_PHY_SHORT_CIRCUIT, IOLINK_EVENT_TYPE_ERROR);

    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, IOLINK_EVENT_PHY_VOLTAGE_FAULT);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_WARNING);

    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, IOLINK_EVENT_PHY_SHORT_CIRCUIT);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_ERROR);
}

static void test_event_peek(void **state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    iolink_event_trigger(&ctx, 0x1122, IOLINK_EVENT_TYPE_NOTIFICATION);

    iolink_event_t ev;
    assert_true(iolink_events_peek(&ctx, &ev));
    assert_int_equal(ev.code, 0x1122);
    /* Should still be pending */
    assert_true(iolink_events_pending(&ctx));

    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, 0x1122);
    assert_false(iolink_events_pending(&ctx));
}

static void test_event_helpers(void **state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    /* Test highest severity */
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 0); /* OK */

    iolink_event_trigger(&ctx, 0x1001, IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 1); /* Maintenance */

    iolink_event_trigger(&ctx, 0x1002, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 3); /* Failure */

    iolink_event_trigger(&ctx, 0x1003, IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 3); /* Still Failure */

    /* Test get_all */
    iolink_event_t events[8];
    uint8_t count = iolink_events_get_all(&ctx, events, 8);
    assert_int_equal(count, 3);
    assert_int_equal(events[0].code, 0x1001);
    assert_int_equal(events[1].code, 0x1002);
    assert_int_equal(events[2].code, 0x1003);

    /* Test get_all with limit */
    count = iolink_events_get_all(&ctx, events, 2);
    assert_int_equal(count, 2);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_event_queue_flow), cmocka_unit_test(test_event_queue_overflow),
        cmocka_unit_test(test_standard_codes),   cmocka_unit_test(test_phy_diagnostic_codes),
        cmocka_unit_test(test_event_peek),       cmocka_unit_test(test_event_helpers),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
