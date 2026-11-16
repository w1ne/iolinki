/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_integration_ds_reset.c
 * @brief Device-level integration: ISDU -> Data Storage -> NVM, SystemCommand
 *        resets, access locks, events and last valid Process Data.
 *
 * Every request travels as real M-sequences through iolink_device_process():
 * Type 0 in PREOPERATE, Type 2_2 (2 PDin, 2 PDout, 1 OD) in OPERATE. The test
 * owns the clock and a RAM NVM with write-fault injection.
 *
 * References: IO-Link V1.1.5 10.3.5 (Table 98), 10.4 (Table 100), 10.7
 * (Table 101), B.2.3 (Table B.10/B.11), B.2.4 (Table B.12), B.2.23/B.2.24,
 * 7.3.7 (Table 55).
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <cmocka.h>

#include "iolinki/crc.h"
#include "iolinki/data_storage.h"
#include "iolinki/device.h"
#include "iolinki/device_info.h"
#include "iolinki/events.h"
#include "iolinki/params.h"
#include "iolinki/platform.h"
#include "iolinki/protocol.h"
#include "iolinki/time_utils.h"

/* ---- Controlled clock (overrides the weak platform defaults) ----------- */

static uint64_t g_now_us;

uint32_t iolink_time_get_ms(void)
{
    return (uint32_t) (g_now_us / 1000U);
}

uint64_t iolink_time_get_us(void)
{
    return g_now_us;
}

/* ---- RAM NVM with fault injection -------------------------------------- */

static uint8_t g_nvm[1024];
static bool g_nvm_fail;
static int g_nvm_writes;

int iolink_nvm_read(uint32_t offset, uint8_t* data, size_t len)
{
    if (((size_t) offset + len) > sizeof(g_nvm)) {
        return -1;
    }
    (void) memcpy(data, &g_nvm[offset], len);
    return 0;
}

int iolink_nvm_write(uint32_t offset, const uint8_t* data, size_t len)
{
    g_nvm_writes++;
    if (g_nvm_fail || (((size_t) offset + len) > sizeof(g_nvm))) {
        return -1;
    }
    (void) memcpy(&g_nvm[offset], data, len);
    return 0;
}

/* ---- PHY: one queued master message, captured device reply ------------- */

static uint8_t g_rx[48];
static size_t g_rx_len;
static size_t g_rx_pos;
static uint8_t g_tx[48];
static size_t g_tx_len;
static int g_tx_count;
static iolink_phy_mode_t g_phy_mode;

static int phy_recv(void* user, uint8_t* byte)
{
    (void) user;
    if (g_rx_pos < g_rx_len) {
        *byte = g_rx[g_rx_pos++];
        return 1;
    }
    return 0;
}

static int phy_send(void* user, const uint8_t* data, size_t len)
{
    (void) user;
    assert_true(len <= sizeof(g_tx));
    (void) memcpy(g_tx, data, len);
    g_tx_len = len;
    g_tx_count++;
    return 0;
}

static void phy_set_mode(void* user, iolink_phy_mode_t mode)
{
    (void) user;
    g_phy_mode = mode;
}

/* ---- Application reset handler ----------------------------------------- */

static int g_handler_calls[4];
static int g_handler_total;
static int g_tx_count_at_handler;

static void reset_handler(iolink_reset_type_t type)
{
    assert_true((unsigned) type < 4U);
    g_handler_calls[type]++;
    g_handler_total++;
    g_tx_count_at_handler = g_tx_count;
}

/* ---- Device under test -------------------------------------------------- */

static iolink_device_ctx_t g_dev;
static iolink_device_config_t g_cfg;
static int g_vendor_writes;
static int g_vendor_reads;

/* Vendor ISDU service for index 0x0040 (device-specific parameter). */
static uint8_t vendor_service(void* user, uint16_t index, uint8_t subindex, bool write,
                              const uint8_t* input, size_t length, uint8_t* output,
                              size_t* output_length)
{
    (void) user;
    (void) subindex;
    (void) input;
    (void) length;
    if (index != 0x0040U) {
        return IOLINK_ISDU_ERROR_INDEX_NOT_AVAIL;
    }
    if (write) {
        g_vendor_writes++;
        *output_length = 0U;
    }
    else {
        g_vendor_reads++;
        output[0] = 0x5AU;
        *output_length = 1U;
    }
    return 0U;
}
static uint8_t g_pdout[2];

static const iolink_device_info_t k_info = {
    .vendor_name = "iolinki",
    .product_name = "integration",
    .access_locks = IOLINK_LOCK_LOCAL_UI, /* factory default of DeviceAccessLocks */
};

/** Send one master message and run one device cycle. Returns reply length (0 = none). */
static size_t master_msg(uint8_t mc, uint8_t type_bits, const uint8_t* body, size_t n)
{
    uint8_t f[sizeof(g_rx)];
    assert_true((n + 2U) <= sizeof(f));
    f[0] = mc;
    f[1] = type_bits;
    if (n > 0U) {
        (void) memcpy(&f[2], body, n);
    }
    f[1] = (uint8_t) (f[1] | (iolink_checksum6(f, n + 2U) & 0x3FU));
    (void) memcpy(g_rx, f, n + 2U);
    g_rx_len = n + 2U;
    g_rx_pos = 0U;
    g_now_us += 2000U; /* 2 ms master cycle */
    const int before = g_tx_count;
    g_tx_len = 0U;
    iolink_device_process(&g_dev);
    return (g_tx_count != before) ? g_tx_len : 0U;
}

static bool in_operate(void)
{
    return iolink_device_get_state(&g_dev) == IOLINK_DLL_STATE_OPERATE;
}

/** One OD octet write on @p channel with @p addr (Type 0 or Type 2_2 + PDout). */
static void od_write(uint8_t channel, uint8_t addr, uint8_t octet)
{
    const uint8_t mc = (uint8_t) (channel | addr);
    if (in_operate() && (channel != IOLINK_MC_CHANNEL_PAGE)) {
        const uint8_t body[3] = {g_pdout[0], g_pdout[1], octet};
        assert_int_equal(master_msg(mc, IOLINK_MSEQ_TYPE_2, body, 3U), 3U);
    }
    else {
        assert_int_equal(master_msg(mc, IOLINK_MSEQ_TYPE_0, &octet, 1U), 1U);
    }
}

/** One OD octet read on @p channel with @p addr; returns the OD octet. */
static uint8_t od_read(uint8_t channel, uint8_t addr)
{
    const uint8_t mc = (uint8_t) (IOLINK_MC_RW_MASK | channel | addr);
    if (in_operate() && (channel != IOLINK_MC_CHANNEL_PAGE)) {
        assert_int_equal(master_msg(mc, IOLINK_MSEQ_TYPE_2, g_pdout, 2U), 4U);
    }
    else {
        assert_int_equal(master_msg(mc, IOLINK_MSEQ_TYPE_0, NULL, 0U), 2U);
    }
    return g_tx[0];
}

