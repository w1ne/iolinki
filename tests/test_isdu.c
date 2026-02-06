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
#include "iolinki/params.h"
#include "iolinki/data_storage.h"
#include "iolinki/protocol.h"
#include "iolinki/dll.h"

static void test_isdu_vendor_name_read(void** state)
{
    (void) state;
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
    while (iolink_isdu_get_response_byte(&ctx, &byte) > 0) {   // Control
        if (iolink_isdu_get_response_byte(&ctx, &byte) > 0) {  // Data
            name[i++] = (char) byte;
        }
        if (i >= 31) break;
    }
    name[i] = '\0';

    assert_int_equal(i, 7);
    assert_memory_equal(name, "iolinki", 7);
}

static void test_isdu_device_status_read(void** state)
{
    (void) state;
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

static void test_isdu_detailed_device_status_read(void** state)
{
    (void) state;
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
    assert_int_equal(byte,
                     0x9A); /* Appeared (0x80) | Error (0x03<<3 = 0x18) | Instance DLL (0x02) */

    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Control */
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Data: Code High */
    assert_int_equal(byte, 0x18);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Control */
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Data: Code Low */
    assert_int_equal(byte, 0x01);
}

static void test_isdu_error_stats_read(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_dll_ctx_t dll_ctx;
    (void) memset(&dll_ctx, 0, sizeof(dll_ctx));
    dll_ctx.crc_errors = 0x11223344U;
    dll_ctx.timeout_errors = 0xAABBCCDDU;
    dll_ctx.framing_errors = 0x01020304U;
    dll_ctx.timing_errors = 0x05060708U;

    iolink_isdu_init(&ctx);
    ctx.dll_ctx = &dll_ctx;

    /* Read Error Statistics (Index 0x0025) */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xC0), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x90), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, (uint8_t) (IOLINK_IDX_ERROR_STATS >> 8)), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, (uint8_t) (IOLINK_IDX_ERROR_STATS & 0xFFU)), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t data[16];
    size_t idx = 0U;
    uint8_t byte;
    while (iolink_isdu_get_response_byte(&ctx, &byte) > 0) {
        if (iolink_isdu_get_response_byte(&ctx, &byte) > 0) {
            if (idx < sizeof(data)) {
                data[idx++] = byte;
            }
        }
    }

    assert_int_equal(idx, sizeof(data));

    const uint8_t expected[16] = {0x11U, 0x22U, 0x33U, 0x44U, 0xAAU, 0xBBU, 0xCCU, 0xDDU,
                                  0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U};
    assert_memory_equal(data, expected, sizeof(expected));
}

/* System Command Tests */

static void test_system_cmd_device_reset(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);

    /* Write 0x80 to Index 0x0002 */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xC0), 0);                    /* Start + Last */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xA1), 0);                    /* Write, Len=1 */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);                    /* Index High */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x02), 0);                    /* Index Low */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);                    /* Subindex */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, IOLINK_CMD_DEVICE_RESET), 1); /* Command */

    iolink_isdu_process(&ctx);

    /* Verify reset flag is set */
    assert_true(ctx.reset_pending);

    /* Verify success response (empty) */
    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Control byte */
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 0); /* No data */
}

static void test_system_cmd_application_reset(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);

    /* Write 0x81 to Index 0x0002 */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xC0), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xA1), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x02), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, IOLINK_CMD_APPLICATION_RESET), 1);

    iolink_isdu_process(&ctx);

    /* Verify app reset flag is set */
    assert_true(ctx.app_reset_pending);

    /* Verify success response */
    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 0);
}

static void test_system_cmd_factory_restore(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Set a custom application tag first */
    const uint8_t test_tag[] = "TestTag123";
    iolink_params_set(0x0018, 0x00, test_tag, sizeof(test_tag) - 1, false);

    /* Write 0x82 to Index 0x0002 */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xC0), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xA1), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x02), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, IOLINK_CMD_RESTORE_FACTORY_SETTINGS), 1);

    iolink_isdu_process(&ctx);

    /* Verify success response */
    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 0);

    /* Verify application tag was reset */
    uint8_t readback[33];
    int len = iolink_params_get(0x0018, 0x00, readback, sizeof(readback));
    assert_int_equal(len, 0); /* Should be empty after factory reset */
}

static void test_system_cmd_param_upload(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;

    /* Write 0x95 to Index 0x0002 */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xC0), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xA1), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x02), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, IOLINK_CMD_PARAM_UPLOAD), 1);

    iolink_isdu_process(&ctx);

    /* Verify DS state changed to upload */
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOAD_REQ);

    /* Verify success response */
    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 0);
}

static void test_system_cmd_param_download(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;

    /* Write 0x96 to Index 0x0002 */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xC0), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xA1), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x02), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, IOLINK_CMD_PARAM_DOWNLOAD), 1);

    iolink_isdu_process(&ctx);

    /* Verify DS state changed to download */
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOAD_REQ);

    /* Verify success response */
    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 0);
}

