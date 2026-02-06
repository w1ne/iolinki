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
#include "iolinki/device_info.h"
#include "test_helpers.h"

static void test_isdu_vendor_name_read(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    static const iolink_device_info_t info = {
        .vendor_name = "iolinki",
        .vendor_id = 0x1234,
        .device_id = 0x567890,
    };
    iolink_device_info_init(&info);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* 1. Send READ Request for Index 0x10 (Vendor Name) */
    assert_int_equal(isdu_send_read_request(&ctx, 0x0010, 0x00), 1);

    iolink_isdu_process(&ctx);

    /* 2. Collect Response */
    char name[32] = {0};
    int len = isdu_collect_response(&ctx, (uint8_t*) name, sizeof(name) - 1);

    assert_int_equal(len, 7);
    assert_memory_equal(name, "iolinki", 7);
}

static void test_isdu_device_status_read(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_events_ctx_t events;
    static const iolink_device_info_t info = {
        .vendor_name = "iolinki",
        .vendor_id = 0x1234,
        .device_id = 0x567890,
    };
    iolink_device_info_init(&info);
    iolink_params_init();
    iolink_events_init(&events);
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;

    /* 1. Initially status should be OK (0) */
    assert_int_equal(isdu_send_read_request(&ctx, 0x001B, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t status_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, status_buf, sizeof(status_buf)), 1);
    assert_int_equal(status_buf[0], 0);

    /* 2. Trigger an error and check status again */
    iolink_event_trigger(&events, IOLINK_EVENT_COMM_CRC, IOLINK_EVENT_TYPE_ERROR);

    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;
    assert_int_equal(isdu_send_read_request(&ctx, 0x001B, 0x00), 1);
    iolink_isdu_process(&ctx);

    assert_int_equal(isdu_collect_response(&ctx, status_buf, sizeof(status_buf)), 1);
    assert_int_equal(status_buf[0], 3); /* Failure */
}

static void test_isdu_detailed_device_status_read(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_events_ctx_t events;
    static const iolink_device_info_t info = {
        .vendor_name = "iolinki",
        .vendor_id = 0x1234,
        .device_id = 0x567890,
    };
    iolink_device_info_init(&info);
    iolink_params_init();
    iolink_events_init(&events);
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;

    iolink_event_trigger(&events, 0x1801, IOLINK_EVENT_TYPE_ERROR);

    /* Read Detailed Status (Index 0x1C) */
    assert_int_equal(isdu_send_read_request(&ctx, 0x001C, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t detailed_buf[3];
    assert_int_equal(isdu_collect_response(&ctx, detailed_buf, sizeof(detailed_buf)), 3);
    assert_int_equal(detailed_buf[0],
                     0x9A); /* Appeared (0x80) | Error (0x03<<3 = 0x18) | Instance DLL (0x02) */
    assert_int_equal(detailed_buf[1], 0x18); /* Code High */
    assert_int_equal(detailed_buf[2], 0x01); /* Code Low */
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

    static const iolink_device_info_t info = {
        .vendor_name = "iolinki",
        .vendor_id = 0x1234,
        .device_id = 0x567890,
    };
    iolink_device_info_init(&info);
    iolink_params_init();
    iolink_isdu_init(&ctx);
    ctx.dll_ctx = &dll_ctx;

    /* Read Error Statistics (Index 0x0025) */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_ERROR_STATS, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t data[16];
    int len = isdu_collect_response(&ctx, data, sizeof(data));
    assert_int_equal(len, sizeof(data));

    const uint8_t expected[16] = {0x11U, 0x22U, 0x33U, 0x44U, 0xAAU, 0xBBU, 0xCCU, 0xDDU,
                                  0x01U, 0x02U, 0x03U, 0x04U, 0x05U, 0x06U, 0x07U, 0x08U};
    assert_memory_equal(data, expected, sizeof(expected));
}

/* System Command Tests */

static void test_system_cmd_device_reset(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    static const iolink_device_info_t info = {
        .vendor_name = "iolinki",
        .vendor_id = 0x1234,
        .device_id = 0x567890,
    };
    iolink_device_info_init(&info);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Write 0x80 to Index 0x0002 */
    uint8_t cmd_data[] = {IOLINK_CMD_DEVICE_RESET};
    assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);

    iolink_isdu_process(&ctx);

    /* Verify reset flag is set */
    assert_true(ctx.reset_pending);

    /* Verify success response (empty) */
    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0); /* No data */
}

static void test_system_cmd_application_reset(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    static const iolink_device_info_t info = {
        .vendor_name = "iolinki",
        .vendor_id = 0x1234,
        .device_id = 0x567890,
    };
    iolink_device_info_init(&info);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Write 0x81 to Index 0x0002 */
    uint8_t cmd_data[] = {IOLINK_CMD_APPLICATION_RESET};
    assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);

    iolink_isdu_process(&ctx);

    /* Verify app reset flag is set */
    assert_true(ctx.app_reset_pending);

    /* Verify success response */
    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0); /* No data */
}

