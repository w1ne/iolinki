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

static int test_setup(void** state)
{
    (void) state;
    iolink_nvm_mock_cleanup();
    return 0;
}

static int test_teardown(void** state)
{
    (void) state;
    iolink_nvm_mock_cleanup();
    return 0;
}

static void test_isdu_vendor_name_read(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
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
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_events_init(&events);
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;

    /* 1. Initially status should be OK (0) */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DEVICE_STATUS, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t status_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, status_buf, sizeof(status_buf)), 1);
    assert_int_equal(status_buf[0], 0);

    /* 2. Trigger an error and check status again */
    iolink_event_trigger(&events, IOLINK_EVENT_COMM_CRC, IOLINK_EVENT_TYPE_ERROR);

    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DEVICE_STATUS, 0x00), 1);
    iolink_isdu_process(&ctx);

    assert_int_equal(isdu_collect_response(&ctx, status_buf, sizeof(status_buf)), 1);
    assert_int_equal(status_buf[0], 3); /* Failure */
}

static void test_isdu_detailed_device_status_read(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_events_ctx_t events;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_events_init(&events);
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;

    iolink_event_trigger(&events, 0x1801, IOLINK_EVENT_TYPE_ERROR);

    /* Read Detailed Status (Index 0x0025, Table B.8) */
    assert_int_equal(isdu_send_read_request(&ctx, 0x0025, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t detailed_buf[3];
    assert_int_equal(isdu_collect_response(&ctx, detailed_buf, sizeof(detailed_buf)), 3);
    /* EventQualifier per V1.1.5 (Figure A.24/Table A.17): MODE=appears(0xC0)
       | TYPE=error(0x03<<4=0x30) | SOURCE=device(0) | INSTANCE=Application(0x04) = 0xF4 */
    assert_int_equal(detailed_buf[0], 0xF4);
    assert_int_equal(detailed_buf[1], 0x18); /* Code High */
    assert_int_equal(detailed_buf[2], 0x01); /* Code Low */
}

/* CR443 (Package 2026 Interface D1.1.6-02, B.2.22 Table B.14 p. 270):
 * each DetailedDeviceStatus record is exactly [EventQualifier][EventCode MSB]
 * [EventCode LSB]. This pins the raw octet order so a regression to the old
 * "Octet 2,3: EventCode" wording (V1.1.5) cannot slip in unnoticed. */
static void test_isdu_detailed_device_status_cr443_octet_order(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_events_ctx_t events;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_events_init(&events);
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;

    /* One queued warning: 0x8CA0 (vendor-specific Device range). */
    iolink_event_trigger(&events, 0x8CA0, IOLINK_EVENT_TYPE_WARNING);

    /* Read DetailedDeviceStatus (Index 0x0025, Table B.14). */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0x00), 1);
    iolink_isdu_process(&ctx);

    /* EventQualifier per Figure A.24/Table A.17:
       MODE=event appears (0xC0) | TYPE=warning (0x02 << 4) | INSTANCE=Application (0x04). */
    uint8_t record[3];
    assert_int_equal(isdu_collect_response(&ctx, record, sizeof(record)), 3);
    assert_int_equal(record[0], 0xE4U); /* Octet 1: EventQualifier */
    assert_int_equal(record[1], 0x8CU); /* Octet 2: EventCode MSB */
    assert_int_equal(record[2], 0xA0U); /* Octet 3: EventCode LSB */

    const uint8_t expected[3] = {0xE4U, 0x8CU, 0xA0U};
    assert_memory_equal(record, expected, sizeof(expected));
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

    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);
    ctx.dll_ctx = &dll_ctx;

    /* Read Error Statistics (vendor object at Index 0x0040) */
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
    iolink_device_info_init(NULL);
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
    iolink_device_info_init(NULL);
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
    iolink_device_info_init(NULL);
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

/* Table 101: Device reset sets the diagnosis and status to "0"; Restore
 * factory settings and Back-to-box "Clear" it by resetting ErrorCount and
 * withdrawing the active Events; Application reset leaves it untouched. */
