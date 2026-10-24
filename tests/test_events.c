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
#include <unistd.h>

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

    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN, IOLINK_EVENT_TYPE_WARNING);
    iolink_event_trigger(&ctx, IOLINK_EVENTCODE_SHORT_CIRCUIT, IOLINK_EVENT_TYPE_ERROR);

    iolink_event_t ev;
    /* A.6.4: Errors are prioritized over Warnings. */
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_SHORT_CIRCUIT);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_ERROR);

    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, IOLINK_EVENTCODE_SUPPLY_VOLTAGE_UNDERRUN);
    assert_int_equal(ev.type, IOLINK_EVENT_TYPE_WARNING);
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
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 0); /* Still OK */

    iolink_event_trigger(&ctx, 0x1002, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 4); /* Failure */

    iolink_event_trigger(&ctx, 0x1003, IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(iolink_events_get_highest_severity(&ctx), 4); /* Still Failure */

    /* Test get_all */
    iolink_event_t events[8];
    uint8_t count = iolink_events_get_all(&ctx, events, 8);
    assert_int_equal(count, 3);
    /* Severity order: Error(0x1002) > Warning(0x1003) > Notification(0x1001). */
    assert_int_equal(events[0].code, 0x1002);
    assert_int_equal(events[1].code, 0x1003);
    assert_int_equal(events[2].code, 0x1001);

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

    /* One error event: the memory freezes as the flag is raised (Table 60 T3). */
    iolink_event_trigger(&ctx, 0x1801U, IOLINK_EVENT_TYPE_ERROR);
    assert_true(iolink_events_flag(&ctx));

    /* Table 58 / Figure A.22: StatusCode type 2, bit 7 set, bit 0 active. */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);

    /* Slot 1 at addresses 1,2,3: qualifier 0xF4 (appears|error|device|Application), 0x1801. */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x01U), 0xF4U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x02U), 0x18U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x03U), 0x01U);

    /* A second event while frozen is queued but not visible. */
    iolink_event_trigger(&ctx, 0x1803U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x04U), 0x00U);

    /* Reserved / out of range reads return 0. */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x13U), 0x00U);
}

static void test_event_memory_frozen(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);

    /* Further events while frozen are queued but not visible. */
    iolink_event_trigger(&ctx, 0x2000U, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_trigger(&ctx, 0x3000U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x04U), 0x00U); /* slot 2 unused */
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

    /* Table 60 T5: confirmation releases the presented slot; the event queued
       while frozen is re-frozen afterwards. */
    iolink_events_memory_write(&ctx, 0x00U, 0x00U);
    assert_true(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);

    /* Confirming again clears the queue. */
    iolink_events_memory_write(&ctx, 0x00U, 0x00U);
    assert_false(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x00U);
    assert_int_equal(iolink_events_memory_read(&ctx, 0x01U), 0x00U);

    /* Writes to other addresses are ignored. */
    iolink_event_trigger(&ctx, 0x4000U, IOLINK_EVENT_TYPE_WARNING);
    assert_true(iolink_events_flag(&ctx));
    iolink_events_memory_write(&ctx, 0x01U, 0xAAU);
    assert_true(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0x01U), 0xE4U); /* warning qualifier */
}

/* Confirmation acknowledges only the slots that were actually presented; events
 * queued while frozen survive for the next readout. */
static void test_event_memory_confirm_only_presented(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    /* The first event freezes a 1-slot snapshot. */
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x81U);

    /* Further events arrive while frozen: queued but not yet presented. */
    iolink_event_trigger(&ctx, 0x2000U, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_trigger(&ctx, 0x3000U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(ctx.count, 3U);

    /* Confirm: only the presented slot is acknowledged; the other two survive. */
    iolink_events_memory_write(&ctx, 0x00U, 0x00U);
    assert_int_equal(ctx.count, 2U);
    assert_true(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x83U); /* 2 slots */
}

/* A.6.4: an Error/Warning appears once and disappears only after the 50 ms
 * minimum appearance window. */
static void test_event_appears_disappears(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_clear(&ctx, 0x1000U);

    /* Within the window the disappearance stays deferred. */
    iolink_events_process(&ctx);
    iolink_event_t ev;
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, 0x1000U);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_APPEARS);
    assert_false(iolink_events_pop(&ctx, &ev));

    usleep(60000U); /* > IOLINK_EVENT_MIN_APPEAR_MS */
    iolink_events_process(&ctx);
    assert_true(iolink_events_pop(&ctx, &ev));
    assert_int_equal(ev.code, 0x1000U);
    assert_int_equal(ev.mode, IOLINK_EVENT_MODE_DISAPPEARS);
}

/* The same EventCode may not appear twice while it is active; clearing an
 * unknown code is ignored. */
static void test_event_lifecycle_no_repeat(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_WARNING);
    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(ctx.count, 1U);

    iolink_event_clear(&ctx, 0x2000U); /* not active: ignored */
    assert_int_equal(ctx.count, 1U);
}

/* A.6.4: no more than one report per second for the same EventCode. */
static void test_event_recently_sent(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);
    iolink_event_t ev;

    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_clear(&ctx, 0x1000U);
    usleep(60000U);
    iolink_events_process(&ctx);
    (void) iolink_events_pop(&ctx, &ev); /* appears */
    (void) iolink_events_pop(&ctx, &ev); /* disappears */

    /* Re-appearing within one second is suppressed. */
    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    assert_int_equal(ctx.count, 0U);
}

/* A.6.4: queued events are discarded when communication is interrupted. */
static void test_events_reset_clears(void** state)
{
    (void) state;
    iolink_events_ctx_t ctx;
    iolink_events_init(&ctx);

    iolink_event_trigger(&ctx, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    assert_true(iolink_events_flag(&ctx));

    iolink_events_reset(&ctx);
    assert_int_equal(ctx.count, 0U);
    assert_int_equal(ctx.pending_count, 0U);
    assert_false(iolink_events_flag(&ctx));
    assert_int_equal(iolink_events_memory_read(&ctx, 0x00U), 0x00U);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_event_queue_flow),    cmocka_unit_test(test_event_queue_overflow),
        cmocka_unit_test(test_standard_codes),      cmocka_unit_test(test_phy_diagnostic_codes),
        cmocka_unit_test(test_event_peek),          cmocka_unit_test(test_event_helpers),
        cmocka_unit_test(test_event_classify),      cmocka_unit_test(test_event_memory_layout),
        cmocka_unit_test(test_event_memory_frozen), cmocka_unit_test(test_event_memory_confirm),
        cmocka_unit_test(test_event_memory_confirm_only_presented),
        cmocka_unit_test(test_event_appears_disappears),
        cmocka_unit_test(test_event_lifecycle_no_repeat),
        cmocka_unit_test(test_event_recently_sent),
        cmocka_unit_test(test_events_reset_clears),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