/** Idle cyclic message (no OD service). */
static void idle_cycle(void)
{
    if (in_operate()) {
        (void) master_msg(IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PROCESS, IOLINK_MSEQ_TYPE_2,
                          g_pdout, 2U);
    }
    else {
        (void) od_read(IOLINK_MC_CHANNEL_ISDU, IOLINK_FLOWCTRL_IDLE);
    }
}

static uint8_t next_count(uint8_t fc)
{
    return (fc == IOLINK_FLOWCTRL_START) ? 1U : (uint8_t) ((fc + 1U) & 0x0FU);
}

/** Full ISDU transfer (A.5, Table 52). Returns the response octet count. */
static size_t isdu_transfer(const uint8_t* req, size_t n, uint8_t* rsp, size_t max)
{
    uint8_t fc = IOLINK_FLOWCTRL_START;
    for (size_t i = 0U; i < n; i++) {
        od_write(IOLINK_MC_CHANNEL_ISDU, fc, req[i]);
        fc = next_count(fc);
    }
    uint8_t first = od_read(IOLINK_MC_CHANNEL_ISDU, IOLINK_FLOWCTRL_START);
    for (int guard = 0; (first == 0x01U) && (guard < 8); guard++) {
        first = od_read(IOLINK_MC_CHANNEL_ISDU, IOLINK_FLOWCTRL_START); /* Busy */
    }
    rsp[0] = first;
    size_t total = (size_t) (first & 0x0FU);
    fc = 1U;
    if (total == 1U) {
        rsp[1] = od_read(IOLINK_MC_CHANNEL_ISDU, fc);
        fc = next_count(fc);
        total = rsp[1];
        for (size_t i = 2U; i < total; i++) {
            assert_true(i < max);
            rsp[i] = od_read(IOLINK_MC_CHANNEL_ISDU, fc);
            fc = next_count(fc);
        }
    }
    else {
        for (size_t i = 1U; i < total; i++) {
            assert_true(i < max);
            rsp[i] = od_read(IOLINK_MC_CHANNEL_ISDU, fc);
            fc = next_count(fc);
        }
    }
    uint8_t chk = 0U;
    for (size_t i = 0U; i < total; i++) {
        chk ^= rsp[i];
    }
    assert_int_equal(chk, 0U); /* CHKPDU */
    return total;
}

/** Build an 8-bit Index + Subindex request (services 0x2 / 0xA). */
static size_t isdu_request(uint8_t* out, bool write, uint8_t index, uint8_t sub,
                           const uint8_t* data, size_t n)
{
    const uint8_t service = write ? 0x2U : 0xAU;
    size_t pos = 0U;
    size_t total = n + 4U;
    if (total > 15U) {
        total++;
        out[pos++] = (uint8_t) ((service << 4) | 1U);
        out[pos++] = (uint8_t) total;
    }
    else {
        out[pos++] = (uint8_t) ((service << 4) | total);
    }
    out[pos++] = index;
    out[pos++] = sub;
    if (n > 0U) {
        (void) memcpy(&out[pos], data, n);
        pos += n;
    }
    uint8_t chk = 0U;
    for (size_t i = 0U; i < pos; i++) {
        chk ^= out[i];
    }
    out[pos++] = chk;
    assert_int_equal(pos, total);
    return pos;
}

/** ISDU write; returns 0 on Write Response (+), else the AdditionalCode. */
static int isdu_write(uint8_t index, uint8_t sub, const uint8_t* data, size_t n)
{
    uint8_t req[64];
    uint8_t rsp[64];
    size_t len = isdu_request(req, true, index, sub, data, n);
    size_t r = isdu_transfer(req, len, rsp, sizeof(rsp));
    if ((r == 2U) && (rsp[0] == 0x52U)) {
        return 0;
    }
    assert_int_equal(r, 4U);
    assert_int_equal(rsp[0], 0x44U);
    assert_int_equal(rsp[1], 0x80U);
    return 0x8000 | rsp[2];
}

/** ISDU read; returns data length on Read Response (+), else -(0x8000 | AdditionalCode). */
static int isdu_read(uint8_t index, uint8_t sub, uint8_t* out, size_t max)
{
    uint8_t req[8];
    uint8_t rsp[256];
    size_t len = isdu_request(req, false, index, sub, NULL, 0U);
    size_t r = isdu_transfer(req, len, rsp, sizeof(rsp));
    if ((rsp[0] & 0xF0U) == 0xC0U) {
        assert_int_equal(r, 4U);
        assert_int_equal(rsp[1], 0x80U);
        return -(0x8000 | rsp[2]);
    }
    assert_int_equal(rsp[0] & 0xF0U, 0xD0U);
    size_t off = ((rsp[0] & 0x0FU) == 1U) ? 2U : 1U;
    size_t dlen = r - off - 1U;
    assert_true(dlen <= max);
    (void) memcpy(out, &rsp[off], dlen);
    return (int) dlen;
}

static int sys_cmd(uint8_t cmd)
{
    return isdu_write(IOLINK_IDX_SYSTEM_COMMAND, 0U, &cmd, 1U);
}

static int ds_cmd(uint8_t cmd)
{
    return isdu_write(IOLINK_IDX_DATA_STORAGE, IOLINK_DS_SUBIDX_COMMAND, &cmd, 1U);
}

static int write_tag(uint8_t index, const char* value)
{
    return isdu_write(index, 0U, (const uint8_t*) value, strlen(value));
}

static void expect_tag(uint8_t index, const char* value)
{
    uint8_t buf[40];
    int n = isdu_read(index, 0U, buf, sizeof(buf));
    assert_int_equal(n, (int) strlen(value));
    assert_memory_equal(buf, value, strlen(value));
}

static uint8_t read_u8(uint8_t index, uint8_t sub)
{
    uint8_t v[4];
    assert_int_equal(isdu_read(index, sub, v, sizeof(v)), 1);
    return v[0];
}

static uint16_t read_locks(void)
{
    uint8_t v[4];
    assert_int_equal(isdu_read(IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U, v, sizeof(v)), 2);
    return (uint16_t) ((v[0] << 8) | v[1]);
}

/** Tags restored from NVM by a fresh parameter context (power-cycle view). */
static void expect_nvm_tag(uint16_t index, const char* value)
{
    iolink_device_info_ctx_t info;
    iolink_params_ctx_t params;
    uint8_t buf[40];
    iolink_device_info_ctx_init(&info, &k_info);
    assert_int_equal(iolink_params_ctx_init_at(&params, &info, 0U), 0);
    int n = iolink_params_ctx_get(&params, index, 0U, buf, sizeof(buf));
    assert_int_equal(n, (int) strlen(value));
    assert_memory_equal(buf, value, strlen(value));
}

