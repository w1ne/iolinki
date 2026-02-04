/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_isdu.c
 * @brief Unit tests for ISDU acyclic messaging
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "iolinki/isdu.h"
#include "iolinki/events.h"

static void test_isdu_vendor_name_read(void **state)
{
    (void)state;
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);

    /* 1. Send READ Request for Index 0x10 (Vendor Name) */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xC0), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x90), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x10), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 1);

    iolink_isdu_process(&ctx);

    /* 2. Collect Response */
    uint8_t byte;
    char name[32] = {0};
    int i = 0;
    
    /* Default vendor name is "iolinki" (7 chars) */
    /* Alternate: Control, Data, Control, Data... */
    while (iolink_isdu_get_response_byte(&ctx, &byte) > 0) { // Control
        if (iolink_isdu_get_response_byte(&ctx, &byte) > 0) { // Data
            name[i++] = (char)byte;
        }
        if (i >= 31) break;
    }
    name[i] = '\0';
    
    assert_int_equal(i, 7);
    assert_memory_equal(name, "iolinki", 7);
}

static void test_isdu_device_status_read(void **state)
{
    (void)state;
    iolink_isdu_ctx_t ctx;
    iolink_events_ctx_t events;
    iolink_events_init(&events);
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;

    /* 1. Initially status should be OK (0) */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xC0), 0); /* Read Index 0x1B */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x90), 0); /* Read Service */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x1B), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Control */
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Data: Status */
    assert_int_equal(byte, 0);

    /* 2. Trigger an error and check status again */
    iolink_event_trigger(&events, IOLINK_EVENT_COMM_CRC, IOLINK_EVENT_TYPE_ERROR);
    
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xC0), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x90), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x1B), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 1);
    iolink_isdu_process(&ctx);

    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 3); /* Failure */
}

static void test_isdu_detailed_device_status_read(void **state)
{
    (void)state;
    iolink_isdu_ctx_t ctx;
    iolink_events_ctx_t events;
    iolink_events_init(&events);
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;

    iolink_event_trigger(&events, 0x1801, IOLINK_EVENT_TYPE_ERROR);

    /* Read Detailed Status (Index 0x1C) */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xC0), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x90), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x1C), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Control */
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Data: Qualifier */
    assert_int_equal(byte, 0x9A); /* Appeared (0x80) | Error (0x03<<3 = 0x18) | Instance DLL (0x02) */
    /* Let's check src code for qualifier: Error=3, shifts left 3 in my mind? 
       src/isdu.c:394: qualifier |= (0x03U << 3); break; -> 0x18. 0x80 | 0x18 | 0x02 = 0x9A.
       Wait, let's re-read src/isdu.c:272: qualifier = 0xC0U; break; (That was handle_mandatory_indices)
       But handle_detailed_device_status (line 361) does: qualifier = 0x80U; ... 0x03U << 3 ... qualifier |= 0x02U;
       So 0x80 | 0x18 | 02 = 0x9A.
    */
    
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Control */
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Data: Code High */
    assert_int_equal(byte, 0x18);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Control */
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Data: Code Low */
    assert_int_equal(byte, 0x01);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_isdu_vendor_name_read),
        cmocka_unit_test(test_isdu_device_status_read),
        cmocka_unit_test(test_isdu_detailed_device_status_read),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
