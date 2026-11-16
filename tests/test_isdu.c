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

/* Reset service stub (isdu.h system_reset): records queries and executions. */
static int g_reset_queries;
static int g_reset_execs;
static uint8_t g_reset_last_exec;

static int stub_system_reset(void* user, uint8_t cmd, bool execute)
{
    (void) user;
    if (execute) {
        g_reset_execs++;
        g_reset_last_exec = cmd;
    }
    else {
        g_reset_queries++;
    }
    return 0;
}

static void install_reset_stub(iolink_isdu_ctx_t* ctx)
{
    g_reset_queries = 0;
    g_reset_execs = 0;
    g_reset_last_exec = 0U;
    ctx->system_reset = stub_system_reset;
    ctx->system_reset_user = NULL;
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
    /* Table B.13: an Error maps to Failure (4); 3 is Functional check. */
    assert_int_equal(status_buf[0], IOLINK_DEVICE_STATUS_FAILURE);
    assert_int_equal(status_buf[0], 4);
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

    /* Without a reset service the command cannot be executed: FUNC_NOTAVAIL
       and no pending reset (10.7.2, Table 99). */
    uint8_t cmd_data[] = {IOLINK_CMD_DEVICE_RESET};
    assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);
    iolink_isdu_process(&ctx);
    assert_false(ctx.reset_pending);
    uint8_t neg[2];
    assert_int_equal(isdu_collect_response(&ctx, neg, sizeof(neg)), 2);
    assert_int_equal(neg[0], 0x80U);
    assert_int_equal(neg[1], IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL);

    /* With a reset service: Write 0x80 to Index 0x0002 */
    iolink_isdu_init(&ctx);
    install_reset_stub(&ctx);
    assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);

    iolink_isdu_process(&ctx);

    /* Verify reset flag is set; the warm start itself is deferred until the
       response was read (device layer), so nothing executed yet. */
    assert_true(ctx.reset_pending);
    assert_int_equal(g_reset_queries, 1);
    assert_int_equal(g_reset_execs, 0);

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

    /* Without a reset service: FUNC_NOTAVAIL, nothing executed. */
    uint8_t cmd_data[] = {IOLINK_CMD_APPLICATION_RESET};
    assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);
    iolink_isdu_process(&ctx);
    assert_false(ctx.app_reset_pending);
    uint8_t neg[2];
    assert_int_equal(isdu_collect_response(&ctx, neg, sizeof(neg)), 2);
    assert_int_equal(neg[0], 0x80U);
    assert_int_equal(neg[1], IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL);

    /* With a reset service: Write 0x81 to Index 0x0002 */
    iolink_isdu_init(&ctx);
    install_reset_stub(&ctx);
    assert_int_equal(isdu_send_write_request(&ctx, 0x0002, 0x00, cmd_data, 1), 1);

    iolink_isdu_process(&ctx);

    /* Verify the application reset ran before the response (10.7.3). */
    assert_true(ctx.app_reset_pending);
    assert_int_equal(g_reset_execs, 1);
    assert_int_equal(g_reset_last_exec, IOLINK_CMD_APPLICATION_RESET);

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

    /* Corrected expectation: ParamUploadStart is 0x01 (Table B.9). The former
       value 0x95 is in the reserved range 0x84-0x9F and is now refused. */
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

    /* Corrected expectation: ParamDownloadStart is 0x03 (Table B.9). The former
       value 0x96 is in the reserved range 0x84-0x9F and is now refused. */
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

    /* Corrected expectation: ParamBreak is 0x06 (Table B.9), formerly 0x97. */
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

/* Corrected expectation: Index 0x0028 is ProcessDataInput, the last valid
 * PDin content (Table B.8, B.2.23), not a one-octet PD length descriptor. */