static void test_system_cmd_resets_diagnostics(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_dll_ctx_t dll;
    iolink_device_info_init(NULL);
    iolink_params_init();
    (void) memset(&dll, 0, sizeof(dll));
    iolink_events_init(&dll.events);
    dll.crc_errors = 3U;
    dll.timeout_errors = 2U;
    dll.framing_errors = 1U;
    dll.timing_errors = 4U;
    iolink_event_trigger(&dll.events, 0x1000U, IOLINK_EVENT_TYPE_ERROR);

    iolink_isdu_init(&ctx);
    ctx.dll_ctx = &dll;
    const uint8_t device_reset = IOLINK_CMD_DEVICE_RESET;
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00, &device_reset, 1), 1);
    iolink_isdu_process(&ctx);

    assert_int_equal(dll.crc_errors, 0U);
    assert_int_equal(dll.timeout_errors, 0U);
    assert_int_equal(dll.framing_errors, 0U);
    assert_int_equal(dll.timing_errors, 0U);
    assert_int_equal(dll.events.count, 0U);
    assert_false(iolink_events_flag(&dll.events));

    /* 0x82/0x83: ErrorCount is reset (10.7.4), the active Events stay queued
       so they can be withdrawn with "event disappears". */
    const uint8_t keep_event_commands[] = {IOLINK_CMD_RESTORE_FACTORY_SETTINGS,
                                           IOLINK_CMD_BACK_TO_BOX};
    for (size_t i = 0U; i < (sizeof(keep_event_commands) / sizeof(keep_event_commands[0])); i++) {
        (void) memset(&dll, 0, sizeof(dll));
        iolink_events_init(&dll.events);
        dll.crc_errors = 3U;
        dll.timeout_errors = 2U;
        dll.framing_errors = 1U;
        dll.timing_errors = 4U;
        iolink_event_trigger(&dll.events, 0x1000U, IOLINK_EVENT_TYPE_ERROR);

        iolink_isdu_init(&ctx);
        ctx.dll_ctx = &dll;
        const uint8_t cmd = keep_event_commands[i];
        assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00, &cmd, 1),
                         1);
        iolink_isdu_process(&ctx);

        assert_int_equal(dll.crc_errors, 0U);
        assert_int_equal(dll.timeout_errors, 0U);
        assert_int_equal(dll.framing_errors, 0U);
        assert_int_equal(dll.timing_errors, 0U);
        assert_int_equal(dll.events.count, 1U);
        assert_true(iolink_events_flag(&dll.events));
    }

    /* Application reset (0x81): Table 101 says "No". */
    (void) memset(&dll, 0, sizeof(dll));
    iolink_events_init(&dll.events);
    dll.crc_errors = 5U;
    iolink_event_trigger(&dll.events, 0x2000U, IOLINK_EVENT_TYPE_ERROR);

    iolink_isdu_init(&ctx);
    ctx.dll_ctx = &dll;
    const uint8_t app_reset = IOLINK_CMD_APPLICATION_RESET;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00, &app_reset, 1),
                     1);
    iolink_isdu_process(&ctx);

    assert_int_equal(dll.crc_errors, 5U);
    assert_int_equal(dll.events.count, 1U);
}

static void test_system_cmd_param_upload(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;

    /* Write 0x01 (ParamUploadStart) to Index 0x0002 */
    uint8_t cmd_data[] = {IOLINK_CMD_PARAM_UPLOAD_START};
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
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;

    /* Write 0x03 (ParamDownloadStart) to Index 0x0002 */
    uint8_t cmd_data[] = {IOLINK_CMD_PARAM_DOWNLOAD_START};
    assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);

    iolink_isdu_process(&ctx);

    /* Verify DS state changed to download */
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOAD_REQ);

    /* Verify success response */
    /* Verify success response (empty) */
    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0); /* No data */
}

/* 10.3.5/10.4.2: ParamDownloadStore ends the download and asks the Master to
 * upload the parameter set through the DS_UPLOAD_REQ Event. */
