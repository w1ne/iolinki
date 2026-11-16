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
#include "iolinki/platform.h"
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

    /* Corrected expectation: iolink_ds_process() only moves REQ to the
       active state. The session stays open across any number of device
       cycles and is closed only by End/Store/Break (10.3.5, Table 100). */

    /* Idle stays idle. */
    iolink_ds_process(&ds);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);

    /* 1. Trigger Download (Master has newer data) */
    iolink_ds_check(&ds, 0xABCD);
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOAD_REQ);
    iolink_ds_process(&ds); /* Req -> Downloading */
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOADING);
    for (int i = 0; i < 100; i++) {
        iolink_ds_process(&ds); /* No accidental commit or cancel */
    }
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOADING);
    assert_int_equal(iolink_ds_handle_command(&ds, IOLINK_CMD_PARAM_DOWNLOAD_END, 0U), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);

    /* 2. Trigger Upload (Master has 0x0000 - empty) */
    iolink_ds_check(&ds, 0x0000);
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOAD_REQ);
    iolink_ds_process(&ds); /* Req -> Uploading */
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOADING);
    for (int i = 0; i < 100; i++) {
        iolink_ds_process(&ds);
    }
    assert_int_equal(ds.state, IOLINK_DS_STATE_UPLOADING);
    assert_int_equal(iolink_ds_handle_command(&ds, IOLINK_CMD_PARAM_UPLOAD_END, 0U), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);

    /* 3. Break closes an active session without changes. */
    iolink_ds_check(&ds, 0xABCD);
    iolink_ds_process(&ds);
    assert_int_equal(iolink_ds_handle_command(&ds, IOLINK_CMD_PARAM_BREAK, 0U), 0);
    assert_int_equal(ds.state, IOLINK_DS_STATE_IDLE);
    iolink_ds_process(&ds);
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

static void test_ds_bound_params_restore_device_context_only(void** state)
{
    (void) state;
    iolink_device_info_ctx_t info_a;
    iolink_device_info_ctx_t info_b;
    iolink_params_ctx_t params_a;
    iolink_params_ctx_t params_b;
    iolink_ds_ctx_t ds_a;
    iolink_ds_ctx_t ds_b;
    static const uint8_t tag_a[] = "CtxA";
    static const uint8_t tag_b[] = "CtxB";
    uint8_t backup[IOLINK_DS_IMAGE_MAX];
    uint8_t out[16] = {0U};

    iolink_device_info_ctx_init(&info_a, NULL);
    iolink_device_info_ctx_init(&info_b, NULL);
    iolink_params_ctx_init(&params_a, &info_a);
    iolink_params_ctx_init(&params_b, &info_b);
    iolink_ds_init(&ds_a, NULL);
    iolink_ds_init(&ds_b, NULL);
    iolink_ds_bind_params(&ds_a, &params_a);
    iolink_ds_bind_params(&ds_b, &params_b);

    assert_int_equal(iolink_params_ctx_set(&params_a, IOLINK_IDX_APPLICATION_TAG, 0U, tag_a,
                                           sizeof(tag_a) - 1U, true),
                     0);
    assert_int_equal(iolink_params_ctx_set(&params_b, IOLINK_IDX_APPLICATION_TAG, 0U, tag_b,
                                           sizeof(tag_b) - 1U, true),
                     0);

    assert_true(iolink_ds_build_image(&ds_a) > 0);
    assert_true(ds_a.image_len <= sizeof(backup));
    memcpy(backup, ds_a.image, ds_a.image_len);

    iolink_params_ctx_factory_reset(&params_a);
    assert_int_equal(iolink_ds_apply_image(&ds_a, backup, ds_a.image_len), 0);

    assert_int_equal(
        iolink_params_ctx_get(&params_a, IOLINK_IDX_APPLICATION_TAG, 0U, out, sizeof(out)),
        (int) (sizeof(tag_a) - 1U));
    assert_memory_equal(out, tag_a, sizeof(tag_a) - 1U);

    memset(out, 0, sizeof(out));
    assert_int_equal(
        iolink_params_ctx_get(&params_b, IOLINK_IDX_APPLICATION_TAG, 0U, out, sizeof(out)),
        (int) (sizeof(tag_b) - 1U));
    assert_memory_equal(out, tag_b, sizeof(tag_b) - 1U);
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

static void test_ds_download_rejects_unknown_index(void** state)
{
    (void) state;
    iolink_ds_ctx_t ds;
    iolink_device_info_init(NULL);
    iolink_params_init();
    iolink_ds_init(&ds, NULL);

    const uint8_t keep[] = "KEEP";
    assert_int_equal(iolink_params_set(IOLINK_IDX_APPLICATION_TAG, 0U, keep, 4U, true), 0);
    assert_true(iolink_ds_build_image(&ds) > 0);
    uint16_t cs_before = ds.current_checksum;

    /* A known parameter (ApplicationTag 0x18 = "X") plus an UNKNOWN index
       (0x9999). The whole image is rejected; the valid record is not applied. */
    uint8_t img[16];
    size_t p = 0U;
    img[p++] = 0x00;
    img[p++] = 0x18;
    img[p++] = 0x00;
    img[p++] = 0x01;
    img[p++] = 'X';
    img[p++] = 0x99;
    img[p++] = 0x99;
    img[p++] = 0x00;
    img[p++] = 0x01;
    img[p++] = 0xAB;
    uint16_t downloaded_cs = iolink_ds_calc_checksum(img, p);

    assert_int_equal(iolink_ds_apply_image(&ds, img, p), IOLINK_DS_ERR_INVALID);

    uint8_t buf[8];
    assert_int_equal(iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0U, buf, sizeof(buf)), 4);
    assert_memory_equal(buf, keep, 4U);
    assert_true(iolink_ds_verify(&ds));
    assert_int_equal(ds.current_checksum, cs_before);

    /* The stale Master checksum still differs, so the next check re-requests download. */
    iolink_ds_check(&ds, downloaded_cs);
    assert_int_equal(ds.state, IOLINK_DS_STATE_DOWNLOAD_REQ);
}