static void test_isdu_pdin_descriptor_read(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_dll_ctx_t dll = {0};
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    assert_int_equal(iolink_dll_set_pd_length(&dll, 3, 0), 0);
    const uint8_t pdin[3] = {0xA1U, 0xB2U, 0xC3U};
    assert_int_equal(iolink_dll_publish_pd_in(&dll, pdin, 3U, true), 0);
    ctx.dll_ctx = &dll;

    assert_int_equal(isdu_send_read_request(&ctx, IOLINK_IDX_PROCESS_DATA_INPUT, 0x00), 1);
    iolink_isdu_process(&ctx);

    uint8_t data[4];
    assert_int_equal(isdu_collect_response(&ctx, data, sizeof(data)), 3);
    assert_int_equal(data[0], 0xA1U);
    assert_int_equal(data[1], 0xB2U);
    assert_int_equal(data[2], 0xC3U);

    /* Test write protection */
    iolink_isdu_init(&ctx);
    ctx.dll_ctx = &dll;
    uint8_t cmd_data[] = {0x05};
    assert_int_equal(
        isdu_send_write_request(&ctx, IOLINK_IDX_PROCESS_DATA_INPUT, 0x00, cmd_data, 1), 1);

    iolink_isdu_process(&ctx);

    /* Verify write-protected error: {0x80, AdditionalCode}. */
    uint8_t byte;
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

/* Corrected expectation: Indices 0x0004-0x000B are reserved (Table B.8). The
 * three-octet DeviceID is read through Direct Parameter page 1 addresses
 * 0x09-0x0B (Subindex = address + 1), and the former nonstandard Index 0x000B
 * is IDX_NOTAVAIL. */
static void test_isdu_device_id_three_octets(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_t info;
    (void) memset(&info, 0, sizeof(info));
    info.vendor_name = "v";
    info.vendor_text = "v";
    info.product_name = "p";
    info.product_id = "i";
    info.product_text = "t";
    info.serial_number = "s";
    info.hardware_revision = "h";
    info.firmware_revision = "f";
    info.device_id = 0x010203U;
    iolink_device_info_init(&info);
    iolink_params_init();

    const uint8_t expected[3] = {0x01U, 0x02U, 0x03U};
    for (uint8_t i = 0U; i < 3U; i++) {
        iolink_isdu_init(&ctx);
        assert_int_equal(
            isdu_send_read_request(&ctx, IOLINK_IDX_DIRECT_PARAMETERS_1, (uint8_t) (0x0AU + i)),
            1);
        iolink_isdu_process(&ctx);
        uint8_t data[2];
        assert_int_equal(isdu_collect_response(&ctx, data, sizeof(data)), 1);
        assert_int_equal(data[0], expected[i]);
    }

    iolink_isdu_init(&ctx);
    assert_int_equal(isdu_send_read_request(&ctx, 0x000BU, 0x00), 1);
    iolink_isdu_process(&ctx);
    uint8_t err[2];
    assert_int_equal(isdu_collect_response(&ctx, err, sizeof(err)), 2);
    assert_int_equal(err[0], 0x80U);
    assert_int_equal(err[1], IOLINK_ISDU_ERROR_INDEX_NOT_AVAIL);
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
    install_reset_stub(&ctx);
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

/* ---- Regression tests: Table B.8 / Table B.9 conformance (V1.1.5) ---- */

/** Run one request through the service layer and return the payload length. */
static int isdu_run_read(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex, uint8_t* out,
                         size_t out_size)
{
    assert_int_equal(isdu_send_read_request(ctx, index, subindex), 1);
    iolink_isdu_process(ctx);
    return isdu_collect_response(ctx, out, out_size);
}

static int isdu_run_write(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex,
                          const uint8_t* data, uint8_t len, uint8_t* out, size_t out_size)
{
    assert_int_equal(isdu_send_write_request(ctx, index, subindex, data, len), 1);
    iolink_isdu_process(ctx);
    return isdu_collect_response(ctx, out, out_size);
}

/** Assert a negative response {0x80, AdditionalCode} (Table C.1). */
static void assert_isdu_error(const uint8_t* resp, int len, uint8_t additional_code)
{
    assert_int_equal(len, 2);
    assert_int_equal(resp[0], 0x80U);
    assert_int_equal(resp[1], additional_code);
}

static void test_system_cmd_codes_table_b9(void** state)
{
    (void) state;
    assert_int_equal(IOLINK_CMD_PARAM_UPLOAD_START, 0x01U);
    assert_int_equal(IOLINK_CMD_PARAM_UPLOAD_END, 0x02U);
    assert_int_equal(IOLINK_CMD_PARAM_DOWNLOAD_START, 0x03U);
    assert_int_equal(IOLINK_CMD_PARAM_DOWNLOAD_END, 0x04U);
    assert_int_equal(IOLINK_CMD_PARAM_DOWNLOAD_STORE, 0x05U);
    assert_int_equal(IOLINK_CMD_PARAM_BREAK, 0x06U);
    assert_int_equal(IOLINK_CMD_DEVICE_RESET, 0x80U);
    assert_int_equal(IOLINK_CMD_APPLICATION_RESET, 0x81U);
    assert_int_equal(IOLINK_CMD_RESTORE_FACTORY_SETTINGS, 0x82U);
    assert_int_equal(IOLINK_CMD_BACK_TO_BOX, 0x83U);
    /* Table C.1: PAR_SETINVALID 0x8040, PAR_SETINCONSIST 0x8041 (Table 98 rule 7). */
    assert_int_equal(IOLINK_ISDU_ERROR_PARAM_SET_INVALID, 0x40U);
    assert_int_equal(IOLINK_ISDU_ERROR_PARAM_INCONSISTENT, 0x41U);
    /* Table B.10 DataStorageIndex subindices and Table 55 control code. */
    assert_int_equal(IOLINK_DS_SUBIDX_COMMAND, 1U);
    assert_int_equal(IOLINK_DS_SUBIDX_INDEX_LIST, 5U);
    assert_int_equal(IOLINK_CMD_PD_OUTPUT_OPERATE, 0x98U);
    assert_int_equal(IOLINK_IDX_PRODUCT_URI, 0x001BU);
    assert_int_equal(IOLINK_IDX_PROCESS_DATA_INPUT, 0x0028U);
    assert_int_equal(IOLINK_IDX_PROCESS_DATA_OUTPUT, 0x0029U);
}

/* Table B.9 reserved, profile and vendor values, the former nonstandard
 * 0x84/0x95/0x96/0x97 codes and the unimplemented Back-to-box are refused with
 * FUNC_NOTAVAIL (Table 99, C.2.14) and have no side effect. */
static void test_system_cmd_reserved_values_rejected(void** state)
{
    (void) state;
    static const uint8_t refused[] = {0x00U, 0x07U, 0x08U, 0x09U, 0x3FU, 0x40U, 0x7FU, 0x83U,
                                      0x84U, 0x95U, 0x96U, 0x97U, 0x9FU, 0xA0U, 0xFFU};
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);

    const uint8_t tag[] = "KeepTag";
    assert_int_equal(iolink_params_set(IOLINK_IDX_APPLICATION_TAG, 0U, tag, sizeof(tag) - 1U, false),
                     0);

    for (size_t i = 0U; i < sizeof(refused); i++) {
        iolink_isdu_init(&ctx);
        ctx.ds_ctx = &ds;
        uint8_t resp[4];
        int len = isdu_run_write(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00, &refused[i], 1U, resp,
                                 sizeof(resp));
        assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL);
        assert_false(ctx.reset_pending);
        assert_false(ctx.app_reset_pending);
        assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);
    }

    /* Back-to-box (0x83) did not apply a factory reset. */
    uint8_t rb[40];
    assert_int_equal(iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0U, rb, sizeof(rb)),
                     (int) (sizeof(tag) - 1U));
}