static uint16_t nvm_locks(void)
{
    iolink_device_info_ctx_t info;
    iolink_params_ctx_t params;
    iolink_device_info_ctx_init(&info, &k_info);
    assert_int_equal(iolink_params_ctx_init_at(&params, &info, 0U), 0);
    return iolink_params_ctx_get_access_locks(&params);
}

static void dev_start(bool with_handler)
{
    (void) memset(&g_dev, 0, sizeof(g_dev));
    (void) memset(&g_cfg, 0, sizeof(g_cfg));
    g_cfg.phy.recv_byte = phy_recv;
    g_cfg.phy.send = phy_send;
    g_cfg.phy.set_mode = phy_set_mode;
    g_cfg.stack.m_seq_type = IOLINK_M_SEQ_TYPE_2_2;
    g_cfg.stack.pd_in_len = 2U;
    g_cfg.stack.pd_out_len = 2U;
    g_cfg.device_info = &k_info;
    g_cfg.vendor_service = vendor_service;
    assert_int_equal(iolink_device_init(&g_dev, &g_cfg), 0);
    if (with_handler) {
        iolink_device_set_reset_handler(&g_dev, reset_handler);
    }
    /* Startup probe: Type-0 read of Direct Parameter 0x02 -> PREOPERATE. */
    assert_int_equal(master_msg(0xA2U, IOLINK_MSEQ_TYPE_0, NULL, 0U), 2U);
    assert_int_equal(iolink_device_get_state(&g_dev), IOLINK_DLL_STATE_PREOPERATE);
}

static void go_operate(void)
{
    const uint8_t dev_operate = IOLINK_CMD_DEVICE_OPERATE;
    assert_int_equal(master_msg(IOLINK_MC_CHANNEL_PAGE, IOLINK_MSEQ_TYPE_0, &dev_operate, 1U),
                     1U);
    assert_int_equal(iolink_device_get_state(&g_dev), IOLINK_DLL_STATE_ESTAB_COM);
    /* The first Type 2_2 message moves ESTAB_COM -> OPERATE. */
    assert_int_equal(master_msg(IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_PROCESS, IOLINK_MSEQ_TYPE_2,
                                g_pdout, 2U),
                     4U);
    assert_true(in_operate());
}

static bool queued_event(uint16_t code, iolink_event_mode_t mode)
{
    iolink_event_t list[IOLINK_EVENT_QUEUE_SIZE];
    uint8_t n = iolink_events_get_all(iolink_device_get_events_ctx(&g_dev), list,
                                      (uint8_t) IOLINK_EVENT_QUEUE_SIZE);
    for (uint8_t i = 0U; i < n; i++) {
        if ((list[i].code == code) && (list[i].mode == mode)) {
            return true;
        }
    }
    return false;
}

static int setup(void** state)
{
    (void) state;
    g_now_us = 10000000U; /* 10 s */
    (void) memset(g_nvm, 0, sizeof(g_nvm));
    g_nvm_fail = false;
    g_nvm_writes = 0;
    g_tx_count = 0;
    g_phy_mode = IOLINK_PHY_MODE_SDCI;
    (void) memset(g_handler_calls, 0, sizeof(g_handler_calls));
    g_handler_total = 0;
    g_tx_count_at_handler = -1;
    g_pdout[0] = 0U;
    g_pdout[1] = 0U;
    return 0;
}

/* ---- 1. ISDU -> DS -> NVM: staged session commits once at End ---------- */

static void test_block_download_stages_and_commits_atomically(void** state)
{
    (void) state;
    dev_start(false);
    iolink_ds_ctx_t* ds = iolink_device_get_ds_ctx(&g_dev);

    assert_int_equal(write_tag(IOLINK_IDX_APPLICATION_TAG, "APP-OLD"), 0);
    assert_int_equal(write_tag(IOLINK_IDX_FUNCTION_TAG, "FUN-OLD"), 0);
    expect_nvm_tag(IOLINK_IDX_APPLICATION_TAG, "APP-OLD");
    uint8_t cs_before[4];
    assert_int_equal(isdu_read(IOLINK_IDX_DATA_STORAGE, IOLINK_DS_SUBIDX_PARAM_CHECKSUM,
                               cs_before, sizeof(cs_before)),
                     4);

    assert_int_equal(sys_cmd(IOLINK_CMD_PARAM_DOWNLOAD_START), 0);
    const int writes_at_start = g_nvm_writes;

    /* A single parameter write and a 0x0003 image write are only staged. */
    assert_int_equal(write_tag(IOLINK_IDX_APPLICATION_TAG, "APP-NEW"), 0);
    const uint8_t img[] = {0x00U, 0x19U, 0x00U, 0x07U, 'F', 'U', 'N', '-', 'N', 'E', 'W'};
    assert_int_equal(isdu_write(IOLINK_IDX_DATA_STORAGE, 0U, img, sizeof(img)), 0);
    expect_tag(IOLINK_IDX_APPLICATION_TAG, "APP-OLD");
    expect_tag(IOLINK_IDX_FUNCTION_TAG, "FUN-OLD");
    assert_int_equal(g_nvm_writes, writes_at_start);
    assert_int_equal(read_u8(IOLINK_IDX_DATA_STORAGE, IOLINK_DS_SUBIDX_STATE_PROPERTY),
                     IOLINK_DS_STATE_PROPERTY_DOWNLOAD);

    /* Device cycles without traffic never close or commit the session. */
    for (int i = 0; i < 50; i++) {
        g_now_us += 2000U;
        iolink_device_process(&g_dev);
    }
    assert_int_equal(ds->state, IOLINK_DS_STATE_DOWNLOADING);
    expect_tag(IOLINK_IDX_APPLICATION_TAG, "APP-OLD");

    /* End: one NVM write for the whole set, then the new values are live. */
    assert_int_equal(sys_cmd(IOLINK_CMD_PARAM_DOWNLOAD_END), 0);
    assert_int_equal(g_nvm_writes, writes_at_start + 1);
    assert_int_equal(ds->state, IOLINK_DS_STATE_IDLE);
    expect_tag(IOLINK_IDX_APPLICATION_TAG, "APP-NEW");
    expect_tag(IOLINK_IDX_FUNCTION_TAG, "FUN-NEW");
    expect_nvm_tag(IOLINK_IDX_APPLICATION_TAG, "APP-NEW");
    expect_nvm_tag(IOLINK_IDX_FUNCTION_TAG, "FUN-NEW");
    uint8_t cs_after[4];
    assert_int_equal(isdu_read(IOLINK_IDX_DATA_STORAGE, IOLINK_DS_SUBIDX_PARAM_CHECKSUM,
                               cs_after, sizeof(cs_after)),
                     4);
    assert_memory_not_equal(cs_before, cs_after, 4U);
    /* ParamDownloadEnd alone raises no Data Storage upload request. */
    assert_false(queued_event(IOLINK_EVENTCODE_DS_UPLOAD_REQUEST, IOLINK_EVENT_MODE_SINGLE_SHOT));
    assert_int_equal(read_u8(IOLINK_IDX_DATA_STORAGE, IOLINK_DS_SUBIDX_STATE_PROPERTY), 0x00U);
}

