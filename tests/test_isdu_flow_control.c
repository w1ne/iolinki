/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/isdu.h"
#include "iolinki/protocol.h"

static void test_isdu_busy_during_execution(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);

    /* 1. Simulate an ongoing execution state */
    ctx.state = ISDU_STATE_SERVICE_EXECUTE;
    ctx.header.index = 0x1234; /* Fake index */

    /* 2. Send a new 'Start' request (Read Index 0x10) */
    /* Byte: Start(1)|RW(1)|Len(0) -> 0x90 ? No, Read is 0x09. */
    /* Start bit is 0x80. */
    /* Byte = 0x80 | (0x09 << 4) | 0 = 0x80 | 0x90 = 0x90 ? No.
       ISDU Byte structure:
       IOLINK_ISDU_CTRL_START (0x80) is part of Control Byte?
       No, in V1.1 (which we are using), the Start bit is in the Control Byte if interleaved used.
       Wait, ISDU flow:
       Type 0/1 without interleaving (Legacy/Simple): Just data bytes.
       iolink_isdu_collect_byte documentation says: "This is called by the DLL for every OD byte".

       In `isdu.c`:
       bool start = ((byte & 0x80U) != 0U);

       So if we send 0x90 (Read service) with MSB set?
       Service IDs are 4 bits. Read=0x9, Write=0xA.
       If we send 0x90, that is 1001 0000. MSB is 1.
       The code checks `start` bit.

       Code in `src/isdu.c`:
       bool start = ((byte & 0x80U) != 0U);
       case ISDU_STATE_IDLE: return isdu_handle_idle(ctx, byte);

       So yes, 0x90 is treated as a Start.
    */

    int result = iolink_isdu_collect_byte(&ctx, 0x90);

    /* Expectation: Should be handled (return 1) but generate Error Response */
    assert_int_equal(result, 1);

    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Control */
    /* Actually we don't handle control bytes fully in this unit test mock unless we parse it.
       But the first byte should be the response control byte if interleaving, or just response?
       Let's check `isdu.c`: `isdu_get_response_byte` sends Control Byte first.
    */

    /* Check response buffer directly for simplicity */
    assert_int_equal(ctx.response_buf[0], 0x80U);                  /* Error */
    assert_int_equal(ctx.response_buf[1], IOLINK_ISDU_ERROR_BUSY); /* 0x30 */
    assert_int_equal(ctx.response_len, 2);
}

static void test_isdu_interrupted_response(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);

    /* 1. Set up a response ready state */
    ctx.state = ISDU_STATE_RESPONSE_READY;
    ctx.response_len = 5;
    ctx.response_idx = 2; /* Partially read */

    /* 2. Send Start bit (New Request) */
    int result = iolink_isdu_collect_byte(&ctx, 0x90);

    /* Expectation: Abort old response, start new request */
    /* Should return 0 (still collecting header)? No, isdu_handle_idle returns 0 if success. */
    assert_int_equal(result, 0);

    assert_int_equal(ctx.state,
                     ISDU_STATE_HEADER_INDEX_HIGH); /* 0x90 -> Read, Len 0 -> expect Index High */
    assert_int_equal(ctx.response_len, 0);          /* Cleared */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_isdu_busy_during_execution),
        cmocka_unit_test(test_isdu_interrupted_response),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