/* ---- Helpers for device-local DS tests ---------------------------------- */

/* NVM offsets inside the 4096-octet test_helpers NVM mock. Offsets near the
   end make every record write fail (backend range error). */
#define NVM_OFF_A 1024U
#define NVM_OFF_B 1536U
#define NVM_OFF_C 2048U
#define NVM_OFF_FAIL 4090U

typedef struct
{
    iolink_device_info_ctx_t info;
    iolink_params_ctx_t params;
    iolink_ds_ctx_t ds;
} ds_dev_t;

static void nvm_clear(uint32_t offset)
{
    uint8_t zero[IOLINK_PARAMS_NVM_RECORD_SIZE] = {0U};
    assert_int_equal(iolink_nvm_write(offset, zero, sizeof(zero)), 0);
}

static void dev_init(ds_dev_t* dev, const iolink_device_info_t* cfg, uint32_t offset,
                     const iolink_ds_storage_api_t* storage)
{
    iolink_device_info_ctx_init(&dev->info, cfg);
    (void) iolink_params_ctx_init_at(&dev->params, &dev->info, offset);
    iolink_ds_init(&dev->ds, storage);
    iolink_ds_bind_params(&dev->ds, &dev->params);
}

static void dev_set_tags(ds_dev_t* dev, const char* app, const char* fun, const char* loc,
                         bool persist)
{
    assert_int_equal(iolink_params_ctx_set(&dev->params, IOLINK_IDX_APPLICATION_TAG, 0U,
                                           (const uint8_t*) app, strlen(app), persist),
                     0);
    assert_int_equal(iolink_params_ctx_set(&dev->params, IOLINK_IDX_FUNCTION_TAG, 0U,
                                           (const uint8_t*) fun, strlen(fun), persist),
                     0);
    assert_int_equal(iolink_params_ctx_set(&dev->params, IOLINK_IDX_LOCATION_TAG, 0U,
                                           (const uint8_t*) loc, strlen(loc), persist),
                     0);
}

static void assert_tag(const iolink_params_ctx_t* params, uint16_t index, const char* expected)
{
    uint8_t buf[40];
    int n = iolink_params_ctx_get(params, index, 0U, buf, sizeof(buf));
    assert_int_equal(n, (int) strlen(expected));
    assert_memory_equal(buf, expected, strlen(expected));
}

static void assert_tags(const ds_dev_t* dev, const char* app, const char* fun, const char* loc)
{
    assert_tag(&dev->params, IOLINK_IDX_APPLICATION_TAG, app);
    assert_tag(&dev->params, IOLINK_IDX_FUNCTION_TAG, fun);
    assert_tag(&dev->params, IOLINK_IDX_LOCATION_TAG, loc);
}

/* Append one record; returns the new position. */
static size_t put_rec(uint8_t* img, size_t pos, uint16_t index, uint8_t sub, const void* val,
                      uint8_t len)
{
    img[pos] = (uint8_t) (index >> 8);
    img[pos + 1U] = (uint8_t) (index & 0xFFU);
    img[pos + 2U] = sub;
    img[pos + 3U] = len;
    if (len > 0U) {
        memcpy(&img[pos + 4U], val, len);
    }
    return pos + 4U + len;
}

/* ---- Whole-image validation before any write ---------------------------- */

