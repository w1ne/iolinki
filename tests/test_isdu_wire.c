/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_isdu_wire.c
 * @brief Byte-exact ISDU transport tests (C3, A.5, Table 52/54).
 *
 * All vectors are computed from the spec formula (CHKPDU = XOR of all ISDU
 * octets with the checksum octet zeroed, A.5.6) and must not be derived from
 * the implementation.
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>

#include "iolinki/isdu.h"
#include "iolinki/params.h"
#include "iolinki/device_info.h"
#include "iolinki/protocol.h"
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

static uint8_t chkpdu(const uint8_t* o, size_t n)
{
    uint8_t c = 0U;
    for (size_t i = 0U; i < n; i++) {
        c ^= o[i];
    }
    return c;
}

/* Two-octet identity object {0x12, 0x34} served at Index 0x0010. */
static const char g_vendor_name[3] = {(char) 0x12, (char) 0x34, '\0'};

static void wire_device_info_init(void)
{
    static iolink_device_info_t info;
    (void) memset(&info, 0, sizeof(info));
    info.vendor_name = g_vendor_name;
    info.vendor_text = "v";
    info.product_name = "p";
    info.product_id = "i";
    info.product_text = "t";
    info.serial_number = "s";
    info.hardware_revision = "h";
    info.firmware_revision = "f";
    info.vendor_id = 0x0123U;
    info.device_id = 0x010203U;
    info.profile_characteristic = 0x0001U;
    info.revision_id = 0x0111U;
    info.min_cycle_time = 0U;
    iolink_device_info_init(&info);
}