/* ---- 2. Malformed or failed commit: no live mutation ------------------- */

static void test_block_download_failure_keeps_previous_set(void** state)
{
    (void) state;
    dev_start(false);
    iolink_ds_ctx_t* ds = iolink_device_get_ds_ctx(&g_dev);
    assert_int_equal(write_tag(IOLINK_IDX_LOCATION_TAG, "LOC-OLD"), 0);

    /* Malformed image (truncated record) in a session: 0x8041, set invalid. */
    assert_int_equal(sys_cmd(IOLINK_CMD_PARAM_DOWNLOAD_START), 0);
    assert_int_equal(write_tag(IOLINK_IDX_LOCATION_TAG, "LOC-NEW"), 0);
    const uint8_t bad[] = {0x00U, 0x1AU, 0x00U, 0x09U, 'X'};
    assert_int_equal(isdu_write(IOLINK_IDX_DATA_STORAGE, 0U, bad, sizeof(bad)), 0x8041);
    const int writes = g_nvm_writes;
    assert_int_equal(sys_cmd(IOLINK_CMD_PARAM_DOWNLOAD_END), 0x8041);
    assert_int_equal(g_nvm_writes, writes);
    assert_int_equal(ds->state, IOLINK_DS_STATE_IDLE);
    expect_tag(IOLINK_IDX_LOCATION_TAG, "LOC-OLD");
    expect_nvm_tag(IOLINK_IDX_LOCATION_TAG, "LOC-OLD");

    /* Over-long tag in a session: VAL_LENOVRRUN, then Store fails 0x8041 and
       no Data Storage upload request is raised (Table 98 rule 7). */
    assert_int_equal(sys_cmd(IOLINK_CMD_PARAM_DOWNLOAD_START), 0);
    assert_int_equal(write_tag(IOLINK_IDX_LOCATION_TAG, "0123456789012345678901234567890123"),
                     0x8033);
    assert_int_equal(sys_cmd(IOLINK_CMD_PARAM_DOWNLOAD_STORE), 0x8041);
    expect_tag(IOLINK_IDX_LOCATION_TAG, "LOC-OLD");
    assert_false(queued_event(IOLINK_EVENTCODE_DS_UPLOAD_REQUEST, IOLINK_EVENT_MODE_SINGLE_SHOT));
    assert_int_equal(read_u8(IOLINK_IDX_DATA_STORAGE, IOLINK_DS_SUBIDX_STATE_PROPERTY), 0x00U);

    /* NVM failure on Store: APP_DEV 0x8000, RAM and NVM keep the old set. */
    assert_int_equal(sys_cmd(IOLINK_CMD_PARAM_DOWNLOAD_START), 0);
    assert_int_equal(write_tag(IOLINK_IDX_LOCATION_TAG, "LOC-NEW"), 0);
    g_nvm_fail = true;
    assert_int_equal(sys_cmd(IOLINK_CMD_PARAM_DOWNLOAD_STORE), 0x8000);
    g_nvm_fail = false;
    assert_int_equal(ds->last_result, IOLINK_DS_ERR_STORAGE);
    expect_tag(IOLINK_IDX_LOCATION_TAG, "LOC-OLD");
    expect_nvm_tag(IOLINK_IDX_LOCATION_TAG, "LOC-OLD");
    assert_false(queued_event(IOLINK_EVENTCODE_DS_UPLOAD_REQUEST, IOLINK_EVENT_MODE_SINGLE_SHOT));

    /* Outside a session a malformed image is refused without any NVM write;
       an NVM failure is APP_DEV, not "inconsistent". */
    const int w2 = g_nvm_writes;
    assert_int_equal(isdu_write(IOLINK_IDX_DATA_STORAGE, 0U, bad, sizeof(bad)), 0x8041);
    assert_int_equal(g_nvm_writes, w2);
    const uint8_t good[] = {0x00U, 0x1AU, 0x00U, 0x01U, 'Z'};
    g_nvm_fail = true;
    assert_int_equal(isdu_write(IOLINK_IDX_DATA_STORAGE, 0U, good, sizeof(good)), 0x8000);
    g_nvm_fail = false;
    expect_tag(IOLINK_IDX_LOCATION_TAG, "LOC-OLD");
    assert_int_equal(isdu_write(IOLINK_IDX_DATA_STORAGE, 0U, good, sizeof(good)), 0);
    expect_tag(IOLINK_IDX_LOCATION_TAG, "Z");

    /* Single tag write with NVM failure: APP_DEV and the old value stays. */
    g_nvm_fail = true;
    assert_int_equal(write_tag(IOLINK_IDX_LOCATION_TAG, "NOPE"), 0x8000);
    g_nvm_fail = false;
    expect_tag(IOLINK_IDX_LOCATION_TAG, "Z");
}

/* ---- 3. ParamDownloadStore -> DS_UPLOAD_REQ Event, DS_Command, Table B.10 */