static void test_ds_rejects_invalid_images_before_write(void** state)
{
    (void) state;
    ds_dev_t dev;
    nvm_clear(NVM_OFF_A);
    dev_init(&dev, NULL, NVM_OFF_A, NULL);
    dev_set_tags(&dev, "APP-OLD", "FUN-OLD", "LOC-OLD", true);
    assert_true(iolink_ds_build_image(&dev.ds) > 0);

    uint8_t snap[IOLINK_DS_IMAGE_MAX];
    size_t snap_len = dev.ds.image_len;
    uint16_t snap_cs = dev.ds.current_checksum;
    memcpy(snap, dev.ds.image, snap_len);

    uint8_t img[IOLINK_DS_IMAGE_MAX + 8U];
    uint8_t long_tag[IOLINK_PARAMS_TAG_MAX + 1U];
    memset(long_tag, 'L', sizeof(long_tag));
    size_t n;

    struct
    {
        const char* what;
        size_t len;
    } cases[10];
    uint8_t bufs[10][IOLINK_DS_IMAGE_MAX + 8U];
    size_t c = 0U;

    /* 1. Valid first record, then a truncated header. */
    n = put_rec(img, 0U, IOLINK_IDX_APPLICATION_TAG, 0U, "NEW", 3U);
    img[n++] = 0x00;
    img[n++] = 0x19;
    memcpy(bufs[c], img, n);
    cases[c].what = "truncated header";
    cases[c++].len = n;

    /* 2. Valid first record, then a truncated value. */
    n = put_rec(img, 0U, IOLINK_IDX_APPLICATION_TAG, 0U, "NEW", 3U);
    n = put_rec(img, n, IOLINK_IDX_FUNCTION_TAG, 0U, "FUNNEW", 6U);
    n -= 2U;
    memcpy(bufs[c], img, n);
    cases[c].what = "truncated data";
    cases[c++].len = n;

    /* 3. Duplicate record. */
    n = put_rec(img, 0U, IOLINK_IDX_APPLICATION_TAG, 0U, "NEW", 3U);
    n = put_rec(img, n, IOLINK_IDX_APPLICATION_TAG, 0U, "NEW2", 4U);
    memcpy(bufs[c], img, n);
    cases[c].what = "duplicate";
    cases[c++].len = n;

    /* 4. Unknown index. */
    n = put_rec(img, 0U, IOLINK_IDX_APPLICATION_TAG, 0U, "NEW", 3U);
    n = put_rec(img, n, IOLINK_IDX_VENDOR_NAME, 0U, "X", 1U);
    memcpy(bufs[c], img, n);
    cases[c].what = "unknown index";
    cases[c++].len = n;

    /* 5. Known index, unsupported subindex. */
    n = put_rec(img, 0U, IOLINK_IDX_APPLICATION_TAG, 0U, "NEW", 3U);
    n = put_rec(img, n, IOLINK_IDX_LOCATION_TAG, 1U, "X", 1U);
    memcpy(bufs[c], img, n);
    cases[c].what = "unknown subindex";
    cases[c++].len = n;

    /* 6. Length overrun: 33-octet tag. */
    n = put_rec(img, 0U, IOLINK_IDX_APPLICATION_TAG, 0U, "NEW", 3U);
    n = put_rec(img, n, IOLINK_IDX_FUNCTION_TAG, 0U, long_tag, (uint8_t) sizeof(long_tag));
    memcpy(bufs[c], img, n);
    cases[c].what = "length overrun";
    cases[c++].len = n;

    /* 7. Invalid value: embedded NUL in a StringT tag. */
    n = put_rec(img, 0U, IOLINK_IDX_APPLICATION_TAG, 0U, "NEW", 3U);
    n = put_rec(img, n, IOLINK_IDX_LOCATION_TAG, 0U, "A\0B", 3U);
    memcpy(bufs[c], img, n);
    cases[c].what = "embedded NUL";
    cases[c++].len = n;

    /* 8. Empty image. */
    cases[c].what = "empty";
    cases[c++].len = 0U;

    /* 9. Capacity overflow: image longer than IOLINK_DS_IMAGE_MAX. */
    memset(bufs[c], 0, sizeof(bufs[c]));
    (void) put_rec(bufs[c], 0U, IOLINK_IDX_APPLICATION_TAG, 0U, "NEW", 3U);
    cases[c].what = "capacity overflow";
    cases[c++].len = IOLINK_DS_IMAGE_MAX + 1U;

    for (size_t i = 0U; i < c; ++i) {
        assert_int_equal(iolink_ds_validate_image(bufs[i], cases[i].len), IOLINK_DS_ERR_INVALID);
        assert_int_equal(iolink_ds_apply_image(&dev.ds, bufs[i], cases[i].len),
                         IOLINK_DS_ERR_INVALID);
        /* No live parameter changed, the last valid image is intact. */
        assert_tags(&dev, "APP-OLD", "FUN-OLD", "LOC-OLD");
        assert_int_equal(dev.ds.image_len, snap_len);
        assert_memory_equal(dev.ds.image, snap, snap_len);
        assert_int_equal(dev.ds.current_checksum, snap_cs);
        assert_true(iolink_ds_verify(&dev.ds));
    }

    /* NVM still holds the old values: a re-init restores them. */
    ds_dev_t again;
    dev_init(&again, NULL, NVM_OFF_A, NULL);
    assert_tags(&again, "APP-OLD", "FUN-OLD", "LOC-OLD");

    /* NULL data is rejected without a crash. */
    assert_int_equal(iolink_ds_apply_image(&dev.ds, NULL, 4U), IOLINK_DS_ERR_INVALID);
}

/* ---- ParamDownloadStart / End / Store / Break lifecycle ----------------- */