/* SystemCommand is exactly one octet without Subindex structure. */
static void test_system_cmd_length_and_subindex(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    uint8_t resp[4];
    iolink_device_info_init(NULL);
    iolink_params_init();

    iolink_isdu_init(&ctx);
    int len = isdu_run_write(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00, NULL, 0U, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_LENGTH_UNDERRUN);

    iolink_isdu_init(&ctx);
    const uint8_t two[2] = {IOLINK_CMD_DEVICE_RESET, 0x00U};
    len = isdu_run_write(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00, two, 2U, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_LENGTH_OVERRUN);
    assert_false(ctx.reset_pending);

    iolink_isdu_init(&ctx);
    const uint8_t one = IOLINK_CMD_DEVICE_RESET;
    len = isdu_run_write(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x01, &one, 1U, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
    assert_false(ctx.reset_pending);
}

/* Without a Data Storage engine the Block Parameterization commands are not
 * implemented: they must not return a positive response. */
static void test_system_cmd_param_without_ds(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_init(NULL);
    iolink_params_init();

    for (uint8_t cmd = IOLINK_CMD_PARAM_UPLOAD_START; cmd <= IOLINK_CMD_PARAM_BREAK; cmd++) {
        iolink_isdu_init(&ctx);
        uint8_t resp[4];
        int len =
            isdu_run_write(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00, &cmd, 1U, resp, sizeof(resp));
        assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL);
    }
}

