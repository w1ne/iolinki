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

static void test_event_queue_flow(void** state)
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

static void test_event_queue_overflow(void** state)
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

static void test_standard_codes(void** state)
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

static void test_phy_diagnostic_codes(void** state)
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

static void test_event_peek(void** state)
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

static void test_event_helpers(void** state)
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

static void test_event_classify(void** state)
{
    (void) state;
    assert_int_equal(iolink_event_classify(IOLINK_EVENTCODE_NO_MALFUNCTION),
                     IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_int_equal(iolink_event_classify(IOLINK_EVENTCODE_DS_UPLOAD_REQUEST),
                     IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_int_equal(iolink_event_classify(IOLINK_EVENTCODE_SUPPLY_VOLTAGE_OVERRUN),
                     IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(iolink_event_classify(IOLINK_EVENTCODE_MAINTENANCE_REFILL),
                     IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(iolink_event_classify(IOLINK_EVENTCODE_HARDWARE_FAULT),
                     IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(iolink_event_classify(IOLINK_EVENTCODE_SHORT_CIRCUIT),
                     IOLINK_EVENT_TYPE_ERROR);
    /* Reserved/unknown defaults to error. */
    assert_int_equal(iolink_event_classify(0x2222U), IOLINK_EVENT_TYPE_ERROR);
}

static void test_event_memory_layout(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    assert_false(iolink_events_flag(&ctx));

    /* Two error events (Queue slots 1 and 2). */
    iolink_event_trigger(&ctx, 0x1801U, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_trigger(&ctx, 0x1803U, IOLINK_EVENT_TYPE_ERROR);
    assert_true(iolink_events_flag(&ctx));

    /* Table 58 / Figure A.22: StatusCode type 2, bit 7 set, bits 0-1 active. */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x83U);

    /* Slot 1 at addresses 1,2,3: qualifier 0xF2 (appears|error|device|DL), 0x1801. */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x01U), 0xF2U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x02U), 0x18U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x03U), 0x01U);

    /* Slot 2 at addresses 4,5,6: 0x1803. */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x04U), 0xF2U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x05U), 0x18U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x06U), 0x03U);

    /* Reserved / out of range reads return 0. */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x13U), 0x00U);
}

static void test_event_memory_frozen(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_trigger(&ctx, 0x2000U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x83U);

    /* A third event while the flag is set is queued but not visible. */
    iolink_event_trigger(&ctx, 0x3000U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x83U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x07U), 0x00U); /* slot 3 unused */
    assert_int_equal(ctx.count, 3U);                                 /* still queued */
}

static void test_event_memory_confirm(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_trigger(&ctx, 0x2000U, IOLINK_EVENT_TYPE_ERROR);
    assert_true(iolink_events_flag(&ctx));

    /* Table 60 T5: a write to StatusCode clears the flag and releases memory. */
    iolink_events_memory_write(&ctx, 0x00U, 0x00U);
    assert_false(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x00U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x01U), 0x00U);

    /* Writes to other addresses are ignored. */
    iolink_event_trigger(&ctx, 0x4000U, IOLINK_EVENT_TYPE_WARNING);
    assert_true(iolink_events_flag(&ctx));
    iolink_events_memory_write(&ctx, 0x01U, 0xAAU);
    assert_true(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0x01U), 0xE2U); /* warning qualifier */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_event_queue_flow),    cmocka_unit_test(test_event_queue_overflow),
        cmocka_unit_test(test_standard_codes),      cmocka_unit_test(test_phy_diagnostic_codes),
        cmocka_unit_test(test_event_peek),          cmocka_unit_test(test_event_helpers),
        cmocka_unit_test(test_event_classify),      cmocka_unit_test(test_event_memory_layout),
        cmocka_unit_test(test_event_memory_frozen), cmocka_unit_test(test_event_memory_confirm),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