static void test_ds_download_session_commit_and_checksum(void** state)
{
    (void) state;
    ds_dev_t dev;
    uint8_t img[IOLINK_DS_IMAGE_MAX];
    size_t n;

    nvm_clear(NVM_OFF_A);
    dev_init(&dev, NULL, NVM_OFF_A, NULL);
    dev_set_tags(&dev, "A0", "F0", "L0", true);

    /* Staging outside a session is refused. */
    n = put_rec(img, 0U, IOLINK_IDX_APPLICATION_TAG, 0U, "A1", 2U);
    assert_int_equal(iolink_ds_stage_image(&dev.ds, img, n), IOLINK_DS_ERR_INVALID);

    assert_int_equal(iolink_ds_handle_command(&dev.ds, IOLINK_CMD_PARAM_DOWNLOAD_START, 0U), 0);
    n = put_rec(img, 0U, IOLINK_IDX_APPLICATION_TAG, 0U, "A1", 2U);
    n = put_rec(img, n, IOLINK_IDX_FUNCTION_TAG, 0U, "F1", 2U);
    n = put_rec(img, n, IOLINK_IDX_LOCATION_TAG, 0U, "L1", 2U);
    assert_int_equal(iolink_ds_stage_image(&dev.ds, img, n), 0);
    assert_int_equal(dev.ds.state, IOLINK_DS_STATE_DOWNLOADING);

    /* Staged data is not live and is not exposed by the upload image. */
    assert_tags(&dev, "A0", "F0", "L0");
    size_t up_len = 0U;
    const uint8_t* up = iolink_ds_get_image(&dev.ds, &up_len);
    assert_non_null(up);
    assert_true(up_len != n || memcmp(up, img, n) != 0);

    assert_int_equal(iolink_ds_handle_command(&dev.ds, IOLINK_CMD_PARAM_DOWNLOAD_END, 0U), 0);
    assert_int_equal(dev.ds.state, IOLINK_DS_STATE_IDLE);
    assert_false(dev.ds.staged_valid);
    assert_tags(&dev, "A1", "F1", "L1");

    /* Image and checksum are rebuilt from actual parameters. */
    assert_int_equal(dev.ds.image_len, n);
    assert_memory_equal(dev.ds.image, img, n);
    assert_int_equal(dev.ds.current_checksum, iolink_ds_calc_checksum(img, n));
    assert_true(iolink_ds_verify(&dev.ds));

    /* Matching Master checksum: no further DS action. */
    iolink_ds_check(&dev.ds, dev.ds.current_checksum);
    assert_int_equal(dev.ds.state, IOLINK_DS_STATE_IDLE);

    /* Values reached NVM. */
    ds_dev_t again;
    dev_init(&again, NULL, NVM_OFF_A, NULL);
    assert_tags(&again, "A1", "F1", "L1");
}

static void test_ds_rejected_stage_discards_previous(void** state)
{
    (void) state;
    ds_dev_t dev;
    uint8_t good[IOLINK_DS_IMAGE_MAX];
    uint8_t bad[IOLINK_DS_IMAGE_MAX];
    size_t ng;
    size_t nb;

    nvm_clear(NVM_OFF_A);
    dev_init(&dev, NULL, NVM_OFF_A, NULL);
    dev_set_tags(&dev, "A0", "F0", "L0", true);
    assert_true(iolink_ds_build_image(&dev.ds) > 0);
    uint16_t cs = dev.ds.current_checksum;

    assert_int_equal(iolink_ds_handle_command(&dev.ds, IOLINK_CMD_PARAM_DOWNLOAD_START, 0U), 0);
    ng = put_rec(good, 0U, IOLINK_IDX_APPLICATION_TAG, 0U, "A1", 2U);
    assert_int_equal(iolink_ds_stage_image(&dev.ds, good, ng), 0);

    nb = put_rec(bad, 0U, IOLINK_IDX_APPLICATION_TAG, 0U, "A2", 2U);
    nb = put_rec(bad, nb, IOLINK_IDX_APPLICATION_TAG, 0U, "A3", 2U);
    assert_int_equal(iolink_ds_stage_image(&dev.ds, bad, nb), IOLINK_DS_ERR_INVALID);
    assert_false(dev.ds.staged_valid);

    /* Corrected expectation (Table 98 rules 4 and 7): a rejected write makes
       the downloaded set invalid. End answers negatively, the previous set
       stays active, the checksum is unchanged and the session is closed. */
    assert_int_equal(iolink_ds_handle_command(&dev.ds, IOLINK_CMD_PARAM_DOWNLOAD_END, 0U),
                     IOLINK_DS_CMD_COMMIT_FAILED);
    assert_int_equal(dev.ds.last_result, IOLINK_DS_ERR_INVALID);
    assert_int_equal(dev.ds.state, IOLINK_DS_STATE_IDLE);
    assert_tags(&dev, "A0", "F0", "L0");
    assert_int_equal(dev.ds.current_checksum, cs);

    /* A new session starts clean: a valid image then commits. */
    assert_int_equal(iolink_ds_handle_command(&dev.ds, IOLINK_CMD_PARAM_DOWNLOAD_START, 0U), 0);
    assert_int_equal(iolink_ds_stage_image(&dev.ds, good, ng), 0);
    assert_int_equal(iolink_ds_handle_command(&dev.ds, IOLINK_CMD_PARAM_DOWNLOAD_END, 0U), 0);
    assert_tags(&dev, "A1", "F0", "L0");
}