/* Upload and download blocks through SystemCommand 0x01-0x04 and 0x06. */
static void test_system_cmd_block_param_sequences(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    uint8_t resp[4];
    int len;
    iolink_device_info_init(NULL);
    iolink_device_info_set_access_locks(0U);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);

    /* End without Start: FUNC_UNAVAILTEMP (Table 99 priority 3). */
    const uint8_t up_end = IOLINK_CMD_PARAM_UPLOAD_END;
    const uint8_t dn_end = IOLINK_CMD_PARAM_DOWNLOAD_END;
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    len = isdu_run_write(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0, &up_end, 1U, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_FUNC_TEMP_UNAVAIL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    len = isdu_run_write(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0, &dn_end, 1U, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_FUNC_TEMP_UNAVAIL);

    /* Upload block: Start, a second Start is refused, End closes it. */
    const uint8_t up_start = IOLINK_CMD_PARAM_UPLOAD_START;
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(
        isdu_run_write(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0, &up_start, 1U, resp, sizeof(resp)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOAD_REQ);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    len = isdu_run_write(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0, &up_start, 1U, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_FUNC_TEMP_UNAVAIL);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(
        isdu_run_write(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0, &up_end, 1U, resp, sizeof(resp)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);

    /* Download block: Start, End closes it. */
    const uint8_t dn_start = IOLINK_CMD_PARAM_DOWNLOAD_START;
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(
        isdu_run_write(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0, &dn_start, 1U, resp, sizeof(resp)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOAD_REQ);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(
        isdu_run_write(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0, &dn_end, 1U, resp, sizeof(resp)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);

    /* Data Storage lock (B.2.4): it applies to the DataStorageIndex only.
       Corrected expectation: Block Parameterization stays available, while a
       DS_Command write to 0x0003 is refused with "access denied" (0x8023)
       and leaves the DS state unchanged. */
    iolink_device_info_set_access_locks(IOLINK_LOCK_DS);
    const uint8_t ds_dn_start = IOLINK_DS_COMMAND_DOWNLOAD_START;
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    len = isdu_run_write(&ctx, IOLINK_IDX_DATA_STORAGE, IOLINK_DS_SUBIDX_COMMAND, &ds_dn_start,
                         1U, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_NOT_ACCESSIBLE);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(
        isdu_run_write(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0, &dn_start, 1U, resp, sizeof(resp)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOAD_REQ);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(
        isdu_run_write(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0, &dn_end, 1U, resp, sizeof(resp)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);
    iolink_device_info_set_access_locks(0U);

    /* ParamBreak cancels an open block. */
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(
        isdu_run_write(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0, &dn_start, 1U, resp, sizeof(resp)), 0);
    const uint8_t brk = IOLINK_CMD_PARAM_BREAK;
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    assert_int_equal(
        isdu_run_write(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0, &brk, 1U, resp, sizeof(resp)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);
}

static bool events_contain(iolink_events_ctx_t* events, uint16_t code)
{
    iolink_event_t list[IOLINK_EVENT_QUEUE_SIZE];
    uint8_t n = iolink_events_get_all(events, list, (uint8_t) IOLINK_EVENT_QUEUE_SIZE);
    for (uint8_t i = 0U; i < n; i++) {
        if (list[i].code == code) {
            return true;
        }
    }
    return false;
}

/* ParamDownloadStore closes the download block and raises DS_UPLOAD_REQ
 * (10.3.5, 10.4.2, Table D.1 EventCode 0xFF91). */
static void test_system_cmd_param_download_store(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_ds_ctx_t ds;
    iolink_events_ctx_t events;
    uint8_t resp[4];
    int len;
    iolink_device_info_init(NULL);
    iolink_device_info_set_access_locks(0U);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);
    iolink_events_init(&events);

    const uint8_t store = IOLINK_CMD_PARAM_DOWNLOAD_STORE;
    const uint8_t dn_start = IOLINK_CMD_PARAM_DOWNLOAD_START;

    /* No event channel: the upload request cannot be signalled. */
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    len = isdu_run_write(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0, &store, 1U, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL);

    /* Open download block, then Store. */
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    ctx.event_ctx = &events;
    assert_int_equal(
        isdu_run_write(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0, &dn_start, 1U, resp, sizeof(resp)), 0);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    ctx.event_ctx = &events;
    assert_int_equal(
        isdu_run_write(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0, &store, 1U, resp, sizeof(resp)), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);
    assert_true(events_contain(&events, IOLINK_EVENTCODE_DS_UPLOAD_REQUEST));

    /* Store during an upload block is refused. */
    iolink_events_init(&events);
    ds.state = IOLINK_DS_STATE_UPLOADING;
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    ctx.event_ctx = &events;
    len = isdu_run_write(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0, &store, 1U, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_FUNC_TEMP_UNAVAIL);
    assert_false(events_contain(&events, IOLINK_EVENTCODE_DS_UPLOAD_REQUEST));
    ds.state = IOLINK_DS_STATE_IDLE;

    /* 10.6.6: a Data Storage lock suppresses the DS_UPLOAD_REQ Event. */
    iolink_device_info_set_access_locks(IOLINK_LOCK_DS);
    iolink_isdu_init(&ctx);
    ctx.ds_ctx = &ds;
    ctx.event_ctx = &events;
    assert_int_equal(
        isdu_run_write(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0, &store, 1U, resp, sizeof(resp)), 0);
    assert_false(events_contain(&events, IOLINK_EVENTCODE_DS_UPLOAD_REQUEST));
    iolink_device_info_set_access_locks(0U);
}

/* Restore factory settings clears ErrorCount (10.7.4). */
static void test_isdu_error_count(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    uint8_t resp[4];
    int len;
    iolink_device_info_init(NULL);
    iolink_params_init();

    iolink_isdu_init(&ctx);
    ctx.error_count = 0x1234U;
    len = isdu_run_read(&ctx, IOLINK_IDX_ERROR_COUNT, 0x00, resp, sizeof(resp));
    assert_int_equal(len, 2);
    assert_int_equal(resp[0], 0x12U);
    assert_int_equal(resp[1], 0x34U);

    const uint8_t w[2] = {0x00U, 0x00U};
    len = isdu_run_write(&ctx, IOLINK_IDX_ERROR_COUNT, 0x00, w, 2U, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_NOT_ACCESSIBLE);
    assert_int_equal(ctx.error_count, 0x1234U);

    len = isdu_run_read(&ctx, IOLINK_IDX_ERROR_COUNT, 0x01, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);

    const uint8_t cmd = IOLINK_CMD_RESTORE_FACTORY_SETTINGS;
    assert_int_equal(
        isdu_run_write(&ctx, IOLINK_IDX_SYSTEM_COMMAND, 0x00, &cmd, 1U, resp, sizeof(resp)), 0);
    assert_int_equal(ctx.error_count, 0U);
}

static void init_identity(iolink_device_info_t* info)
{
    (void) memset(info, 0, sizeof(*info));
    info->vendor_name = "Vendor";
    info->product_name = "Product";
}

/* ProductURI (0x001B) and ProfileCharacteristic (0x000D) are conditional:
 * IDX_NOTAVAIL until configured, then read-only. */
static void test_isdu_conditional_profile_objects(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_t info;
    uint8_t resp[128];
    int len;
    init_identity(&info);
    iolink_device_info_init(&info);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    len = isdu_run_read(&ctx, IOLINK_IDX_PRODUCT_URI, 0x00, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_INDEX_NOT_AVAIL);
    const uint8_t x = 0x41U;
    len = isdu_run_write(&ctx, IOLINK_IDX_PRODUCT_URI, 0x00, &x, 1U, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_INDEX_NOT_AVAIL);
    len = isdu_run_read(&ctx, IOLINK_IDX_PROFILE_CHARACTERISTIC, 0x00, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_INDEX_NOT_AVAIL);

    /* ProductURI is bounded to 100 octets (B.2.19). */
    static char long_uri[121];
    (void) memset(long_uri, 'u', sizeof(long_uri) - 1U);
    long_uri[sizeof(long_uri) - 1U] = '\0';
    info.product_uri = long_uri;
    info.profile_characteristic = 0x4000U;
    iolink_device_info_init(&info);

    len = isdu_run_read(&ctx, IOLINK_IDX_PRODUCT_URI, 0x00, resp, sizeof(resp));
    assert_int_equal(len, 100);
    assert_memory_equal(resp, long_uri, 100);
    len = isdu_run_write(&ctx, IOLINK_IDX_PRODUCT_URI, 0x00, &x, 1U, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_NOT_ACCESSIBLE);
    len = isdu_run_read(&ctx, IOLINK_IDX_PRODUCT_URI, 0x02, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);

    len = isdu_run_read(&ctx, IOLINK_IDX_PROFILE_CHARACTERISTIC, 0x00, resp, sizeof(resp));
    assert_int_equal(len, 2);
    assert_int_equal(resp[0], 0x40U);
    assert_int_equal(resp[1], 0x00U);
    const uint8_t pid[2] = {0x00U, 0x01U};
    len = isdu_run_write(&ctx, IOLINK_IDX_PROFILE_CHARACTERISTIC, 0x00, pid, 2U, resp,
                         sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_NOT_ACCESSIBLE);
    len = isdu_run_read(&ctx, IOLINK_IDX_PROFILE_CHARACTERISTIC, 0x01, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
}

/* Identification strings are read-only; optional ones that are not configured
 * do not exist; mandatory ones always answer. */
static void test_isdu_identification_access_rules(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_device_info_t info;
    uint8_t resp[80];
    int len;
    init_identity(&info);
    info.serial_number = "0123456789ABCDEF-TOO-LONG";
    iolink_device_info_init(&info);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    const uint8_t w[] = {'X'};
    len = isdu_run_write(&ctx, IOLINK_IDX_VENDOR_NAME, 0x00, w, 1U, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_NOT_ACCESSIBLE);
    len = isdu_run_read(&ctx, IOLINK_IDX_PRODUCT_NAME, 0x01, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
    len = isdu_run_read(&ctx, IOLINK_IDX_VENDOR_TEXT, 0x00, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_INDEX_NOT_AVAIL);
    len = isdu_run_read(&ctx, IOLINK_IDX_SERIAL_NUMBER, 0x00, resp, sizeof(resp));
    assert_int_equal(len, 16); /* B.2.13: max. 16 octets */
    assert_memory_equal(resp, "0123456789ABCDEF", 16);

    /* Mandatory ProductName without configuration reads as an empty string. */
    info.product_name = NULL;
    iolink_device_info_init(&info);
    len = isdu_run_read(&ctx, IOLINK_IDX_PRODUCT_NAME, 0x00, resp, sizeof(resp));
    assert_int_equal(len, 0);
    assert_int_equal(ctx.state, ISDU_STATE_RESPONSE_READY);

    /* Former VendorID index 0x000A and reserved 0x0004 are not available. */
    len = isdu_run_read(&ctx, 0x000AU, 0x00, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_INDEX_NOT_AVAIL);
    len = isdu_run_read(&ctx, 0x0004U, 0x00, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_INDEX_NOT_AVAIL);

    /* Tags keep R/W access but have no Subindex structure. */
    len = isdu_run_write(&ctx, IOLINK_IDX_APPLICATION_TAG, 0x01, w, 1U, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
}

/* DeviceStatus/DetailedDeviceStatus are read-only; a write gets a negative
 * response instead of a dropped service. */
static void test_isdu_status_objects_read_only(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_events_ctx_t events;
    uint8_t resp[32];
    int len;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_events_init(&events);
    iolink_isdu_init(&ctx);
    ctx.event_ctx = &events;

    const uint8_t w = 0x00U;
    len = isdu_run_write(&ctx, IOLINK_IDX_DEVICE_STATUS, 0x00, &w, 1U, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_NOT_ACCESSIBLE);
    len = isdu_run_write(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0x00, &w, 1U, resp,
                         sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_NOT_ACCESSIBLE);
    len = isdu_run_read(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0x01, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);

    /* No event context: an empty DetailedDeviceStatus array. */
    ctx.event_ctx = NULL;
    len = isdu_run_read(&ctx, IOLINK_IDX_DETAILED_DEVICE_STATUS, 0x00, resp, sizeof(resp));
    assert_int_equal(len, 0);
    assert_int_equal(ctx.state, ISDU_STATE_RESPONSE_READY);
}

/* ProcessDataInput 0x0028 (B.2.23): last valid PDin. Covers no DLL, zero
 * length, invalid-before-valid, valid-then-invalid, length change and
 * Subindex/write guards. */
static void test_isdu_process_data_input_guards(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_dll_ctx_t dll;
    uint8_t resp[IOLINK_PD_IN_MAX_SIZE + 2U];
    int len;
    (void) memset(&dll, 0, sizeof(dll));
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    len = isdu_run_read(&ctx, IOLINK_IDX_PROCESS_DATA_INPUT, 0x00, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_INDEX_NOT_AVAIL);

    ctx.dll_ctx = &dll;
    assert_int_equal(iolink_dll_set_pd_length(&dll, 0, 0), 0);
    dll.pd_valid = true;
    len = isdu_run_read(&ctx, IOLINK_IDX_PROCESS_DATA_INPUT, 0x00, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_INDEX_NOT_AVAIL);

    /* Invalid before any valid data: no last valid data exists. */
    assert_int_equal(iolink_dll_set_pd_length(&dll, 2, 0), 0);
    const uint8_t first[2] = {0x5AU, 0xA5U};
    assert_int_equal(iolink_dll_publish_pd_in(&dll, first, 2U, false), 0);
    len = isdu_run_read(&ctx, IOLINK_IDX_PROCESS_DATA_INPUT, 0x00, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_SERV_TEMP_UNAVAIL);

    /* Valid data becomes the snapshot. */
    assert_int_equal(iolink_dll_publish_pd_in(&dll, first, 2U, true), 0);
    len = isdu_run_read(&ctx, IOLINK_IDX_PROCESS_DATA_INPUT, 0x01, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
    len = isdu_run_read(&ctx, IOLINK_IDX_PROCESS_DATA_INPUT, 0x00, resp, sizeof(resp));
    assert_int_equal(len, 2);
    assert_int_equal(resp[0], 0x5AU);
    assert_int_equal(resp[1], 0xA5U);

    /* Valid then invalid: the last valid data is kept, not the current data. */
    const uint8_t bad[2] = {0xEEU, 0xEEU};
    assert_int_equal(iolink_dll_publish_pd_in(&dll, bad, 2U, false), 0);
    assert_false(dll.pd_valid);
    len = isdu_run_read(&ctx, IOLINK_IDX_PROCESS_DATA_INPUT, 0x00, resp, sizeof(resp));
    assert_int_equal(len, 2);
    assert_int_equal(resp[0], 0x5AU);
    assert_int_equal(resp[1], 0xA5U);

    /* A newer valid value replaces the snapshot. */
    const uint8_t second[2] = {0x01U, 0x02U};
    assert_int_equal(iolink_dll_publish_pd_in(&dll, second, 2U, true), 0);
    len = isdu_run_read(&ctx, IOLINK_IDX_PROCESS_DATA_INPUT, 0x00, resp, sizeof(resp));
    assert_int_equal(len, 2);
    assert_int_equal(resp[0], 0x01U);
    assert_int_equal(resp[1], 0x02U);

    /* Write is refused, snapshot unchanged. */
    len = isdu_run_write(&ctx, IOLINK_IDX_PROCESS_DATA_INPUT, 0x00, bad, 1U, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_NOT_ACCESSIBLE);

    /* Buffer boundary: the full IOLINK_PD_IN_MAX_SIZE structure round-trips. */
    uint8_t full[IOLINK_PD_IN_MAX_SIZE];
    for (size_t i = 0U; i < sizeof(full); i++) {
        full[i] = (uint8_t) (0x80U + i);
    }
    assert_int_equal(iolink_dll_publish_pd_in(&dll, full, (uint8_t) sizeof(full), true), 0);
    len = isdu_run_read(&ctx, IOLINK_IDX_PROCESS_DATA_INPUT, 0x00, resp, sizeof(resp));
    assert_int_equal(len, (int) sizeof(full));
    assert_memory_equal(resp, full, sizeof(full));
    assert_int_equal(iolink_dll_publish_pd_in(&dll, full, IOLINK_PD_IN_MAX_SIZE + 1U, true), -1);
    uint8_t small[2];
    assert_int_equal(iolink_dll_get_last_valid_pd_in(&dll, small, sizeof(small)), 0U);

    /* A changed PD structure drops the old snapshot. */
    assert_int_equal(iolink_dll_set_pd_length(&dll, 4, 0), 0);
    len = isdu_run_read(&ctx, IOLINK_IDX_PROCESS_DATA_INPUT, 0x00, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_SERV_TEMP_UNAVAIL);
    assert_int_equal(iolink_dll_publish_pd_in(&dll, bad, 2U, false), 0);
    len = isdu_run_read(&ctx, IOLINK_IDX_PROCESS_DATA_INPUT, 0x00, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_SERV_TEMP_UNAVAIL);
}

/* ProcessDataOutput 0x0029 (B.2.24): last valid PDout is only taken from
 * accepted M-sequences after PDOUTVALID; read-only. The DLL-level capture is
 * covered in test_integration_ds_reset.c; here the snapshot API is used. */
static void test_isdu_process_data_output(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_dll_ctx_t dll;
    uint8_t resp[8];
    int len;
    (void) memset(&dll, 0, sizeof(dll));
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_isdu_init(&ctx);
    ctx.dll_ctx = &dll;

    assert_int_equal(iolink_dll_set_pd_length(&dll, 1, 0), 0);
    dll.state = IOLINK_DLL_STATE_OPERATE;
    len = isdu_run_read(&ctx, IOLINK_IDX_PROCESS_DATA_OUTPUT, 0x00, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_INDEX_NOT_AVAIL);

    /* Corrected expectation: the current OPERATE state alone does not make
       the PDout buffer valid. Before the first valid PDout: SERV_NOTAVAIL. */
    assert_int_equal(iolink_dll_set_pd_length(&dll, 1, 3), 0);
    dll.pd_out[0] = 0x01U;
    dll.pd_out[1] = 0x02U;
    dll.pd_out[2] = 0x03U;
    dll.state = IOLINK_DLL_STATE_OPERATE;
    len = isdu_run_read(&ctx, IOLINK_IDX_PROCESS_DATA_OUTPUT, 0x00, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_SERV_TEMP_UNAVAIL);

    /* A captured snapshot is served even outside OPERATE. */
    dll.pd_out_last_valid[0] = 0x01U;
    dll.pd_out_last_valid[1] = 0x02U;
    dll.pd_out_last_valid[2] = 0x03U;
    dll.pd_out_last_valid_len = 3U;
    dll.state = IOLINK_DLL_STATE_PREOPERATE;
    len = isdu_run_read(&ctx, IOLINK_IDX_PROCESS_DATA_OUTPUT, 0x00, resp, sizeof(resp));
    assert_int_equal(len, 3);
    assert_int_equal(resp[0], 0x01U);
    assert_int_equal(resp[1], 0x02U);
    assert_int_equal(resp[2], 0x03U);

    const uint8_t w = 0xEEU;
    len = isdu_run_write(&ctx, IOLINK_IDX_PROCESS_DATA_OUTPUT, 0x00, &w, 1U, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_NOT_ACCESSIBLE);
    assert_int_equal(dll.pd_out_last_valid[0], 0x01U);
    len = isdu_run_read(&ctx, IOLINK_IDX_PROCESS_DATA_OUTPUT, 0x03, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);

    /* A changed PDout length drops the snapshot. */
    assert_int_equal(iolink_dll_set_pd_length(&dll, 1, 2), 0);
    len = isdu_run_read(&ctx, IOLINK_IDX_PROCESS_DATA_OUTPUT, 0x00, resp, sizeof(resp));
    assert_isdu_error(resp, len, IOLINK_ISDU_ERROR_SERV_TEMP_UNAVAIL);
}

/* Wire level: transport one request octet per message (START, COUNT 1, 2..)
 * and collect the framed response octets the same way. */
static size_t wire_exchange(iolink_isdu_ctx_t* ctx, const uint8_t* req, size_t req_len,
                            uint8_t* resp, size_t resp_len)
{
    uint8_t fc = IOLINK_FLOWCTRL_START;
    for (size_t i = 0U; i < req_len; i++) {
        iolink_isdu_od_write(ctx, fc, &req[i], 1U);
        fc = (fc == IOLINK_FLOWCTRL_START) ? 1U : (uint8_t) ((fc + 1U) & 0x0FU);
    }
    iolink_isdu_process(ctx);

    fc = IOLINK_FLOWCTRL_START;
    for (size_t i = 0U; i < resp_len; i++) {
        iolink_isdu_od_read(ctx, fc, &resp[i], 1U);
        fc = (fc == IOLINK_FLOWCTRL_START) ? 1U : (uint8_t) ((fc + 1U) & 0x0FU);
    }
    return resp_len;
}

static void test_isdu_wire_conformance_frames(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    iolink_dll_ctx_t dll;
    uint8_t resp[8];
    (void) memset(&dll, 0, sizeof(dll));
    iolink_device_info_init(NULL);
    iolink_params_init();

    /* Write SystemCommand 0x95 (reserved): Write Response (-) 0x44, 0x80 0x35. */
    iolink_isdu_init(&ctx);
    const uint8_t sc_req[] = {0x14U, 0x02U, 0x95U, (uint8_t) (0x14U ^ 0x02U ^ 0x95U)};
    const uint8_t sc_rsp[] = {0x44U, 0x80U, 0x35U, (uint8_t) (0x44U ^ 0x80U ^ 0x35U)};
    wire_exchange(&ctx, sc_req, sizeof(sc_req), resp, sizeof(sc_rsp));
    assert_memory_equal(resp, sc_rsp, sizeof(sc_rsp));

    /* Write SystemCommand 0x81: Write Response (+) 0x52, CHKPDU 0x52. */
    iolink_isdu_init(&ctx);
    install_reset_stub(&ctx);
    const uint8_t ar_req[] = {0x14U, 0x02U, 0x81U, (uint8_t) (0x14U ^ 0x02U ^ 0x81U)};
    wire_exchange(&ctx, ar_req, sizeof(ar_req), resp, 2U);
    assert_int_equal(resp[0], 0x52U);
    assert_int_equal(resp[1], 0x52U);
    assert_true(ctx.app_reset_pending);

    /* Read 0x0028 with 2 PDin octets: Read Response (+) 0xD4, data, CHKPDU. */
    iolink_isdu_init(&ctx);
    ctx.dll_ctx = &dll;
    assert_int_equal(iolink_dll_set_pd_length(&dll, 2, 0), 0);
    const uint8_t pdin[2] = {0x12U, 0x34U};
    assert_int_equal(iolink_dll_publish_pd_in(&dll, pdin, 2U, true), 0);
    const uint8_t pd_req[] = {0x93U, 0x28U, (uint8_t) (0x93U ^ 0x28U)};
    const uint8_t pd_rsp[] = {0xD4U, 0x12U, 0x34U, (uint8_t) (0xD4U ^ 0x12U ^ 0x34U)};
    wire_exchange(&ctx, pd_req, sizeof(pd_req), resp, sizeof(pd_rsp));
    assert_memory_equal(resp, pd_rsp, sizeof(pd_rsp));

    /* Read 0x000B (reserved): Read Response (-) 0xC4, 0x80 0x11. */
    iolink_isdu_init(&ctx);
    const uint8_t id_req[] = {0x93U, 0x0BU, (uint8_t) (0x93U ^ 0x0BU)};
    const uint8_t id_rsp[] = {0xC4U, 0x80U, 0x11U, (uint8_t) (0xC4U ^ 0x80U ^ 0x11U)};
    wire_exchange(&ctx, id_req, sizeof(id_req), resp, sizeof(id_rsp));
    assert_memory_equal(resp, id_rsp, sizeof(id_rsp));
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
        cmocka_unit_test_setup_teardown(test_isdu_device_id_three_octets, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_reserved_indices_negative, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_mseq_capability_codes, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_spec_index_constants, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_system_command_read_only_write, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_device_reset, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_application_reset, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_factory_restore, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_param_upload, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_param_download, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_param_break, test_setup, test_teardown),
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
                                        test_teardown),        cmocka_unit_test_setup_teardown(test_system_cmd_codes_table_b9, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_reserved_values_rejected, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_length_and_subindex, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_param_without_ds, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_block_param_sequences, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_system_cmd_param_download_store, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_error_count, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_conditional_profile_objects, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_identification_access_rules, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_status_objects_read_only, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_process_data_input_guards, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_process_data_output, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_wire_conformance_frames, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