/* Feed a full request as consecutive 1-octet messages: START then COUNT 1..0. */
static void wire_write_request(iolink_isdu_ctx_t* ctx, const uint8_t* data, size_t len)
{
    uint8_t fc = IOLINK_FLOWCTRL_START;
    for (size_t i = 0U; i < len; i++) {
        iolink_isdu_od_write(ctx, fc, &data[i], 1U);
        if (fc == IOLINK_FLOWCTRL_START) {
            fc = 1U;
        }
        else {
            fc = (uint8_t) ((fc + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
        }
    }
    iolink_isdu_process(ctx);
}

/* Read a framed response over the ISDU channel with 1-octet reads.
 * On START the first octet is returned; the length nibble determines the rest. */
static size_t wire_read_response(iolink_isdu_ctx_t* ctx, uint8_t* out, size_t out_size)
{
    uint8_t octet = 0U;
    uint8_t fc = IOLINK_FLOWCTRL_START;
    size_t n = 0U;
    size_t total = 0U;

    iolink_isdu_od_read(ctx, fc, &octet, 1U);
    if (n < out_size) {
        out[n] = octet;
    }
    n++;
    if (octet == 0x01U) {
        return 1U; /* Busy */
    }
    uint8_t len = (uint8_t) (octet & 0x0FU);
    if (len == 0U) {
        return 1U; /* No Service */
    }
    total = (len == 1U) ? 0U : len; /* ExtLength read happens below */

    fc = 1U;
    while (1) {
        if ((total != 0U) && (n >= total)) {
            break;
        }
        iolink_isdu_od_read(ctx, fc, &octet, 1U);
        if (n < out_size) {
            out[n] = octet;
        }
        n++;
        if ((total == 0U) && (n == 2U)) {
            len = octet;
            if ((len >= 17U) && (len <= 238U)) {
                total = len;
            }
            else {
                total = n; /* No ext length: two octets were the whole response */
            }
        }
        if ((total != 0U) && (n >= total)) {
            break;
        }
        fc = (uint8_t) ((fc + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
    }
    return n;
}

/* Figure A.20: read 8-bit Index 0x10 -> D4 12 34 F2. */
static void test_isdu_wire_read_figure_a20(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    wire_device_info_init();
    iolink_params_init();
    iolink_isdu_init(&ctx);

    const uint8_t req[3] = {0x93U, 0x10U, 0x83U};
    wire_write_request(&ctx, req, sizeof(req));

    uint8_t resp[4];
    size_t n = wire_read_response(&ctx, resp, sizeof(resp));
    assert_int_equal(n, 4U);
    const uint8_t expected[4] = {0xD4U, 0x12U, 0x34U, 0xF2U};
    assert_memory_equal(resp, expected, sizeof(expected));
}

/* Figure A.20: write Index 0x10 Subindex 0x01 data 0x12 0x34 -> 52 52. */
static void test_isdu_wire_write_figure_a20(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    wire_device_info_init();
    iolink_params_init();
    iolink_isdu_init(&ctx);

    const uint8_t req[6] = {0x26U, 0x10U, 0x01U, 0x12U, 0x34U, 0x11U};
    assert_int_equal(chkpdu(req, 6U), 0U);
    wire_write_request(&ctx, req, sizeof(req));

    uint8_t resp[2];
    size_t n = wire_read_response(&ctx, resp, sizeof(resp));
    assert_int_equal(n, 2U);
    assert_int_equal(resp[0], 0x52U);
    assert_int_equal(resp[1], 0x52U);
}

/* 16-bit read of VendorName 0x0010 with FlowCTRL COUNT wrap past 15. */
static void test_isdu_wire_16bit_count_wrap(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    wire_device_info_init();
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* 16-bit Index + Subindex read (I-Service 0xB, Length 5): B5 00 10 00 A5.
       Send each octet as its own message, wrapping COUNT 15 -> 0. */
    const uint8_t req[5] = {0xB5U, 0x00U, 0x10U, 0x00U, 0xA5U};
    assert_int_equal(chkpdu(req, 5U), 0U);
    uint8_t fc = IOLINK_FLOWCTRL_START;
    for (size_t i = 0U; i < sizeof(req); i++) {
        iolink_isdu_od_write(&ctx, fc, &req[i], 1U);
        fc =
            (fc == IOLINK_FLOWCTRL_START) ? 1U : (uint8_t) ((fc + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
    }
    iolink_isdu_process(&ctx);

    uint8_t resp[4];
    size_t n = wire_read_response(&ctx, resp, sizeof(resp));
    assert_int_equal(n, 4U);
    assert_int_equal(resp[0], 0xD4U);
    assert_int_equal(resp[1], 0x12U);
    assert_int_equal(resp[2], 0x34U);
    assert_int_equal(resp[3], 0xF2U);
}

/* Repeated FlowCTRL repeats the previous message: its payload is ignored. */
static void test_isdu_wire_repeated_flowctrl(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    wire_device_info_init();
    iolink_params_init();
    iolink_isdu_init(&ctx);

    iolink_isdu_od_write(&ctx, IOLINK_FLOWCTRL_START, (const uint8_t[]){0x93U}, 1U);
    iolink_isdu_od_write(&ctx, 1U, (const uint8_t[]){0x10U}, 1U);
    /* Repeat COUNT 1 with a bogus payload: must be ignored. */
    iolink_isdu_od_write(&ctx, 1U, (const uint8_t[]){0xAAU}, 1U);
    iolink_isdu_od_write(&ctx, 2U, (const uint8_t[]){0x83U}, 1U);
    iolink_isdu_process(&ctx);

    uint8_t resp[4];
    size_t n = wire_read_response(&ctx, resp, sizeof(resp));
    assert_int_equal(n, 4U);
    assert_int_equal(resp[3], 0xF2U);
}

/* A FlowCTRL that is neither COUNT+1 nor a repeat is a structure violation:
   the request is dropped and the next read answers No Service (0x00). */
static void test_isdu_wire_flowctrl_error(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    wire_device_info_init();
    iolink_params_init();
    iolink_isdu_init(&ctx);

    iolink_isdu_od_write(&ctx, IOLINK_FLOWCTRL_START, (const uint8_t[]){0x93U}, 1U);
    iolink_isdu_od_write(&ctx, 3U, (const uint8_t[]){0x10U}, 1U); /* skips 1,2 */
    iolink_isdu_process(&ctx);

    uint8_t resp[4];
    size_t n = wire_read_response(&ctx, resp, sizeof(resp));
    assert_int_equal(n, 1U);
    assert_int_equal(resp[0], 0x00U);
}

/* CHKPDU mismatch yields a negative Read Response with APP_DEV / 0x00. */
static void test_isdu_wire_chkpdu_corrupt(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    wire_device_info_init();
    iolink_params_init();
    iolink_isdu_init(&ctx);

    const uint8_t req[3] = {0x93U, 0x10U, 0x82U}; /* should be 0x83 */
    wire_write_request(&ctx, req, sizeof(req));

    uint8_t resp[4];
    size_t n = wire_read_response(&ctx, resp, sizeof(resp));
    assert_int_equal(n, 4U);
    assert_int_equal(resp[0], 0xC4U);
    assert_int_equal(resp[1], 0x80U);
    assert_int_equal(resp[2], 0x00U);
    assert_int_equal(resp[3], chkpdu(resp, 3U));
}

/* A START read while the application has not answered yields Busy (0x01). */
static void test_isdu_wire_busy_polling(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    wire_device_info_init();
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* Parse the request but do not run iolink_isdu_process() yet. */
    const uint8_t req[3] = {0x93U, 0x10U, 0x83U};
    uint8_t fc = IOLINK_FLOWCTRL_START;
    for (size_t i = 0U; i < sizeof(req); i++) {
        iolink_isdu_od_write(&ctx, fc, &req[i], 1U);
        fc =
            (fc == IOLINK_FLOWCTRL_START) ? 1U : (uint8_t) ((fc + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
    }

    uint8_t octet = 0U;
    iolink_isdu_od_read(&ctx, IOLINK_FLOWCTRL_START, &octet, 1U);
    assert_int_equal(octet, 0x01U); /* Busy */

    /* Application answers; the next START read returns the response. */
    iolink_isdu_process(&ctx);
    uint8_t resp[4];
    size_t n = wire_read_response(&ctx, resp, sizeof(resp));
    assert_int_equal(n, 4U);
    assert_int_equal(resp[0], 0xD4U);
}

/* ABORT mid-request discards everything; the next read answers No Service. */
static void test_isdu_wire_abort(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    wire_device_info_init();
    iolink_params_init();
    iolink_isdu_init(&ctx);

    iolink_isdu_od_write(&ctx, IOLINK_FLOWCTRL_START, (const uint8_t[]){0x93U}, 1U);
    iolink_isdu_od_read(&ctx, IOLINK_FLOWCTRL_ABORT, NULL, 0U);

    uint8_t octet = 0U;
    iolink_isdu_od_read(&ctx, IOLINK_FLOWCTRL_START, &octet, 1U);
    assert_int_equal(octet, 0x00U);
}

/* ExtLength read of a 63-octet string: D1 42 <63 data> <CHKPDU> (Figure A.19 ex. 3: n = data+3). */
static void test_isdu_wire_extlength_read(void** state)
{
    (void) state;
    iolink_isdu_ctx_t ctx;
    static uint8_t big[64];
    static iolink_device_info_t info;
    for (uint8_t i = 0U; i < 64U; i++) {
        big[i] = (uint8_t) (0x40U + i);
    }
    big[63] = 0U; /* NUL-terminate so strlen() sees 63 characters */
    (void) memset(&info, 0, sizeof(info));
    info.vendor_name = (const char*) big;
    info.vendor_text = "v";
    info.product_name = "p";
    info.product_id = "i";
    info.product_text = "t";
    info.serial_number = "s";
    info.hardware_revision = "h";
    info.firmware_revision = "f";
    iolink_device_info_init(&info);
    iolink_params_init();
    iolink_isdu_init(&ctx);

    /* 8-bit read of Index 0x10: 0x93 0x10 0x83. */
    const uint8_t req[3] = {0x93U, 0x10U, 0x83U};
    wire_write_request(&ctx, req, sizeof(req));

    uint8_t resp[128];
    size_t n = wire_read_response(&ctx, resp, sizeof(resp));
    /* Response total = I-Service/Length octet + ExtLength + 63 data + CHKPDU = 66 (0x42). */
    assert_int_equal(n, 66U);
    assert_int_equal(resp[0], 0xD1U);
    assert_int_equal(resp[1], 0x42U);
    assert_memory_equal(&resp[2], big, 63U);
    assert_int_equal(resp[65], chkpdu(resp, 65U));
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_isdu_wire_read_figure_a20, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_wire_write_figure_a20, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_wire_16bit_count_wrap, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_wire_repeated_flowctrl, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_wire_flowctrl_error, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_wire_chkpdu_corrupt, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_wire_busy_polling, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_wire_abort, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_isdu_wire_extlength_read, test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