static void test_ds_break_discards_staging(void** state)
{
    (void) state;
    ds_dev_t dev;
    uint8_t img[IOLINK_DS_IMAGE_MAX];
    size_t n;

    nvm_clear(NVM_OFF_A);
    dev_init(&dev, NULL, NVM_OFF_A, NULL);
    dev_set_tags(&dev, "A0", "F0", "L0", true);

    assert_int_equal(iolink_ds_handle_command(&dev.ds, IOLINK_CMD_PARAM_DOWNLOAD_START, 0U), 0);
    n = put_rec(img, 0U, IOLINK_IDX_APPLICATION_TAG, 0U, "A1", 2U);
    assert_int_equal(iolink_ds_stage_image(&dev.ds, img, n), 0);
    assert_int_equal(iolink_ds_handle_command(&dev.ds, IOLINK_CMD_PARAM_BREAK, 0U), 0);
    assert_int_equal(dev.ds.state, IOLINK_DS_STATE_IDLE);
    assert_false(dev.ds.staged_valid);

    /* A late End outside the session does not apply the discarded data. */
    assert_int_equal(iolink_ds_handle_command(&dev.ds, IOLINK_CMD_PARAM_DOWNLOAD_END, 0U), 0);
    assert_tags(&dev, "A0", "F0", "L0");

    /* DS lock also blocks ParamDownloadStore. */
    assert_int_equal(
        iolink_ds_handle_command(&dev.ds, IOLINK_CMD_PARAM_DOWNLOAD_STORE, IOLINK_LOCK_DS),
        IOLINK_DS_CMD_DENIED);
    assert_int_equal(iolink_ds_handle_command(&dev.ds, 0x42U, 0U), IOLINK_DS_CMD_UNKNOWN);
}

/* ---- NVM / storage failure ---------------------------------------------- */

static void test_ds_nvm_failure_keeps_snapshot(void** state)
{
    (void) state;
    ds_dev_t dev;
    uint8_t img[IOLINK_DS_IMAGE_MAX];
    size_t n;

    /* Record at the end of the mock NVM: every write fails. */
    dev_init(&dev, NULL, NVM_OFF_FAIL, NULL);
    dev_set_tags(&dev, "A0", "F0", "L0", false);
    assert_true(iolink_ds_build_image(&dev.ds) > 0);
    uint8_t snap[IOLINK_DS_IMAGE_MAX];
    size_t snap_len = dev.ds.image_len;
    uint16_t snap_cs = dev.ds.current_checksum;
    memcpy(snap, dev.ds.image, snap_len);

    n = put_rec(img, 0U, IOLINK_IDX_APPLICATION_TAG, 0U, "A1", 2U);
    n = put_rec(img, n, IOLINK_IDX_FUNCTION_TAG, 0U, "F1", 2U);

    /* Direct apply reports the storage failure. */
    assert_int_equal(iolink_ds_apply_image(&dev.ds, img, n), IOLINK_DS_ERR_STORAGE);
    assert_tags(&dev, "A0", "F0", "L0");
    assert_memory_equal(dev.ds.image, snap, snap_len);
    assert_int_equal(dev.ds.current_checksum, snap_cs);

    /* Session path: End reports commit failure and returns to IDLE. */
    assert_int_equal(iolink_ds_handle_command(&dev.ds, IOLINK_CMD_PARAM_DOWNLOAD_START, 0U), 0);
    assert_int_equal(iolink_ds_stage_image(&dev.ds, img, n), 0);
    assert_int_equal(iolink_ds_handle_command(&dev.ds, IOLINK_CMD_PARAM_DOWNLOAD_END, 0U),
                     IOLINK_DS_CMD_COMMIT_FAILED);
    assert_int_equal(dev.ds.state, IOLINK_DS_STATE_IDLE);
    assert_int_equal(dev.ds.last_result, IOLINK_DS_ERR_STORAGE);
    assert_false(dev.ds.staged_valid);
    assert_tags(&dev, "A0", "F0", "L0");
    assert_int_equal(dev.ds.image_len, snap_len);
    assert_memory_equal(dev.ds.image, snap, snap_len);
    assert_int_equal(dev.ds.current_checksum, snap_cs);

    /* Store path: same result, no upload request raised. */
    assert_int_equal(iolink_ds_handle_command(&dev.ds, IOLINK_CMD_PARAM_DOWNLOAD_START, 0U), 0);
    assert_int_equal(iolink_ds_stage_image(&dev.ds, img, n), 0);
    assert_int_equal(iolink_ds_handle_command(&dev.ds, IOLINK_CMD_PARAM_DOWNLOAD_STORE, 0U),
                     IOLINK_DS_CMD_COMMIT_FAILED);
    assert_false(dev.ds.upload_requested);
    assert_tags(&dev, "A0", "F0", "L0");
}