static void test_system_cmd_param_download_store(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_events_ctx_t events;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);
    iolink_events_init(&events);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    ctx.event_ctx = &events;

    const uint8_t download_start = IOLINK_CMD_PARAM_DOWNLOAD_START;
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00, &download_start, 1), 1);
    iolink_isdu_process(&ctx);
    iolink_ds_process(&ds); /* DOWNLOAD_REQ -> DOWNLOADING */

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    ctx.event_ctx = &events;
    const uint8_t store = IOLINK_CMD_PARAM_DOWNLOAD_STORE;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00, &store, 1), 1);
    iolink_isdu_process(&ctx);

    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0); /* No data */
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOAD_REQ);

    iolink_event_t event;
    assert_true(iolink_events_pop(&events, &event));
    assert_int_equal(event.code, IOLINK_EVENTCODE_DS_UPLOAD_REQUEST);
    assert_int_equal(event.type, IOLINK_EVENT_TYPE_NOTIFICATION);
    assert_false(iolink_events_pop(&events, &event));
}

/* ParamDownloadEnd does not start the upload. */
static void test_system_cmd_param_download_end_no_upload_request(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_events_ctx_t events;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);
    iolink_events_init(&events);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    ctx.event_ctx = &events;

    ds.state = IOLINK_DS_STATE_DOWNLOADING;
    const uint8_t download_end = IOLINK_CMD_PARAM_DOWNLOAD_END;
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00, &download_end, 1), 1);
    iolink_isdu_process(&ctx);

    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);
    iolink_event_t event;
    assert_false(iolink_events_pop(&events, &event));
}

static void test_system_cmd_param_break(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;

    /* Set DS to uploading state first */
    ds.state = IOLINK_DS_STATE_UPLOADING;

    /* Write 0x06 (ParamBreak) to Index 0x0002 */
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
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Write invalid command 0xFF to Index 0x0002 */
    uint8_t cmd_data[] = {0xFF};
    assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);

    iolink_isdu_process(&ctx);

    /* Verify error response: negative payload {0x80, AdditionalCode}. */
    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x35U); /* FUNC_NOTAVAIL (Table C.1 / C.2.14). */
}

static void test_isdu_function_tag_read_write(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
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
    iolink_device_info_init(NULL);
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
    iolink_dll_ctx_t dll = {0};
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Wire a DLL context with a configured PD-in length of 3 so the descriptor
       reflects the real runtime length rather than a hardcoded constant. */
    assert_int_equal(iolink_dll_set_pd_length(&dll, 3, 0), 0);
    ctx.dll_ctx = &dll;

    /* Read Process Data Input descriptor (Index 0x0028) */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_PROCESS_DATA_INPUT, 0x00), 1);

    iolink_isdu_process(&ctx);

    /* Verify response reports the configured PD-in length */
    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 3);

    /* Test write protection */
    iolink_isdu_init(&ctx);
    uint8_t cmd_data[] = {0x05};
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_PROCESS_DATA_INPUT, 0x00, cmd_data, 1), 1);

    iolink_isdu_process(&ctx);

    /* Verify write-protected error: {0x80, AdditionalCode}. */
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
}

static void test_isdu_data_storage_round_trip(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);
    iolink_ds_init(&ds, NULL);
    ctx.ds_ctx = &ds;

    /* Configure a known ApplicationTag. */
    const char* app = "ISDU-DS";
    assert_int_equal(
        iolink_params_set(IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) app, strlen(app), true),
        0);

    /* Upload (read 0x0003): Master backs up the serialized parameter image. */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DATA_STORAGE, 0x00), 1);
    iolink_isdu_process(&ctx);
    uint8_t image[IOLINK_DS_IMAGE_MAX];
    int img_len = isdu_collect_response(&ctx, image, sizeof(image));
    assert_true(img_len > 0);

    /* Wipe parameters. */
    iolink_params_factory_reset();
    uint8_t buf[40];
    assert_int_equal(iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf)), 0);

    /* Download (write 0x0003): Master restores the backed-up image. */
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_DATA_STORAGE, 0x00, image, (uint8_t) img_len), 1);
    iolink_isdu_process(&ctx);

    /* ApplicationTag restored over the wire. */
    int len = iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf));
    assert_int_equal(len, (int) strlen(app));
    assert_memory_equal(buf, app, strlen(app));
}

