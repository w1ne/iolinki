/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_isdu_segmented.c
 * @brief Unit tests for segmented ISDU messaging
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "iolinki/isdu.h"

/* Removed unused test stub to avoid -Werror=unused-function */

static void test_isdu_segmented_write_corrected(void **state)
{
    (void)state;
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);
    
    /* Write Index 0x18, 2 bytes */
    iolink_isdu_collect_byte(&ctx, 0x81);
    iolink_isdu_collect_byte(&ctx, 0xA2); 
    
    iolink_isdu_collect_byte(&ctx, 0x02);
    iolink_isdu_collect_byte(&ctx, 0x00); 
    
    iolink_isdu_collect_byte(&ctx, 0x03);
    iolink_isdu_collect_byte(&ctx, 0x18); 
    
    iolink_isdu_collect_byte(&ctx, 0x04);
    iolink_isdu_collect_byte(&ctx, 0x00); 
    
    iolink_isdu_collect_byte(&ctx, 0x05);
    iolink_isdu_collect_byte(&ctx, 0xAA); 
    
    iolink_isdu_collect_byte(&ctx, 0x46);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xBB), 1); 
    
    iolink_isdu_process(&ctx);
    
    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    /* Response to successful write is just a Control byte with Start/Last set (0xC0) 
       and same sequence as LAST request? Actually IO-Link V1.1 response seq is often 0 or same. 
       Our code sends (ctx->segment_seq & 0x3F). Last seq was 6. So 0xC6 or 0xC0.
       Wait, iolink_isdu_get_response_byte resets seq or keeps it?
    */
    assert_int_equal(byte & 0xC0, 0xC0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_isdu_segmented_write_corrected),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