static int g_ds_store_write_ret;
static int ds_fail_read(uint32_t addr, uint8_t* buf, size_t len)
{
    return g_ds_storage_mock.read(addr, buf, len);
}
static int ds_fail_write(uint32_t addr, const uint8_t* buf, size_t len)
{
    if (g_ds_store_write_ret != 0) {
        return g_ds_store_write_ret;
    }
    return g_ds_storage_mock.write(addr, buf, len);
}
static const iolink_ds_storage_api_t k_ds_fail_storage = {
    .read = ds_fail_read, .write = ds_fail_write, .erase = NULL};

static void test_ds_download_store_and_backend(void** state)
{
    (void) state;
    ds_dev_t dev;
    uint8_t img[IOLINK_DS_IMAGE_MAX];
    uint8_t loaded[IOLINK_DS_IMAGE_MAX];
    size_t loaded_len = 0U;
    size_t n;

    iolink_ds_mock_reset();
    g_ds_store_write_ret = 0;
    nvm_clear(NVM_OFF_A);
    dev_init(&dev, NULL, NVM_OFF_A, &k_ds_fail_storage);
    dev_set_tags(&dev, "A0", "F0", "L0", true);

    /* No stored record yet: load reports a corrupt/empty record. */
    assert_int_equal(iolink_ds_load(&dev.ds, loaded, sizeof(loaded), &loaded_len),
                     IOLINK_DS_ERR_INVALID);

    assert_int_equal(iolink_ds_handle_command(&dev.ds, IOLINK_CMD_PARAM_DOWNLOAD_START, 0U), 0);
    n = put_rec(img, 0U, IOLINK_IDX_LOCATION_TAG, 0U, "L1", 2U);
    assert_int_equal(iolink_ds_stage_image(&dev.ds, img, n), 0);
    assert_int_equal(iolink_ds_handle_command(&dev.ds, IOLINK_CMD_PARAM_DOWNLOAD_STORE, 0U), 0);
    assert_true(dev.ds.upload_requested);
    assert_tags(&dev, "A0", "F0", "L1");

    /* Backend record: header identifies layout; image matches the actual parameters. */
    const uint8_t* raw = iolink_ds_mock_get_buf();
    assert_int_equal(raw[0], 'D');
    assert_int_equal(raw[1], 'S');
    assert_int_equal(raw[2], IOLINK_DS_IMAGE_LAYOUT_VERSION);
    assert_int_equal(iolink_ds_load(&dev.ds, loaded, sizeof(loaded), &loaded_len), 0);
    assert_int_equal(loaded_len, dev.ds.image_len);
    assert_memory_equal(loaded, dev.ds.image, loaded_len);

    /* Corrupt one stored image octet: load detects it. */
    iolink_ds_mock_get_buf()[IOLINK_DS_STORE_HEADER_SIZE + 4U] ^= 0x5AU;
    assert_int_equal(iolink_ds_load(&dev.ds, loaded, sizeof(loaded), &loaded_len),
                     IOLINK_DS_ERR_INVALID);

    /* Upload end clears the upload request. */
    assert_int_equal(iolink_ds_handle_command(&dev.ds, IOLINK_CMD_PARAM_UPLOAD_START, 0U), 0);
    assert_int_equal(iolink_ds_handle_command(&dev.ds, IOLINK_CMD_PARAM_UPLOAD_END, 0U), 0);
    assert_false(dev.ds.upload_requested);

    /* Backend write failure is reported. Parameters are already committed:
       the parameter NVM write and the DS backend store are separate steps. */
    g_ds_store_write_ret = -1;
    assert_int_equal(iolink_ds_handle_command(&dev.ds, IOLINK_CMD_PARAM_DOWNLOAD_START, 0U), 0);
    n = put_rec(img, 0U, IOLINK_IDX_LOCATION_TAG, 0U, "L2", 2U);
    assert_int_equal(iolink_ds_stage_image(&dev.ds, img, n), 0);
    assert_int_equal(iolink_ds_handle_command(&dev.ds, IOLINK_CMD_PARAM_DOWNLOAD_STORE, 0U),
                     IOLINK_DS_CMD_COMMIT_FAILED);
    assert_int_equal(dev.ds.last_result, IOLINK_DS_ERR_STORAGE);
    assert_tags(&dev, "A0", "F0", "L2");
    assert_true(iolink_ds_verify(&dev.ds));
    g_ds_store_write_ret = 0;

    /* No storage hooks bound: save/load report the missing backend. */
    iolink_ds_ctx_t bare;
    iolink_ds_init(&bare, NULL);
    iolink_ds_bind_params(&bare, &dev.params);
    assert_int_equal(iolink_ds_save(&bare), IOLINK_DS_ERR_STORAGE);
    assert_int_equal(iolink_ds_load(&bare, loaded, sizeof(loaded), &loaded_len),
                     IOLINK_DS_ERR_STORAGE);
}

/* ---- Multi-device isolation, reset, access locks ------------------------ */