static void test_isdu_direct_parameters_page1(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Whole Direct Parameter page 1 (16 bytes). */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_1, 0x00), 1);
    iolink_isdu_process(&ctx);
    uint8_t page[16];
    assert_int_equal(isdu_collect_response(&ctx, page, sizeof(page)), 16);

    const iolink_device_info_t* info = iolink_device_info_get();
    assert_int_equal(page[0x04], 0x11);          /* RevisionID = v1.1 */
    assert_int_equal(page[0x03] & 0x01U, 0x01U); /* ISDU supported */
    assert_int_equal(page[0x07], (uint8_t) (info->vendor_id >> 8));
    assert_int_equal(page[0x08], (uint8_t) (info->vendor_id & 0xFFU));
    assert_int_equal(page[0x09], (uint8_t) ((info->device_id >> 16) & 0xFFU));
    assert_int_equal(page[0x0A], (uint8_t) ((info->device_id >> 8) & 0xFFU));
    assert_int_equal(page[0x0B], (uint8_t) (info->device_id & 0xFFU));

    /* Single-octet read via subindex (addr 0x04 -> subindex 5). */
    iolink_isdu_init(&ctx);
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_1, 0x05), 1);
    iolink_isdu_process(&ctx);
    uint8_t b[2];
    assert_int_equal(isdu_collect_response(&ctx, b, sizeof(b)), 1);
    assert_int_equal(b[0], 0x11);

    /* Page 1 is read-only. */
    iolink_isdu_init(&ctx);
    uint8_t w = 0x55;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_1, 0x01, &w, 1), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &w), 1);
    assert_int_equal(w, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &w), 1);
    assert_int_equal(w, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
}

static void test_isdu_direct_parameters_page2(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_isdu_init(&ctx);

    /* Write the whole page 2, then read it back. */
    uint8_t img[16];
    for (uint8_t i = 0U; i < 16U; i++) {
        img[i] = (uint8_t) (0xA0U + i);
    }
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_2, 0x00, img, 16),
                     1);
    iolink_isdu_process(&ctx);

    iolink_isdu_init(&ctx);
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_2, 0x00), 1);
    iolink_isdu_process(&ctx);
    uint8_t rb[16];
    assert_int_equal(isdu_collect_response(&ctx, rb, sizeof(rb)), 16);
    assert_memory_equal(rb, img, 16);

    /* Single-octet write at subindex 3, read back. */
    iolink_isdu_init(&ctx);
    uint8_t one = 0x5A;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_2, 0x03, &one, 1),
                     1);
    iolink_isdu_process(&ctx);

    iolink_isdu_init(&ctx);
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_2, 0x03), 1);
    iolink_isdu_process(&ctx);
    uint8_t one_rb[2];
    assert_int_equal(isdu_collect_response(&ctx, one_rb, sizeof(one_rb)), 1);
    assert_int_equal(one_rb[0], 0x5A);
}

static void test_isdu_vendor_device_id_not_available(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Table B.8: 0x000A/0x000B are reserved; VendorID/DeviceID live in DPP1
       (0x07-0x0B), not in an ISDU index. A read returns IDX_NOTAVAIL. */
    uint8_t byte;
    assert_int_equal(isdu_send_read_request(&ctx, 0x000AU, 0x00), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL);

    assert_int_equal(isdu_send_read_request(&ctx, 0x000BU, 0x00), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL);
}

static void test_isdu_reserved_indices_negative(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* 0x001C..0x001F are not used (C5); a read is a negative response. */
    assert_int_equal(isdu_send_read_request(&ctx, 0x001CU, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL);
}

/* C.2.3: a non-existent index answers IDX_NOTAVAIL, not SUBIDX_NOTAVAIL, even
 * when the request also carries a non-zero subindex. */
static void test_isdu_reserved_index_with_subindex(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    assert_int_equal(isdu_send_read_request(&ctx, 0x000AU, 0x01U), 1);
    iolink_isdu_process(&ctx);

    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL);
}

/* B.2.4: DeviceAccessLocks is accessible through subindex 0 only. */
static void test_isdu_access_locks_rejects_nonzero_subindex(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0x01U), 1);
    iolink_isdu_process(&ctx);

    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
}