static void test_system_cmd_param_break(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;

    /* Set DS to uploading state first */
    ds.state = IOLINK_DS_STATE_UPLOADING;

    /* Write 0x97 to Index 0x0002 */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xC0), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xA1), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x02), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, IOLINK_CMD_PARAM_BREAK), 1);

    iolink_isdu_process(&ctx);

    /* Verify DS state was aborted to idle */
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);

    /* Verify success response */
    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 0);
}

static void test_system_cmd_invalid(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);

    /* Write invalid command 0xFF to Index 0x0002 */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xC0), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xA1), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x02), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xFF), 1);

    iolink_isdu_process(&ctx);

    /* Verify error response */
    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Control */
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Error flag */
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Control */
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Error code */
    assert_int_equal(byte, IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL);
}

static void test_isdu_function_tag_read_write(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Write Function Tag */
    const char* test_tag = "TestFunction";
    uint8_t tag_len = (uint8_t) strlen(test_tag);

    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xC0), 0);           /* Start + Last */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xA0 | tag_len), 0); /* Write, Len */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);           /* Index High */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x19), 0);           /* Index Low */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);           /* Subindex */

    for (uint8_t i = 0; i < tag_len; i++) {
        assert_int_equal(iolink_isdu_collect_byte(&ctx, (uint8_t) test_tag[i]),
                         (i == tag_len - 1) ? 1 : 0);
    }

    iolink_isdu_process(&ctx);

    /* Verify success response (empty) */
    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Control byte */
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 0); /* No data */

    /* Read Function Tag back */
    iolink_isdu_init(&ctx);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xC0), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x90), 0); /* Read Service */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x19), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 1);

    iolink_isdu_process(&ctx);

    /* Collect Response */
    char readback[33] = {0};
    int i = 0;
    while (iolink_isdu_get_response_byte(&ctx, &byte) > 0) {   // Control
        if (iolink_isdu_get_response_byte(&ctx, &byte) > 0) {  // Data
            readback[i++] = (char) byte;
        }
        if (i >= 32) break;
    }
    readback[i] = '\0';

    assert_int_equal(i, tag_len);
    assert_memory_equal(readback, test_tag, tag_len);
}

static void test_isdu_location_tag_read_write(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Write Location Tag */
    const char* test_tag = "Building-A";
    uint8_t tag_len = (uint8_t) strlen(test_tag);

    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xC0), 0);           /* Start + Last */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xA0 | tag_len), 0); /* Write, Len */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);           /* Index High */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x1A), 0);           /* Index Low */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);           /* Subindex */

    for (uint8_t i = 0; i < tag_len; i++) {
        assert_int_equal(iolink_isdu_collect_byte(&ctx, (uint8_t) test_tag[i]),
                         (i == tag_len - 1) ? 1 : 0);
    }

    iolink_isdu_process(&ctx);

    /* Verify success response (empty) */
    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Control byte */
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 0); /* No data */

    /* Read Location Tag back */
    iolink_isdu_init(&ctx);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xC0), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x90), 0); /* Read Service */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x1A), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 1);

    iolink_isdu_process(&ctx);

    /* Collect Response */
    char readback[33] = {0};
    int i = 0;
    while (iolink_isdu_get_response_byte(&ctx, &byte) > 0) {   // Control
        if (iolink_isdu_get_response_byte(&ctx, &byte) > 0) {  // Data
            readback[i++] = (char) byte;
        }
        if (i >= 32) break;
    }
    readback[i] = '\0';

    assert_int_equal(i, tag_len);
    assert_memory_equal(readback, test_tag, tag_len);
}

static void test_isdu_pdin_descriptor_read(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_isdu_init(&ctx);

    /* Read PD Input Descriptor (Index 0x1D) */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xC0), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x90), 0); /* Read Service */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x1D), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 1);

    iolink_isdu_process(&ctx);

    /* Verify response contains PD length */
    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Control */
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Data: PD length */
    /* Default device info has pdin_len = 2 */
    assert_int_equal(byte, 2);

    /* Test write protection */
    iolink_isdu_init(&ctx);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xC0), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0xA1), 0); /* Write, Len=1 */
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x1D), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x00), 0);
    assert_int_equal(iolink_isdu_collect_byte(&ctx, 0x05), 1); /* Try to write value */

    iolink_isdu_process(&ctx);

    /* Verify write-protected error */
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Control */
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Error flag */
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Control */
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Error code */
    assert_int_equal(byte, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_isdu_vendor_name_read),
        cmocka_unit_test(test_isdu_device_status_read),
        cmocka_unit_test(test_isdu_detailed_device_status_read),
        cmocka_unit_test(test_isdu_error_stats_read),
        cmocka_unit_test(test_system_cmd_device_reset),
        cmocka_unit_test(test_system_cmd_application_reset),
        cmocka_unit_test(test_system_cmd_factory_restore),
        cmocka_unit_test(test_system_cmd_param_upload),
        cmocka_unit_test(test_system_cmd_param_download),
        cmocka_unit_test(test_system_cmd_param_break),
        cmocka_unit_test(test_system_cmd_invalid),
        cmocka_unit_test(test_isdu_function_tag_read_write),
        cmocka_unit_test(test_isdu_location_tag_read_write),
        cmocka_unit_test(test_isdu_pdin_descriptor_read),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