static void test_ds_multi_device_isolation(void** state)
{
    (void) state;
    ds_dev_t a;
    ds_dev_t b;
    uint8_t img[IOLINK_DS_IMAGE_MAX];
    size_t n;

    nvm_clear(NVM_OFF_A);
    nvm_clear(NVM_OFF_B);
    dev_init(&a, NULL, NVM_OFF_A, NULL);
    dev_init(&b, NULL, NVM_OFF_B, NULL);
    dev_set_tags(&a, "DEV-A", "FA", "LA", true);
    dev_set_tags(&b, "DEV-B", "FB", "LB", true);
    assert_int_equal(iolink_params_ctx_set_access_locks(&a.params, IOLINK_LOCK_DS, true), 0);
    assert_int_equal(iolink_params_ctx_set_access_locks(&b.params, IOLINK_LOCK_PARAM, true), 0);

    assert_true(iolink_ds_build_image(&b.ds) > 0);
    uint16_t b_cs = b.ds.current_checksum;

    n = put_rec(img, 0U, IOLINK_IDX_APPLICATION_TAG, 0U, "NEW-A", 5U);
    assert_int_equal(iolink_ds_apply_image(&a.ds, img, n), 0);
    assert_tags(&a, "NEW-A", "FA", "LA");
    assert_tags(&b, "DEV-B", "FB", "LB");
    assert_int_equal(b.ds.current_checksum, b_cs);

    /* Each device restores only its own record, including access locks. */
    ds_dev_t a2;
    ds_dev_t b2;
    dev_init(&a2, NULL, NVM_OFF_A, NULL);
    dev_init(&b2, NULL, NVM_OFF_B, NULL);
    assert_tags(&a2, "NEW-A", "FA", "LA");
    assert_tags(&b2, "DEV-B", "FB", "LB");
    assert_int_equal(iolink_params_ctx_get_access_locks(&a2.params), IOLINK_LOCK_DS);
    assert_int_equal(iolink_params_ctx_get_access_locks(&b2.params), IOLINK_LOCK_PARAM);
}

static void test_ds_factory_reset_and_access_locks(void** state)
{
    (void) state;
    static const iolink_device_info_t cfg = {.application_tag = "CFG",
                                             .access_locks = IOLINK_LOCK_LOCAL_UI};
    ds_dev_t dev;
    uint8_t buf[4];

    nvm_clear(NVM_OFF_C);
    dev_init(&dev, &cfg, NVM_OFF_C, NULL);
    assert_int_equal(iolink_params_ctx_get_access_locks(&dev.params), IOLINK_LOCK_LOCAL_UI);

    /* Reserved bits are rejected; RAM stays unchanged. */
    assert_int_equal(iolink_params_ctx_set_access_locks(&dev.params, 0x0010U, true), -1);
    assert_int_equal(iolink_params_ctx_get_access_locks(&dev.params), IOLINK_LOCK_LOCAL_UI);

    /* Index 0x000C through the generic parameter API. */
    const uint8_t locks_be[2] = {0x00U, (uint8_t) (IOLINK_LOCK_DS | IOLINK_LOCK_PARAM)};
    assert_int_equal(iolink_params_ctx_set(&dev.params, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U,
                                           locks_be, 2U, true),
                     0);
    assert_int_equal(iolink_params_ctx_get(&dev.params, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U, buf,
                                           sizeof(buf)),
                     2);
    assert_memory_equal(buf, locks_be, 2U);
    assert_int_equal(iolink_params_ctx_set(&dev.params, IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U,
                                           locks_be, 1U, true),
                     -1);
    dev_set_tags(&dev, "A", "F", "L", true);

    ds_dev_t again;
    dev_init(&again, &cfg, NVM_OFF_C, NULL);
    assert_int_equal(iolink_params_ctx_get_access_locks(&again.params),
                     IOLINK_LOCK_DS | IOLINK_LOCK_PARAM);
    assert_tags(&again, "A", "F", "L");

    /* Factory reset: tags empty, locks back to the configured default, persisted. */
    assert_int_equal(iolink_params_ctx_factory_reset_checked(&again.params), 0);
    assert_tags(&again, "", "", "");
    assert_int_equal(iolink_params_ctx_get_access_locks(&again.params), IOLINK_LOCK_LOCAL_UI);
    ds_dev_t after;
    dev_init(&after, &cfg, NVM_OFF_C, NULL);
    assert_tags(&after, "", "", "");
    assert_int_equal(iolink_params_ctx_get_access_locks(&after.params), IOLINK_LOCK_LOCAL_UI);

    /* Factory reset with failing NVM reports failure and keeps RAM. */
    ds_dev_t failing;
    dev_init(&failing, &cfg, NVM_OFF_FAIL, NULL);
    dev_set_tags(&failing, "A", "F", "L", false);
    iolink_device_info_ctx_set_access_locks(&failing.info, IOLINK_LOCK_DS);
    assert_int_equal(iolink_params_ctx_factory_reset_checked(&failing.params), -1);
    assert_tags(&failing, "A", "F", "L");
    assert_int_equal(iolink_params_ctx_get_access_locks(&failing.params), IOLINK_LOCK_DS);
    assert_int_equal(iolink_params_ctx_set_access_locks(&failing.params, 0U, true), -1);
    assert_int_equal(iolink_params_ctx_get_access_locks(&failing.params), IOLINK_LOCK_DS);
}

