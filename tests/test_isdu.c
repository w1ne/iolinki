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

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_isdu_vendor_name_read),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