/* Table B.14: 0x0025 subindex 0 is the whole list, subindex 1..n one entry. */
static void test_isdu_detailed_status_subindex_elements(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_events_ctx_t events;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_events_init(&events);
    iolink_event_trigger(&events, 0x1000U, IOLINK_EVENT_TYPE_ERROR);
    iolink_event_trigger(&events, 0x8CA0U, IOLINK_EVENT_TYPE_WARNING);

    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0x00U), 1);
    iolink_isdu_process(&ctx);
    uint8_t all[6];
    assert_int_equal(isdu_collect_response(&ctx, all, sizeof(all)), 6);
    assert_int_equal(all[1], 0x10U);
    assert_int_equal(all[4], 0x8CU);

    /* First element: the Error that was queued first. */
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0x01U), 1);
    iolink_isdu_process(&ctx);
    uint8_t first[3];
    assert_int_equal(isdu_collect_response(&ctx, first, sizeof(first)), 3);
    assert_int_equal(first[0] & 0x30U, 0x30U); /* TYPE = Error */
    assert_int_equal(first[1], 0x10U);
    assert_int_equal(first[2], 0x00U);

    /* Out-of-range subindex. */
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0x03U), 1);
    iolink_isdu_process(&ctx);
    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
}

/* B.8: this stack serves the Data Storage image through subindex 0 only. */
static void test_isdu_data_storage_subindex_not_available(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);
    iolink_ds_init(&ds, NULL);
    ctx.ds_ctx = &ds;

    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_DATA_STORAGE, 0x01U), 1);
    iolink_isdu_process(&ctx);

    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
}

/* B.2.5: ProfileCharacteristic is an ArrayT with one element in this stack. */
static void test_isdu_profile_characteristic_subindex(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_PROFILE_CHARACTERISTIC, 0x01U), 1);
    iolink_isdu_process(&ctx);
    uint8_t element[2];
    assert_int_equal(isdu_collect_response(&ctx, element, sizeof(element)), 2);

    iolink_isdu_init(&ctx);
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_PROFILE_CHARACTERISTIC, 0x02U), 1);
    iolink_isdu_process(&ctx);
    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
}

static void test_isdu_system_command_read_only_write(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* SystemCommand (0x0002) is write-only (Table B.8): a read is answered
       with IDX_NOT_ACCESSIBLE 0x80 0x23 (Table C.1). */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_NOT_ACCESSIBLE);

    /* A write to 0x0002 still executes the command. */
    iolink_isdu_init(&ctx);
    uint8_t cmd_data[] = {IOLINK_CMD_DEVICE_RESET};
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00, cmd_data, 1),
                     1);
    iolink_isdu_process(&ctx);
    assert_true(ctx.reset_pending);
    uint8_t resp_buf[1];
    assert_int_equal(isdu_collect_response(&ctx, resp_buf, sizeof(resp_buf)), 0);
}

static uint8_t page1_octet3(uint8_t m_seq_type, uint8_t pd_in, uint8_t pd_out)
{
    iolink_isdu_ctx_t ctx;
    iolink_dll_ctx_t dll;
    (void) memset(&dll, 0, sizeof(dll));
    dll.m_seq_type = m_seq_type;
    assert_int_equal(iolink_dll_set_pd_length(&dll, pd_in, pd_out), 0);
    iolink_isdu_init(&ctx);
    ctx.dll_ctx = &dll;
    return iolink_isdu_direct_param_page1_octet(&ctx, 0x03U);
}

static void test_isdu_mseq_capability_codes(void** state)
{
    (void) state;
    iolink_device_info_init(NULL);
    iolink_params_init();

    /* Table A.10 / plan Task 6. */
    assert_int_equal(page1_octet3(IOLINK_M_SEQ_TYPE_2_1, 1, 0), 0x01U); /* OD1, code 0 */
    assert_int_equal(page1_octet3(IOLINK_M_SEQ_TYPE_1_2, 0, 0), 0x03U); /* OD2, code 1 */
    assert_int_equal(page1_octet3(IOLINK_M_SEQ_TYPE_1_V, 0, 0), 0x0DU); /* OD8, code 6 */
    /* PREOPERATE bits 4-5 = 0 for TYPE_0. */
    assert_int_equal(page1_octet3(IOLINK_M_SEQ_TYPE_0, 0, 0) & 0x30U, 0x00U);
}