/* ---- NVM record layout -------------------------------------------------- */

static void test_params_nvm_layout_and_range(void** state)
{
    (void) state;
    ds_dev_t dev;
    uint8_t rec[IOLINK_PARAMS_NVM_RECORD_SIZE];

    nvm_clear(NVM_OFF_C);
    dev_init(&dev, NULL, NVM_OFF_C, NULL);
    dev_set_tags(&dev, "APP", "FUN", "LOC", true);

    assert_int_equal(iolink_nvm_read(NVM_OFF_C, rec, sizeof(rec)), 0);
    assert_memory_equal(rec, "IOLP", 4U);
    assert_int_equal(rec[4], IOLINK_PARAMS_NVM_LAYOUT_VERSION);
    assert_int_equal(rec[8], 3U);
    assert_memory_equal(&rec[9], "APP", 3U);

    /* Corrupt one octet: the checksum rejects the record; defaults apply. */
    rec[9] ^= 0x20U;
    assert_int_equal(iolink_nvm_write(NVM_OFF_C, rec, sizeof(rec)), 0);
    iolink_device_info_ctx_t info;
    iolink_params_ctx_t params;
    iolink_device_info_ctx_init(&info, NULL);
    assert_int_equal(iolink_params_ctx_init_at(&params, &info, NVM_OFF_C), 1);
    assert_tag(&params, IOLINK_IDX_FUNCTION_TAG, "");

    /* Unknown layout version with a valid checksum is also rejected. */
    rec[9] ^= 0x20U;
    rec[4] = 99U;
    assert_int_equal(iolink_nvm_write(NVM_OFF_C, rec, sizeof(rec)), 0);
    assert_int_equal(iolink_params_ctx_init_at(&params, &info, NVM_OFF_C), 1);

    /* Legacy layout 1 ("IOL1" native struct) is still restored. */
    struct
    {
        uint32_t magic;
        char application_tag[33];
        char function_tag[33];
        char location_tag[33];
    } legacy;
    memset(&legacy, 0, sizeof(legacy));
    legacy.magic = 0x494F4C31U;
    memcpy(legacy.application_tag, "OLD-APP", 7U);
    memcpy(legacy.function_tag, "OLD-FUN", 7U);
    memset(legacy.location_tag, 'Z', sizeof(legacy.location_tag)); /* no terminator */
    nvm_clear(NVM_OFF_C);
    assert_int_equal(iolink_nvm_write(NVM_OFF_C, (const uint8_t*) &legacy, sizeof(legacy)), 0);
    iolink_device_info_ctx_init(&info, NULL);
    assert_int_equal(iolink_params_ctx_init_at(&params, &info, NVM_OFF_C), 0);
    assert_tag(&params, IOLINK_IDX_APPLICATION_TAG, "OLD-APP");
    assert_tag(&params, IOLINK_IDX_FUNCTION_TAG, "OLD-FUN");
    assert_tag(&params, IOLINK_IDX_LOCATION_TAG, "ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ");

    /* Offset whose record would wrap the 32-bit address space is refused. */
    assert_int_equal(iolink_params_ctx_init_at(&params, &info, 0xFFFFFFF0U), -1);
    assert_int_equal(iolink_params_ctx_set(&params, IOLINK_IDX_FUNCTION_TAG, 0U,
                                           (const uint8_t*) "X", 1U, true),
                     -1);
    assert_int_equal(iolink_params_ctx_set(&params, IOLINK_IDX_FUNCTION_TAG, 0U,
                                           (const uint8_t*) "X", 1U, false),
                     0);

    /* Tags longer than 32 octets are rejected, not truncated. */
    uint8_t long_tag[IOLINK_PARAMS_TAG_MAX + 1U];
    memset(long_tag, 'T', sizeof(long_tag));
    assert_int_equal(iolink_params_ctx_set(&params, IOLINK_IDX_LOCATION_TAG, 0U, long_tag,
                                           sizeof(long_tag), false),
                     -1);
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
        cmocka_unit_test(test_ds_bound_params_restore_device_context_only),
        cmocka_unit_test(test_ds_apply_rejects_truncated),
        cmocka_unit_test(test_ds_download_rejects_unknown_index),
        cmocka_unit_test(test_ds_rejects_invalid_images_before_write),
        cmocka_unit_test(test_ds_download_session_commit_and_checksum),
        cmocka_unit_test(test_ds_rejected_stage_discards_previous),
        cmocka_unit_test(test_ds_break_discards_staging),
        cmocka_unit_test(test_ds_nvm_failure_keeps_snapshot),
        cmocka_unit_test(test_ds_download_store_and_backend),
        cmocka_unit_test(test_ds_multi_device_isolation),
        cmocka_unit_test(test_ds_factory_reset_and_access_locks),
        cmocka_unit_test(test_params_nvm_layout_and_range),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
