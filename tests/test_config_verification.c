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

/* Define custom config BEFORE including headers to verify overrides (if we were testing override mech)
   But here we test that the HEADERS respect the definitions. 
   We will verify the default sizes match config.h defaults first. */

#include "iolinki/isdu.h"
#include "iolinki/events.h"
#include "iolinki/dll.h"
#include "iolinki/config.h"

static void test_config_defaults(void **state) {
    (void)state;
    
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

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_config_defaults),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