static void test_download_store_raises_ds_upload_request(void** state)
{
    (void) state;
    dev_start(false);

    assert_int_equal(sys_cmd(IOLINK_CMD_PARAM_DOWNLOAD_START), 0);
    assert_int_equal(write_tag(IOLINK_IDX_APPLICATION_TAG, "STORED"), 0);
    assert_int_equal(sys_cmd(IOLINK_CMD_PARAM_DOWNLOAD_STORE), 0);
    expect_tag(IOLINK_IDX_APPLICATION_TAG, "STORED");
    expect_nvm_tag(IOLINK_IDX_APPLICATION_TAG, "STORED");
    assert_int_equal(read_u8(IOLINK_IDX_DATA_STORAGE, IOLINK_DS_SUBIDX_STATE_PROPERTY),
                     IOLINK_DS_STATE_PROPERTY_UPLOAD_FLAG);

    /* Event flag in CKS bit 7; event memory over the diagnosis channel shows
       a single shot Notification 0xFF91 (Table D.1, A.6.4). */
    idle_cycle();
    assert_true((g_tx[g_tx_len - 1U] & 0x80U) != 0U);
    const uint8_t status = od_read(IOLINK_MC_CHANNEL_DIAGNOSIS, 0U);
    assert_int_equal(status, 0x81U);
    assert_int_equal(od_read(IOLINK_MC_CHANNEL_DIAGNOSIS, 1U), 0x54U);
    assert_int_equal(od_read(IOLINK_MC_CHANNEL_DIAGNOSIS, 2U), 0xFFU);
    assert_int_equal(od_read(IOLINK_MC_CHANNEL_DIAGNOSIS, 3U), 0x91U);
    od_write(IOLINK_MC_CHANNEL_DIAGNOSIS, 0U, 0x00U); /* confirm the readout */
    idle_cycle();
    assert_true((g_tx[g_tx_len - 1U] & 0x80U) == 0U);

    /* Table B.10/B.11 read objects. */
    uint8_t buf[32];
    assert_int_equal(isdu_read(IOLINK_IDX_DATA_STORAGE, IOLINK_DS_SUBIDX_STORAGE_SIZE, buf,
                               sizeof(buf)),
                     4);
    const uint32_t size = ((uint32_t) buf[0] << 24) | ((uint32_t) buf[1] << 16) |
                          ((uint32_t) buf[2] << 8) | buf[3];
    assert_int_equal(size, iolink_ds_storage_size());
    assert_true(size <= 2048U);
    const uint8_t index_list[] = {0x00U, 0x18U, 0x00U, 0x00U, 0x19U, 0x00U,
                                  0x00U, 0x1AU, 0x00U, 0x00U, 0x00U};
    assert_int_equal(isdu_read(IOLINK_IDX_DATA_STORAGE, IOLINK_DS_SUBIDX_INDEX_LIST, buf,
                               sizeof(buf)),
                     (int) sizeof(index_list));
    assert_memory_equal(buf, index_list, sizeof(index_list));
    assert_int_equal(isdu_read(IOLINK_IDX_DATA_STORAGE, 6U, buf, sizeof(buf)), -0x8012);
    const uint8_t two[2] = {0U, 0U};
    assert_int_equal(isdu_write(IOLINK_IDX_DATA_STORAGE, IOLINK_DS_SUBIDX_STATE_PROPERTY, two, 1U),
                     0x8023);

    /* Master DS upload: DS_UploadStart .. DS_UploadEnd clears the flag (T9). */
    assert_int_equal(ds_cmd(IOLINK_DS_COMMAND_UPLOAD_END), 0x8036);
    assert_int_equal(ds_cmd(IOLINK_DS_COMMAND_UPLOAD_START), 0);
    for (int i = 0; i < 10; i++) {
        idle_cycle();
    }
    assert_int_equal(read_u8(IOLINK_IDX_DATA_STORAGE, IOLINK_DS_SUBIDX_STATE_PROPERTY),
                     IOLINK_DS_STATE_PROPERTY_UPLOAD | IOLINK_DS_STATE_PROPERTY_UPLOAD_FLAG);
    expect_tag(IOLINK_IDX_APPLICATION_TAG, "STORED");
    assert_int_equal(ds_cmd(IOLINK_DS_COMMAND_DOWNLOAD_START), 0x8036);
    assert_int_equal(ds_cmd(IOLINK_DS_COMMAND_UPLOAD_END), 0);
    assert_int_equal(read_u8(IOLINK_IDX_DATA_STORAGE, IOLINK_DS_SUBIDX_STATE_PROPERTY), 0x00U);
    assert_int_equal(ds_cmd(0x06U), 0x8030); /* reserved DS_Command */
    assert_int_equal(isdu_write(IOLINK_IDX_DATA_STORAGE, IOLINK_DS_SUBIDX_COMMAND, two, 2U),
                     0x8033);

    /* Master DS download with DS_Break: nothing changes (T10). */
    assert_int_equal(ds_cmd(IOLINK_DS_COMMAND_DOWNLOAD_START), 0);
    assert_int_equal(write_tag(IOLINK_IDX_APPLICATION_TAG, "BROKEN"), 0);
    assert_int_equal(ds_cmd(IOLINK_DS_COMMAND_BREAK), 0);
    expect_tag(IOLINK_IDX_APPLICATION_TAG, "STORED");

    /* Master DS download with DS_DownloadEnd commits. */
    assert_int_equal(ds_cmd(IOLINK_DS_COMMAND_DOWNLOAD_START), 0);
    assert_int_equal(write_tag(IOLINK_IDX_APPLICATION_TAG, "RESTORED"), 0);
    expect_tag(IOLINK_IDX_APPLICATION_TAG, "STORED");
    assert_int_equal(ds_cmd(IOLINK_DS_COMMAND_DOWNLOAD_END), 0);
    expect_tag(IOLINK_IDX_APPLICATION_TAG, "RESTORED");
    expect_nvm_tag(IOLINK_IDX_APPLICATION_TAG, "RESTORED");
}

/* ---- 4. DeviceAccessLocks 0x000C: device-local, persistent, checked ---- */