static void test_isdu_spec_index_constants(void** state)
{
    (void) state;
    assert_int_equal(IOLINK_IDX_DETAILED_DEVICE_STATUS, 0x0025U);
    assert_int_equal(IOLINK_IDX_DEVICE_STATUS, 0x0024U);
    assert_int_equal(IOLINK_IDX_ERROR_COUNT, 0x0020U);
    assert_int_equal(IOLINK_IDX_ERROR_STATS, 0x0040U);
}

static void test_isdu_write_rejects_read_only_identification(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Table C.1: a WRITE to a read-only identification object is access
       denied (0x80 0x23). */
    uint8_t data[] = {0x41U, 0x42U};
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_VENDOR_NAME, 0x00, data, sizeof(data)), 1);
    iolink_isdu_process(&ctx);

    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
}

static void test_isdu_param_lock_blocks_tag_write(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    iolink_device_info_set_access_locks(IOLINK_LOCK_PARAM);

    uint8_t data[] = {0x41U, 0x42U};
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_APPLICATION_TAG, 0x00, data, sizeof(data)), 1);
    iolink_isdu_process(&ctx);

    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
}

static unsigned g_vendor_writes = 0U;

static uint8_t vendor_write_spy(void* user, uint16_t index, uint8_t subindex, bool write,
                                const uint8_t* input, size_t length, uint8_t* output,
                                size_t* capacity)
{
    (void) user;
    (void) index;
    (void) subindex;
    (void) input;
    (void) length;
    (void) output;
    if (write) {
        g_vendor_writes++;
    }
    *capacity = 0U;
    return 0U;
}

/* B.2.4/10.6.5: the Parameter access lock covers every writable parameter, not
 * only the identification tags. */
static void test_isdu_param_lock_blocks_dpp2_write(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    uint8_t page2[16] = {0U};
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);
    ctx.direct_param_page2 = page2;

    iolink_device_info_set_access_locks(IOLINK_LOCK_PARAM);

    uint8_t data[] = {0x11U, 0x22U};
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_2, 0x00, data, sizeof(data)), 1);
    iolink_isdu_process(&ctx);

    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
    assert_int_equal(page2[0], 0U);
    assert_int_equal(page2[1], 0U);
}

/* B.2.27: device-specific parameters are writable parameters as well. */
static void test_isdu_param_lock_blocks_vendor_write(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);
    ctx.vendor_service = vendor_write_spy;

    g_vendor_writes = 0U;
    iolink_device_info_set_access_locks(IOLINK_LOCK_PARAM);

    const uint8_t value = 0x01U;
    assert_int_equal(isdu_send_write_request(&ctx, 0x0100U, 0x00, &value, 1), 1);
    iolink_isdu_process(&ctx);

    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
    assert_int_equal(g_vendor_writes, 0U);
}

/* B.2.4: DeviceAccessLocks stays writable, and neither SystemCommand nor the
 * Data Storage download are parameters. */
static void test_isdu_param_lock_exempt_writes(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;

    iolink_device_info_set_access_locks(IOLINK_LOCK_PARAM);

    uint8_t locks[2] = {0x00U, 0x01U}; /* keep the Parameter lock set */
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0x00, locks, 2),
                     1);
    iolink_isdu_process(&ctx);
    uint8_t resp[2];
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);
    assert_int_equal(iolink_device_info_get_access_locks(), IOLINK_LOCK_PARAM);

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    const uint8_t reset_cmd = IOLINK_CMD_APPLICATION_RESET;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00, &reset_cmd, 1),
                     1);
    iolink_isdu_process(&ctx);
    assert_true(ctx.app_reset_pending);
    assert_int_equal(isdu_collect_response(&ctx, resp, sizeof(resp)), 0);

    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(isdu_send_write_request(&ctx, IOLINK_IDX_DATA_STORAGE, 0x00, NULL, 0), 1);
    iolink_isdu_process(&ctx);
    uint8_t image_resp[2];
    const int image_len = isdu_collect_response(&ctx, image_resp, sizeof(image_resp));
    assert_true(image_len == 0);
}

