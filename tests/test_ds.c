/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_ds.c
 * @brief Unit tests for Data Storage (DS)
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/data_storage.h"
#include "iolinki/params.h"
#include "iolinki/device_info.h"
#include "iolinki/protocol.h"
#include "test_helpers.h"

static void test_ds_checksum(void** state)
{
    (void) state;
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    uint16_t cs1 = iolink_ds_calc_checksum(data, sizeof(data));
    uint16_t cs2 = iolink_ds_calc_checksum(data, sizeof(data));
    assert_int_equal(cs1, cs2);

    data[0] = 0xAA;
    uint16_t cs3 = iolink_ds_calc_checksum(data, sizeof(data));
    assert_int_not_equal(cs1, cs3);
}

static void test_ds_storage_integration(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    iolink_ds_mock_reset();
    iolink_ds_init(&ds, &g_ds_storage_mock);

    uint8_t write_data[] = {0x11, 0x22, 0x33, 0x44};
    uint8_t read_buf[4] = {0};

    assert_int_equal(ds.storage->write(0, write_data, 4), 0);
    assert_int_equal(ds.storage->read(0, read_buf, 4), 0);
    assert_memory_equal(read_buf, write_data, 4);
}

static void test_ds_state_transitions(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);

    /* 1. Trigger Download (Master has newer data) */
    iolink_ds_check(&ds, 0xABCD);
    iolink_ds_process(&ds); /* Req -> Downloading */
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOADING);
    iolink_ds_process(&ds); /* Downloading -> Idle */
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);

    /* 2. Trigger Upload (Master has 0x0000 - empty) */
    iolink_ds_check(&ds, 0x0000);
    iolink_ds_process(&ds); /* Req -> Uploading */
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOADING);
    iolink_ds_process(&ds); /* Uploading -> Idle */
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);
}

static void test_ds_commands_locked(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);

    /* 1. Try Download Start (Write) while locked */
    int ret = iolink_ds_handle_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_START, IOLINK_LOCK_DS);
    assert_int_equal(ret, -2); /* Access Denied */
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);

    /* 2. Try Upload Start (Read) while locked - Should usually pass?
       Spec: Access Locks usually apply to Parameter Write (Index 2).
       Data Storage Upload is reading from device, so maybe allowed?
       The code we implemented only checks lock for Download (Write).
    */
    ret = iolink_ds_handle_command(&ds, IOLINK_CMD_PARAM_UPLOAD_START, IOLINK_LOCK_DS);
    assert_int_equal(ret, 0); /* Success */
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOAD_REQ);
}

static void test_ds_commands_unlocked(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);

    /* 1. Download Start (Write) unlocked */
    int ret = iolink_ds_handle_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_START, 0);
    assert_int_equal(ret, 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOAD_REQ);

    /* 2. End Download */
    ds.state = IOLINK_DS_STATE_DOWNLOADING;
    ret = iolink_ds_handle_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_END, 0);
    assert_int_equal(ret, 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);
}

static void test_ds_param_round_trip(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);

    /* Configure known parameter values. */
    const char* app = "APP-TAG-1";
    const char* fun = "FLOW";
    const char* loc = "TANK-7";
    assert_int_equal(
        iolink_params_set(IOLINK_IDX_APPLICATION_TAG, 0U, (const uint8_t*) app, strlen(app), true),
        0);
    assert_int_equal(
        iolink_params_set(IOLINK_IDX_FUNCTION_TAG, 0U, (const uint8_t*) fun, strlen(fun), true), 0);
    assert_int_equal(
        iolink_params_set(IOLINK_IDX_LOCATION_TAG, 0U, (const uint8_t*) loc, strlen(loc), true), 0);

    /* Upload: serialize current parameters into the DS image (backup). */
    int n = iolink_ds_build_image(&ds);
    assert_true(n > 0);
    size_t backup_len = 0U;
    const uint8_t* img = iolink_ds_get_image(&ds, &backup_len);
    assert_non_null(img);
    assert_int_equal((int) backup_len, n);
    uint8_t backup[IOLINK_DS_IMAGE_MAX];
    memcpy(backup, img, backup_len);
    uint16_t backup_cs = ds.current_checksum;
    assert_int_not_equal(backup_cs, 0);

    /* Wipe the device parameters. */
    iolink_params_factory_reset();
    uint8_t buf[40];
    assert_int_equal(iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf)), 0);
    assert_int_equal(iolink_params_get(IOLINK_IDX_FUNCTION_TAG, 0U, buf, sizeof(buf)), 0);

    /* Download: apply the backup image (restore). */
    assert_int_equal(iolink_ds_apply_image(&ds, backup, backup_len), 0);
    assert_int_equal(ds.current_checksum, backup_cs);

    /* Parameters are restored byte-for-byte. */
    int len = iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf));
    assert_int_equal(len, (int) strlen(app));
    assert_memory_equal(buf, app, strlen(app));
    len = iolink_params_get(IOLINK_IDX_FUNCTION_TAG, 0U, buf, sizeof(buf));
    assert_int_equal(len, (int) strlen(fun));
    assert_memory_equal(buf, fun, strlen(fun));
    len = iolink_params_get(IOLINK_IDX_LOCATION_TAG, 0U, buf, sizeof(buf));
    assert_int_equal(len, (int) strlen(loc));
    assert_memory_equal(buf, loc, strlen(loc));
}

static void test_ds_apply_rejects_truncated(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    iolink_ds_init(&ds, NULL);
    /* Header claims a 5-byte value but only 2 data bytes follow -> rejected. */
    uint8_t bad[] = {0x00, 0x18, 0x00, 0x05, 0xAA, 0xBB};
    assert_int_equal(iolink_ds_apply_image(&ds, bad, sizeof(bad)), -1);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_ds_checksum),
        cmocka_unit_test(test_ds_storage_integration),
        cmocka_unit_test(test_ds_state_transitions),
        cmocka_unit_test(test_ds_commands_locked),
        cmocka_unit_test(test_ds_commands_unlocked),
        cmocka_unit_test(test_ds_param_round_trip),
        cmocka_unit_test(test_ds_apply_rejects_truncated),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