static void test_access_locks_device_local_and_persistent(void** state)
{
    (void) state;
    dev_start(false);
    assert_int_equal(read_locks(), IOLINK_LOCK_LOCAL_UI); /* configured default */

    const uint8_t ds_lock[2] = {0x00U, IOLINK_LOCK_DS};
    assert_int_equal(isdu_write(IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U, ds_lock, 2U), 0);
    assert_int_equal(read_locks(), IOLINK_LOCK_DS);
    assert_int_equal(nvm_locks(), IOLINK_LOCK_DS);
    /* Device-local: the process-wide legacy value is not touched. */
    assert_int_equal(iolink_params_ctx_get_access_locks(&g_dev.params), IOLINK_LOCK_DS);

    /* Validation: reserved bits, length, subindex, NVM failure. */
    const uint8_t reserved[2] = {0x00U, 0x10U};
    assert_int_equal(isdu_write(IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U, reserved, 2U), 0x8030);
    assert_int_equal(isdu_write(IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U, ds_lock, 1U), 0x8034);
    const uint8_t three[3] = {0U, 0U, 0U};
    assert_int_equal(isdu_write(IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U, three, 3U), 0x8033);
    assert_int_equal(isdu_write(IOLINK_IDX_DEVICE_ACCESS_LOCKS, 1U, ds_lock, 2U), 0x8012);
    const uint8_t none[2] = {0U, 0U};
    g_nvm_fail = true;
    assert_int_equal(isdu_write(IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U, none, 2U), 0x8000);
    g_nvm_fail = false;
    assert_int_equal(read_locks(), IOLINK_LOCK_DS);

    /* DS lock: 0x0003 writes are access denied, State_Property "locked",
       Store still works but sets only DS_UPLOAD_FLAG (Table 100 T2). */
    assert_int_equal(ds_cmd(IOLINK_DS_COMMAND_DOWNLOAD_START), 0x8023);
    assert_int_equal(read_u8(IOLINK_IDX_DATA_STORAGE, IOLINK_DS_SUBIDX_STATE_PROPERTY),
                     IOLINK_DS_STATE_PROPERTY_LOCKED);
    assert_int_equal(sys_cmd(IOLINK_CMD_PARAM_DOWNLOAD_STORE), 0);
    assert_false(queued_event(IOLINK_EVENTCODE_DS_UPLOAD_REQUEST, IOLINK_EVENT_MODE_SINGLE_SHOT));
    assert_int_equal(read_u8(IOLINK_IDX_DATA_STORAGE, IOLINK_DS_SUBIDX_STATE_PROPERTY),
                     IOLINK_DS_STATE_PROPERTY_LOCKED | IOLINK_DS_STATE_PROPERTY_UPLOAD_FLAG);

    /* Parameter (write) lock: single writes and block downloads are denied,
       a Data Storage download is not (B.2.4). */
    const uint8_t p_lock[2] = {0x00U, IOLINK_LOCK_PARAM};
    assert_int_equal(isdu_write(IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U, p_lock, 2U), 0);
    assert_int_equal(write_tag(IOLINK_IDX_FUNCTION_TAG, "DENIED"), 0x8023);
    const uint8_t dp2 = 0x42U;
    assert_int_equal(isdu_write(IOLINK_IDX_DIRECT_PARAMETERS_2, 1U, &dp2, 1U), 0x8023);
    g_vendor_writes = 0;
    g_vendor_reads = 0;
    assert_int_equal(isdu_write(0x40U, 0U, &dp2, 1U), 0x8023);
    assert_int_equal(g_vendor_writes, 0);
    uint8_t vbuf[4];
    assert_int_equal(isdu_read(0x40U, 0U, vbuf, sizeof(vbuf)), 1); /* reads unaffected */
    assert_int_equal(g_vendor_reads, 1);
    assert_int_equal(sys_cmd(IOLINK_CMD_PARAM_DOWNLOAD_START), 0);
    assert_int_equal(write_tag(IOLINK_IDX_FUNCTION_TAG, "DENIED"), 0x8023);
    assert_int_equal(sys_cmd(IOLINK_CMD_PARAM_BREAK), 0);
    assert_int_equal(ds_cmd(IOLINK_DS_COMMAND_DOWNLOAD_START), 0);
    assert_int_equal(write_tag(IOLINK_IDX_FUNCTION_TAG, "BY-DS"), 0);
    assert_int_equal(ds_cmd(IOLINK_DS_COMMAND_DOWNLOAD_END), 0);
    expect_tag(IOLINK_IDX_FUNCTION_TAG, "BY-DS");

    /* Unlocked again: DeviceAccessLocks itself is never write-locked. */
    const uint8_t unlock[2] = {0x00U, 0x00U};
    assert_int_equal(isdu_write(IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U, unlock, 2U), 0);
    assert_int_equal(isdu_write(IOLINK_IDX_DIRECT_PARAMETERS_2, 1U, &dp2, 1U), 0);
    assert_int_equal(isdu_write(0x40U, 0U, &dp2, 1U), 0);
    assert_int_equal(g_vendor_writes, 1);
    assert_int_equal(isdu_write(IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U, p_lock, 2U), 0);

    /* Persistence across a power cycle of the same device. */
    dev_start(false);
    assert_int_equal(read_locks(), IOLINK_LOCK_PARAM);
}

/* ---- 5. Restore factory settings: checked, clears status ---------------- */

static void test_restore_factory_settings(void** state)
{
    (void) state;
    dev_start(true);
    iolink_events_ctx_t* ev = iolink_device_get_events_ctx(&g_dev);

    assert_int_equal(write_tag(IOLINK_IDX_APPLICATION_TAG, "A"), 0);
    assert_int_equal(write_tag(IOLINK_IDX_LOCATION_TAG, "L"), 0);
    const uint8_t locks[2] = {0x00U, IOLINK_LOCK_LOCAL_PARAM};
    assert_int_equal(isdu_write(IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U, locks, 2U), 0);
    assert_int_equal(sys_cmd(IOLINK_CMD_PARAM_DOWNLOAD_STORE), 0); /* DS_UPLOAD_FLAG = 1 */
    g_dev.dll.isdu.error_count = 7U;
    assert_true(iolink_event_appear(ev, IOLINK_EVENTCODE_HARDWARE_FAULT, IOLINK_EVENT_TYPE_ERROR));
    iolink_event_trigger(ev, IOLINK_EVENTCODE_TEMPERATURE_OVERRUN, IOLINK_EVENT_TYPE_WARNING);
    assert_int_equal(read_u8(IOLINK_IDX_DEVICE_STATUS, 0U), IOLINK_DEVICE_STATUS_FAILURE);
    uint8_t buf[32];
    assert_int_equal(isdu_read(IOLINK_IDX_DETAILED_DEVICE_STATUS, 0U, buf, sizeof(buf)), 6);

    /* NVM failure: APP_DEV and nothing changes. */
    g_nvm_fail = true;
    assert_int_equal(sys_cmd(IOLINK_CMD_RESTORE_FACTORY_SETTINGS), 0x8000);
    g_nvm_fail = false;
    expect_tag(IOLINK_IDX_APPLICATION_TAG, "A");
    assert_int_equal(read_locks(), IOLINK_LOCK_LOCAL_PARAM);
    assert_int_equal(g_dev.dll.isdu.error_count, 7U);
    assert_int_equal(read_u8(IOLINK_IDX_DEVICE_STATUS, 0U), IOLINK_DEVICE_STATUS_FAILURE);
    assert_int_equal(g_handler_total, 0);

    /* Success. */
    assert_int_equal(sys_cmd(IOLINK_CMD_RESTORE_FACTORY_SETTINGS), 0);
    assert_int_equal(g_handler_calls[IOLINK_RESET_RESTORE_FACTORY], 1);
    expect_tag(IOLINK_IDX_APPLICATION_TAG, "");
    expect_tag(IOLINK_IDX_LOCATION_TAG, "");
    expect_nvm_tag(IOLINK_IDX_LOCATION_TAG, "");
    assert_int_equal(read_locks(), IOLINK_LOCK_LOCAL_UI);
    assert_int_equal(nvm_locks(), IOLINK_LOCK_LOCAL_UI);
    assert_int_equal(g_dev.dll.isdu.error_count, 0U);
    uint8_t ec[2];
    assert_int_equal(isdu_read(IOLINK_IDX_ERROR_COUNT, 0U, ec, sizeof(ec)), 2);
    assert_int_equal(ec[0] | ec[1], 0);
    assert_int_equal(read_u8(IOLINK_IDX_DEVICE_STATUS, 0U), IOLINK_DEVICE_STATUS_OK);
    assert_int_equal(isdu_read(IOLINK_IDX_DETAILED_DEVICE_STATUS, 0U, buf, sizeof(buf)), 0);
    assert_true(queued_event(IOLINK_EVENTCODE_HARDWARE_FAULT, IOLINK_EVENT_MODE_DISAPPEARS));
    /* Table 101: Data Storage "Delete upload request". */
    assert_int_equal(read_u8(IOLINK_IDX_DATA_STORAGE, IOLINK_DS_SUBIDX_STATE_PROPERTY), 0x00U);
}

/* ---- 6. Device reset / Application reset ------------------------------- */