static void test_system_cmd_factory_restore(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    static const iolink_device_info_t info = {
        .vendor_name = "iolinki",
        .vendor_id = 0x1234,
        .device_id = 0x567890,
    };
    iolink_device_info_init(&info);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Set a custom application tag first */
    const uint8_t test_tag[] = "TestTag123";
    iolink_params_set(0x0018, 0x00, test_tag, sizeof(test_tag) - 1, false);

    /* Write 0x82 to Index 0x0002 */
    uint8_t cmd_data[] = {IOLINK_CMD_RESTORE_FACTORY_SETTINGS};
    assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);

    iolink_isdu_process(&ctx);

    /* Verify success response */
    /* Verify success response (empty) */
    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0); /* No data */

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
    static const iolink_device_info_t info = {
        .vendor_name = "iolinki",
        .vendor_id = 0x1234,
        .device_id = 0x567890,
    };
    iolink_device_info_init(&info);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;

    /* Write 0x95 to Index 0x0002 */
    uint8_t cmd_data[] = {IOLINK_CMD_PARAM_UPLOAD};
    assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);

    iolink_isdu_process(&ctx);

    /* Verify DS state changed to upload */
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOAD_REQ);

    /* Verify success response */
    /* Verify success response (empty) */
    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0); /* No data */
}

static void test_system_cmd_param_download(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    static const iolink_device_info_t info = {
        .vendor_name = "iolinki",
        .vendor_id = 0x1234,
        .device_id = 0x567890,
    };
    iolink_device_info_init(&info);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;

    /* Write 0x96 to Index 0x0002 */
    uint8_t cmd_data[] = {IOLINK_CMD_PARAM_DOWNLOAD};
    assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);

    iolink_isdu_process(&ctx);

    /* Verify DS state changed to download */
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOAD_REQ);

    /* Verify success response */
    /* Verify success response (empty) */
    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0); /* No data */
}

static void test_system_cmd_param_break(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    static const iolink_device_info_t info = {
        .vendor_name = "iolinki",
        .vendor_id = 0x1234,
        .device_id = 0x567890,
    };
    iolink_device_info_init(&info);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;

    /* Set DS to uploading state first */
    ds.state = IOLINK_DS_STATE_UPLOADING;

    /* Write 0x97 to Index 0x0002 */
    uint8_t cmd_data[] = {IOLINK_CMD_PARAM_BREAK};
    assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);

    iolink_isdu_process(&ctx);

    /* Verify DS state was aborted to idle */
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);

    /* Verify success response */
    /* Verify success response (empty) */
    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0); /* No data */
}

static void test_system_cmd_invalid(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    static const iolink_device_info_t info = {
        .vendor_name = "iolinki",
        .vendor_id = 0x1234,
        .device_id = 0x567890,
    };
    iolink_device_info_init(&info);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Write invalid command 0xFF to Index 0x0002 */
    uint8_t cmd_data[] = {0xFF};
    assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);

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
    static const iolink_device_info_t info = {
        .vendor_name = "iolinki",
        .vendor_id = 0x1234,
        .device_id = 0x567890,
    };
    iolink_device_info_init(&info);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Write Function Tag */
    const char* test_tag = "TestFunction";
    uint8_t tag_len = (uint8_t) strlen(test_tag);

    assert_int_equal(
        isdu_send_write_request(&ctx, 0x0019, 0x00, (const uint8_t*) test_tag, tag_len), 1);

    iolink_isdu_process(&ctx);

    /* Verify success response (empty) */
    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0); /* No data */

    /* Read Function Tag back */
    iolink_isdu_init(&ctx);
    assert_int_equal(isdu_send_read_request(&ctx, 0x0019, 0x00), 1);

    iolink_isdu_process(&ctx);

    /* Collect Response */
    char readback[33] = {0};
    int len = isdu_collect_response(&ctx, (uint8_t*) readback, sizeof(readback) - 1);

    assert_int_equal(len, tag_len);
    assert_memory_equal(readback, test_tag, tag_len);
}

static void test_isdu_location_tag_read_write(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    static const iolink_device_info_t info = {
        .vendor_name = "iolinki",
        .vendor_id = 0x1234,
        .device_id = 0x567890,
    };
    iolink_device_info_init(&info);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Write Location Tag */
    const char* test_tag = "Building-A";
    uint8_t tag_len = (uint8_t) strlen(test_tag);

    assert_int_equal(
        isdu_send_write_request(&ctx, 0x001A, 0x00, (const uint8_t*) test_tag, tag_len), 1);

    iolink_isdu_process(&ctx);

    /* Verify success response (empty) */
    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0); /* No data */

    /* Read Location Tag back */
    iolink_isdu_init(&ctx);
    assert_int_equal(isdu_send_read_request(&ctx, 0x001A, 0x00), 1);

    iolink_isdu_process(&ctx);

    /* Collect Response */
    char readback[33] = {0};
    int len = isdu_collect_response(&ctx, (uint8_t*) readback, sizeof(readback) - 1);

    assert_int_equal(len, tag_len);
    assert_memory_equal(readback, test_tag, tag_len);
}

static void test_isdu_pdin_descriptor_read(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    static const iolink_device_info_t info = {
        .vendor_name = "iolinki",
        .vendor_id = 0x1234,
        .device_id = 0x567890,
    };
    iolink_device_info_init(&info);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Read PD Input Descriptor (Index 0x1D) */
    assert_int_equal(isdu_send_read_request(&ctx, 0x001D, 0x00), 1);

    iolink_isdu_process(&ctx);

    /* Verify response contains PD length */
    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Control */
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1); /* Data: PD length */
    /* Default device info has pdin_len = 2 */
    assert_int_equal(byte, 2);

    /* Test write protection */
    iolink_isdu_init(&ctx);
    uint8_t cmd_data[] = {0x05};
    assert_int_equal(isdu_send_write_request(&ctx, 0x001D, 0x00, cmd_data, 1), 1);

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
