/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <cmocka.h>

/* Define custom config BEFORE including headers to verify overrides (if we were testing override
   mech) But here we test that the HEADERS respect the definitions. We will verify the default sizes
   match config.h defaults first. */

#include "iolinki/isdu.h"
#include "iolinki/events.h"
#include "iolinki/dll.h"
#include "iolinki/config.h"
#include "iolinki/iolink.h"

static void test_config_defaults(void** state)
{
    (void) state;

    /* Verify ISDU buffer size */
    iolink_isdu_ctx_t isdu;
    /* Expected: 2 buffers of IOLINK_ISDU_BUFFER_SIZE (256 default) + overhead */
    assert_true(sizeof(isdu.buffer) == IOLINK_ISDU_BUFFER_SIZE);
    assert_true(sizeof(isdu.response_buf) == IOLINK_ISDU_BUFFER_SIZE);

    /* Verify Event Queue */
    iolink_events_ctx_t events;
    /* Expected: IOLINK_EVENT_QUEUE_SIZE (4 default) * sizeof(iolink_event_t) */
    assert_true(sizeof(events.queue) / sizeof(iolink_event_t) == IOLINK_EVENT_QUEUE_SIZE);

    /* Verify DLL Process Data buffers */
    iolink_dll_ctx_t dll;
    assert_true(sizeof(dll.pd_in) == IOLINK_PD_IN_MAX_SIZE);
    assert_true(sizeof(dll.pd_out) == IOLINK_PD_OUT_MAX_SIZE);
}

static void test_cycle_time_encode_table_b3(void** state)
{
    (void) state;

    /* B.1.3: 0x00 means the Device provides no MinCycleTime. */
    assert_int_equal(iolink_cycle_time_encode(0U), 0x00U);

    /* Table B.3 minimum is 0.4 ms; shorter non-zero values clamp to 0x04. */
    assert_int_equal(iolink_cycle_time_encode(1U), 0x04U);
    assert_int_equal(iolink_cycle_time_encode(3U), 0x04U);
    assert_int_equal(iolink_cycle_time_encode(4U), 0x04U);

    /* Time base 00: 0.1 ms x multiplier, 0.4 .. 6.3 ms. */
    assert_int_equal(iolink_cycle_time_encode(40U), 0x28U);
    assert_int_equal(iolink_cycle_time_encode(63U), 0x3FU);

    /* Time base 01: 6.4 ms + 0.4 ms x multiplier, 6.4 .. 31.6 ms. */
    assert_int_equal(iolink_cycle_time_encode(64U), 0x40U);
    assert_int_equal(iolink_cycle_time_encode(65U), 0x41U);
    assert_int_equal(iolink_cycle_time_encode(68U), 0x41U);
    assert_int_equal(iolink_cycle_time_encode(316U), 0x7FU);

    /* Time base 10: 32.0 ms + 1.6 ms x multiplier, 32.0 .. 132.8 ms. */
    assert_int_equal(iolink_cycle_time_encode(317U), 0x80U);
    assert_int_equal(iolink_cycle_time_encode(320U), 0x80U);
    assert_int_equal(iolink_cycle_time_encode(1328U), 0xBFU);
    assert_int_equal(iolink_cycle_time_encode(1329U), 0xBFU);
    assert_int_equal(iolink_cycle_time_encode(0xFFFFU), 0xBFU);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_config_defaults),
        cmocka_unit_test(test_cycle_time_encode_table_b3),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