static void test_resets_require_handler(void** state)
{
    (void) state;
    dev_start(false);
    assert_int_equal(sys_cmd(IOLINK_CMD_DEVICE_RESET), 0x8035);
    assert_int_equal(sys_cmd(IOLINK_CMD_APPLICATION_RESET), 0x8035);
    assert_int_equal(write_tag(IOLINK_IDX_APPLICATION_TAG, "KEEP"), 0);
    assert_int_equal(sys_cmd(IOLINK_CMD_BACK_TO_BOX), 0x8035);
    expect_tag(IOLINK_IDX_APPLICATION_TAG, "KEEP");
    for (int i = 0; i < 10; i++) {
        idle_cycle();
    }
    assert_false(g_dev.dll.isdu.reset_pending);
    assert_false(g_dev.dll.isdu.back_to_box_pending);
    assert_false(g_dev.dll.comm_disabled);
}

static void test_device_and_application_reset(void** state)
{
    (void) state;
    dev_start(true);

    /* Device reset: response first, warm start after 3 more M-sequences. */
    assert_int_equal(sys_cmd(IOLINK_CMD_PARAM_DOWNLOAD_START), 0);
    assert_int_equal(sys_cmd(IOLINK_CMD_DEVICE_RESET), 0);
    assert_int_equal(g_handler_total, 0);
    assert_int_equal(iolink_device_get_ds_ctx(&g_dev)->state, IOLINK_DS_STATE_IDLE);
    /* 10.7.2: at least 3 MasterCycles after the last ISDU response octet. */
    idle_cycle();
    idle_cycle();
    assert_int_equal(g_handler_total, 0);
    idle_cycle();
    assert_int_equal(g_handler_calls[IOLINK_RESET_DEVICE], 1);
    idle_cycle();
    idle_cycle();
    assert_int_equal(g_handler_total, 1);
    assert_false(g_dev.dll.isdu.reset_pending);

    /* Application reset: runs before the response; locks back to default;
       Data Storage upload required (Table 101). */
    const uint8_t locks[2] = {0x00U, IOLINK_LOCK_LOCAL_PARAM};
    assert_int_equal(isdu_write(IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U, locks, 2U), 0);
    assert_int_equal(write_tag(IOLINK_IDX_APPLICATION_TAG, "ID"), 0);
    const int tx_before = g_tx_count;
    assert_int_equal(sys_cmd(IOLINK_CMD_APPLICATION_RESET), 0);
    assert_int_equal(g_handler_calls[IOLINK_RESET_APPLICATION], 1);
    assert_true(g_tx_count_at_handler > tx_before);
    assert_int_equal(read_locks(), IOLINK_LOCK_LOCAL_UI);
    assert_int_equal(nvm_locks(), IOLINK_LOCK_LOCAL_UI);
    expect_tag(IOLINK_IDX_APPLICATION_TAG, "ID"); /* identification unchanged */
    assert_true(queued_event(IOLINK_EVENTCODE_DS_UPLOAD_REQUEST, IOLINK_EVENT_MODE_SINGLE_SHOT));
    assert_int_equal(read_u8(IOLINK_IDX_DATA_STORAGE, IOLINK_DS_SUBIDX_STATE_PROPERTY),
                     IOLINK_DS_STATE_PROPERTY_UPLOAD_FLAG);

    /* Application reset with failing NVM: APP_DEV, handler not called. */
    assert_int_equal(isdu_write(IOLINK_IDX_DEVICE_ACCESS_LOCKS, 0U, locks, 2U), 0);
    g_nvm_fail = true;
    assert_int_equal(sys_cmd(IOLINK_CMD_APPLICATION_RESET), 0x8000);
    g_nvm_fail = false;
    assert_int_equal(g_handler_calls[IOLINK_RESET_APPLICATION], 1);
    assert_int_equal(read_locks(), IOLINK_LOCK_LOCAL_PARAM);
}

/* ---- 7. Back-to-box ------------------------------------------------------ */

static void test_back_to_box_stops_communication(void** state)
{
    (void) state;
    dev_start(true);
    assert_int_equal(write_tag(IOLINK_IDX_APPLICATION_TAG, "INSTALLED"), 0);

    g_nvm_fail = true;
    assert_int_equal(sys_cmd(IOLINK_CMD_BACK_TO_BOX), 0x8000);
    g_nvm_fail = false;
    assert_false(g_dev.dll.isdu.back_to_box_pending);
    expect_tag(IOLINK_IDX_APPLICATION_TAG, "INSTALLED");

    assert_int_equal(sys_cmd(IOLINK_CMD_BACK_TO_BOX), 0);
    expect_nvm_tag(IOLINK_IDX_APPLICATION_TAG, "");
    /* 10.7.5: at least 3 MasterCycles after the last ISDU response octet. */
    assert_int_equal(g_handler_total, 0);
    idle_cycle();
    idle_cycle();
    assert_int_equal(g_handler_total, 0);
    assert_false(g_dev.dll.comm_disabled);
    idle_cycle();
    assert_int_equal(g_handler_calls[IOLINK_RESET_BACK_TO_BOX], 1);
    assert_true(g_dev.dll.comm_disabled);
    assert_int_equal(g_phy_mode, IOLINK_PHY_MODE_INACTIVE);

    /* No reply to any further message until re-initialization. */
    assert_int_equal(master_msg(0xA2U, IOLINK_MSEQ_TYPE_0, NULL, 0U), 0U);
    assert_int_equal(master_msg(IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_IDLE,
                                IOLINK_MSEQ_TYPE_0, NULL, 0U),
                     0U);
    g_now_us += 5000000U;
    iolink_device_process(&g_dev);
    assert_true(g_dev.dll.comm_disabled);

    /* Power cycle: communication resumes with factory values. */
    dev_start(true);
    expect_tag(IOLINK_IDX_APPLICATION_TAG, "");
}

/* ---- 8. ProcessDataOutput / ProcessDataInput over real M-sequences ----- */