static void test_isdu_error_count_read(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* ErrorCount (0x0020) is a 2-octet read-only object; zero without a DLL. */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_ERROR_COUNT, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t buf[2];
    assert_int_equal(isdu_collect_response(&ctx, buf, sizeof(buf)), 2);
    assert_int_equal(buf[0], 0);
    assert_int_equal(buf[1], 0);
}

/* B.2.20: this Device reports the SDCI error counters since power-on/reset,
 * big-endian and saturated at 0xFFFF. */
static void test_isdu_error_count_sums_dll_errors(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_dll_ctx_t dll;
    iolink_device_info_init(NULL);
    iolink_params_init();
    (void) memset(&dll, 0, sizeof(dll));
    iolink_events_init(&dll.events);
    dll.crc_errors = 2U;
    dll.framing_errors = 3U;
    dll.timeout_errors = 4U;
    dll.timing_errors = 1U;

    iolink_isdu_init(&ctx);
    ctx.dll_ctx = &dll;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_ERROR_COUNT, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t buf[2];
    assert_int_equal(isdu_collect_response(&ctx, buf, sizeof(buf)), 2);
    assert_int_equal(buf[0], 0x00U);
    assert_int_equal(buf[1], 0x0AU);

    dll.crc_errors = 0x10000U;
    iolink_isdu_init(&ctx);
    ctx.dll_ctx = &dll;
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_ERROR_COUNT, 0x00), 1);
    iolink_isdu_process(&ctx);
    assert_int_equal(isdu_collect_response(&ctx, buf, sizeof(buf)), 2);
    assert_int_equal(buf[0], 0xFFU);
    assert_int_equal(buf[1], 0xFFU);
}

static void test_isdu_product_uri_read(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_t info;
    (void) memset(&info, 0, sizeof(info));
    info.product_uri = "https://example.org/iolinki";
    iolink_device_info_init(&info);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_PRODUCT_URI, 0x00), 1);
    iolink_isdu_process(&ctx);

    char uri[64];
    int len = isdu_collect_response(&ctx, (uint8_t*) uri, sizeof(uri) - 1);
    assert_int_equal(len, (int) strlen("https://example.org/iolinki"));
    uri[len] = '\0';
    assert_string_equal(uri, "https://example.org/iolinki");
}

static void test_isdu_product_uri_absent(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL); /* default info: product_uri is NULL */
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* B.2.19: ProductURI is conditional. Without one, the object is not
       available and the read is a negative response (Table C.1). */
    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_PRODUCT_URI, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t byte;
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, 0x80);
    assert_int_equal(iolink_isdu_get_response_byte(&ctx, &byte), 1);
    assert_int_equal(byte, IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_isdu_vendor_name_read, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_device_status_read, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_detailed_device_status_read, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_detailed_device_status_cr443_octet_order,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_error_stats_read, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_vendor_device_id_not_available, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_reserved_indices_negative, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_reserved_index_with_subindex, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_access_locks_rejects_nonzero_subindex, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_detailed_status_subindex_elements, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_data_storage_subindex_not_available, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_profile_characteristic_subindex, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_mseq_capability_codes, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_spec_index_constants, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_system_command_read_only_write, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_write_rejects_read_only_identification,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_param_lock_blocks_tag_write, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_param_lock_blocks_dpp2_write, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_param_lock_blocks_vendor_write, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_param_lock_exempt_writes, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_error_count_read, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_error_count_sums_dll_errors, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_product_uri_read, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_product_uri_absent, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_device_reset, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_application_reset, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_factory_restore, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_resets_diagnostics, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_param_upload, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_param_download, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_param_break, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_param_download_store, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_param_download_end_no_upload_request,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_invalid, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_function_tag_read_write, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_location_tag_read_write, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_pdin_descriptor_read, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_direct_parameters_page1, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_direct_parameters_page2, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_data_storage_round_trip, test_setup,
                                        test_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