static void test_process_data_last_valid_over_frames(void** state)
{
    (void) state;
    dev_start(false);
    uint8_t buf[8];

    /* Before OPERATE and before any valid data. */
    assert_int_equal(isdu_read(IOLINK_IDX_PROCESS_DATA_OUTPUT, 0U, buf, sizeof(buf)), -0x8020);
    assert_int_equal(isdu_read(IOLINK_IDX_PROCESS_DATA_INPUT, 0U, buf, sizeof(buf)), -0x8020);

    go_operate();

    /* OPERATE alone does not validate PDout (Table 55: wait for 0x98). */
    g_pdout[0] = 0x11U;
    g_pdout[1] = 0x22U;
    idle_cycle();
    assert_int_equal(isdu_read(IOLINK_IDX_PROCESS_DATA_OUTPUT, 0U, buf, sizeof(buf)), -0x8020);

    /* PDOUTVALID via a Type-0 page write; the next accepted frame counts. */
    const uint8_t valid_cmd = IOLINK_CMD_PD_OUTPUT_OPERATE;
    assert_int_equal(master_msg(IOLINK_MC_CHANNEL_PAGE, IOLINK_MSEQ_TYPE_0, &valid_cmd, 1U), 1U);
    assert_true(g_dev.dll.pd_out_valid);
    g_pdout[0] = 0x33U;
    g_pdout[1] = 0x44U;
    idle_cycle();
    assert_int_equal(isdu_read(IOLINK_IDX_PROCESS_DATA_OUTPUT, 0U, buf, sizeof(buf)), 2);
    assert_int_equal(buf[0], 0x33U);
    assert_int_equal(buf[1], 0x44U);

    /* PDOUTINVALID (0x99 in OPERATE): later PDout does not replace the
       snapshot; the last valid data is returned. */
    const uint8_t invalid_cmd = IOLINK_CMD_DEVICE_OPERATE;
    assert_int_equal(master_msg(IOLINK_MC_CHANNEL_PAGE, IOLINK_MSEQ_TYPE_0, &invalid_cmd, 1U),
                     1U);
    assert_false(g_dev.dll.pd_out_valid);
    assert_true(in_operate());
    g_pdout[0] = 0x55U;
    g_pdout[1] = 0x66U;
    idle_cycle();
    assert_int_equal(isdu_read(IOLINK_IDX_PROCESS_DATA_OUTPUT, 0U, buf, sizeof(buf)), 2);
    assert_int_equal(buf[0], 0x33U);
    assert_int_equal(buf[1], 0x44U);
    uint8_t cur[2];
    assert_int_equal(iolink_device_pd_output_read(&g_dev, cur, sizeof(cur)), 2);
    assert_int_equal(cur[0], 0x55U);

    /* 0x98 carried in a Type 2_2 page write: that frame's PDout counts. */
    const uint8_t frame98[3] = {0x77U, 0x88U, IOLINK_CMD_PD_OUTPUT_OPERATE};
    assert_int_equal(master_msg(IOLINK_MC_CHANNEL_PAGE, IOLINK_MSEQ_TYPE_2, frame98, 3U), 3U);
    uint8_t snap[2];
    assert_int_equal(iolink_dll_get_last_valid_pd_out(&g_dev.dll, snap, sizeof(snap)), 2U);
    assert_int_equal(snap[0], 0x77U);
    assert_int_equal(snap[1], 0x88U);
    assert_int_equal(iolink_dll_get_last_valid_pd_out(&g_dev.dll, snap, 1U), 0U); /* too small */

    /* While valid, every accepted frame updates the snapshot (current data). */
    g_pdout[0] = 0x99U;
    g_pdout[1] = 0xAAU;
    idle_cycle();
    assert_int_equal(isdu_read(IOLINK_IDX_PROCESS_DATA_OUTPUT, 0U, buf, sizeof(buf)), 2);
    assert_int_equal(buf[0], 0x99U);
    assert_int_equal(buf[1], 0xAAU);

    /* ProcessDataInput: invalid-before-valid, valid, valid-then-invalid. */
    const uint8_t in_bad[2] = {0xEEU, 0xEEU};
    assert_int_equal(iolink_device_pd_input_update(&g_dev, in_bad, 2U, false), 0);
    idle_cycle();
    assert_true((g_tx[g_tx_len - 1U] & 0x40U) != 0U); /* CKS: PD invalid */
    assert_int_equal(isdu_read(IOLINK_IDX_PROCESS_DATA_INPUT, 0U, buf, sizeof(buf)), -0x8020);
    const uint8_t in_ok[2] = {0xA1U, 0xB2U};
    assert_int_equal(iolink_device_pd_input_update(&g_dev, in_ok, 2U, true), 0);
    assert_int_equal(iolink_device_pd_input_update(&g_dev, in_bad, 2U, false), 0);
    assert_int_equal(isdu_read(IOLINK_IDX_PROCESS_DATA_INPUT, 0U, buf, sizeof(buf)), 2);
    assert_int_equal(buf[0], 0xA1U);
    assert_int_equal(buf[1], 0xB2U);
    assert_int_equal(iolink_device_pd_input_update(&g_dev, in_ok, IOLINK_PD_IN_MAX_SIZE + 1U,
                                                   true),
                     -1);

    /* Communication loss: validity resets, the snapshot stays. */
    g_now_us += 1500000U;
    iolink_device_process(&g_dev);
    assert_int_equal(iolink_device_get_state(&g_dev), IOLINK_DLL_STATE_STARTUP);
    assert_false(g_dev.dll.pd_out_valid);
    assert_int_equal(iolink_dll_get_last_valid_pd_out(&g_dev.dll, snap, sizeof(snap)), 2U);
    assert_int_equal(snap[0], 0x99U);
    assert_int_equal(snap[1], 0xAAU);
}

/* ---- 9. Communication loss breaks an open DS session -------------------- */

static void test_comm_loss_breaks_download_session(void** state)
{
    (void) state;
    dev_start(false);
    iolink_ds_ctx_t* ds = iolink_device_get_ds_ctx(&g_dev);
    assert_int_equal(write_tag(IOLINK_IDX_FUNCTION_TAG, "F0"), 0);
    assert_int_equal(ds_cmd(IOLINK_DS_COMMAND_DOWNLOAD_START), 0);
    assert_int_equal(write_tag(IOLINK_IDX_FUNCTION_TAG, "F1"), 0);
    for (int i = 0; i < 20; i++) {
        idle_cycle();
    }
    assert_int_equal(ds->state, IOLINK_DS_STATE_DOWNLOADING);

    g_now_us += 1500000U; /* > 1 s without a message */
    iolink_device_process(&g_dev);
    assert_int_equal(iolink_device_get_state(&g_dev), IOLINK_DLL_STATE_STARTUP);
    assert_int_equal(ds->state, IOLINK_DS_STATE_IDLE);
    assert_false(ds->staged_valid);
    expect_nvm_tag(IOLINK_IDX_FUNCTION_TAG, "F0");
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup(test_block_download_stages_and_commits_atomically, setup),
        cmocka_unit_test_setup(test_block_download_failure_keeps_previous_set, setup),
        cmocka_unit_test_setup(test_download_store_raises_ds_upload_request, setup),
        cmocka_unit_test_setup(test_access_locks_device_local_and_persistent, setup),
        cmocka_unit_test_setup(test_restore_factory_settings, setup),
        cmocka_unit_test_setup(test_resets_require_handler, setup),
        cmocka_unit_test_setup(test_device_and_application_reset, setup),
        cmocka_unit_test_setup(test_back_to_box_stops_communication, setup),
        cmocka_unit_test_setup(test_process_data_last_valid_over_frames, setup),
        cmocka_unit_test_setup(test_comm_loss_breaks_download_session, setup),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
