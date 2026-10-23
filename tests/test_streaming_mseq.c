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

#include "iolinki/crc.h"
#include "iolinki/device.h"
#include "iolinki/protocol.h"

/* Byte-exact Annex A request/reply tests for the default streaming transport. */

typedef struct
{
    uint8_t rx[64];
    size_t rx_len;
    size_t rx_pos;
    uint8_t tx[64];
    size_t tx_len;
    bool wakeup;
    iolink_device_config_t config; /* must outlive the device context */
} test_link_t;

static int link_recv(void* user, uint8_t* byte)
{
    test_link_t* link = (test_link_t*) user;
    if (link->rx_pos == link->rx_len) {
        return 0;
    }
    *byte = link->rx[link->rx_pos++];
    return 1;
}

static int link_send(void* user, const uint8_t* data, size_t len)
{
    test_link_t* link = (test_link_t*) user;
    if (len > sizeof(link->tx)) {
        return -1;
    }
    (void) memcpy(link->tx, data, len);
    link->tx_len = len;
    return (int) len;
}

static int link_wakeup(void* user)
{
    test_link_t* link = (test_link_t*) user;
    const int result = link->wakeup ? 1 : 0;
    link->wakeup = false;
    return result;
}

static void exchange(iolink_device_ctx_t* ctx, test_link_t* link, const uint8_t* frame, size_t len)
{
    (void) memcpy(link->rx, frame, len);
    link->rx_len = len;
    link->rx_pos = 0;
    link->tx_len = 0;
    iolink_device_process(ctx);
}

static void init_device(iolink_device_ctx_t* ctx, test_link_t* link, iolink_m_seq_type_t type,
                        uint8_t pd_in, uint8_t pd_out)
{
    const iolink_phy_api_t phy = {
        .user = link,
        .send = link_send,
        .recv_byte = link_recv,
        .detect_wakeup = link_wakeup,
    };
    (void) memset(link, 0, sizeof(*link));
    link->config.phy = phy;
    link->config.stack.m_seq_type = type;
    link->config.stack.pd_in_len = pd_in;
    link->config.stack.pd_out_len = pd_out;
    link->config.stack.min_cycle_time = 1;
    assert_int_equal(iolink_device_init(ctx, &link->config), 0);
    iolink_device_set_timing_enforcement(ctx, false);
}

static void goto_estab(iolink_device_ctx_t* ctx, test_link_t* link)
{
    link->wakeup = true;
    iolink_device_process(ctx);

    uint8_t transition[2] = {IOLINK_MC_TRANSITION_COMMAND, 0U};
    transition[1] = iolink_checksum6(transition, sizeof(transition));
    exchange(ctx, link, transition, sizeof(transition));
    assert_int_equal(iolink_device_get_state(ctx), IOLINK_DLL_STATE_ESTAB_COM);
}

static void send_read_request(iolink_device_ctx_t* ctx, test_link_t* link, uint8_t type_bits,
                              uint8_t pd_out_len, uint8_t* frame, size_t* frame_len)
{
    size_t len = (size_t) IOLINK_M_SEQ_HEADER_LEN + (size_t) pd_out_len;
    (void) memset(frame, 0, len);
    frame[0] = (uint8_t) IOLINK_MC_RW_MASK; /* process channel READ */
    frame[1] = type_bits;
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, len));
    *frame_len = len;
    exchange(ctx, link, frame, len);
}

static void test_type_2_v_read_has_no_od(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    test_link_t link;
    init_device(&ctx, &link, IOLINK_M_SEQ_TYPE_2_V, 2U, 2U);
    const uint8_t input[2] = {0x11U, 0x22U};
    assert_int_equal(iolink_device_pd_input_update(&ctx, input, sizeof(input), true), 0);
    goto_estab(&ctx, &link);

    uint8_t frame[64];
    size_t len;
    send_read_request(&ctx, &link, IOLINK_MSEQ_TYPE_2, 2U, frame, &len);

    assert_int_equal(iolink_device_get_state(&ctx), IOLINK_DLL_STATE_OPERATE);
    assert_int_equal(link.tx_len, 5U); /* OD(2) + PD-in(2) + CKS */
    assert_int_equal(link.tx[0], 0U);
    assert_int_equal(link.tx[1], 0U);
    assert_int_equal(link.tx[2], input[0]);
    assert_int_equal(link.tx[3], input[1]);
}

static void test_type_2_v_write_reply_has_no_od(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    test_link_t link;
    init_device(&ctx, &link, IOLINK_M_SEQ_TYPE_2_V, 2U, 2U);
    const uint8_t input[2] = {0x33U, 0x44U};
    assert_int_equal(iolink_device_pd_input_update(&ctx, input, sizeof(input), true), 0);
    goto_estab(&ctx, &link);

    uint8_t frame[64] = {0U};
    const size_t len = 2U + 2U + 2U; /* MC + CKT + PD-out(2) + OD(2) */
    frame[0] = 0U;                   /* process channel WRITE */
    frame[1] = IOLINK_MSEQ_TYPE_2;
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, len));
    exchange(&ctx, &link, frame, len);

    assert_int_equal(iolink_device_get_state(&ctx), IOLINK_DLL_STATE_OPERATE);
    assert_int_equal(link.tx_len, 3U); /* PD-in(2) + CKS; no OD */
    assert_int_equal(link.tx[0], input[0]);
    assert_int_equal(link.tx[1], input[1]);
}

static void test_type_1_2_read_has_no_od(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    test_link_t link;
    init_device(&ctx, &link, IOLINK_M_SEQ_TYPE_1_2, 0U, 0U);
    goto_estab(&ctx, &link);

    uint8_t frame[64];
    size_t len;
    send_read_request(&ctx, &link, IOLINK_MSEQ_TYPE_1, 0U, frame, &len);

    assert_int_equal(iolink_device_get_state(&ctx), IOLINK_DLL_STATE_OPERATE);
    assert_int_equal(link.tx_len, 3U); /* OD(2) + CKS */
}

static void test_type_1_v_read_has_eight_od_octets(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    test_link_t link;
    init_device(&ctx, &link, IOLINK_M_SEQ_TYPE_1_V, 0U, 0U);
    goto_estab(&ctx, &link);

    uint8_t frame[64];
    size_t len;
    send_read_request(&ctx, &link, IOLINK_MSEQ_TYPE_1, 0U, frame, &len);

    assert_int_equal(iolink_device_get_state(&ctx), IOLINK_DLL_STATE_OPERATE);
    assert_int_equal(link.tx_len, 9U); /* OD(8) + CKS */
}

static void test_type_2_1_read_has_no_od(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    test_link_t link;
    init_device(&ctx, &link, IOLINK_M_SEQ_TYPE_2_1, 1U, 0U);
    const uint8_t input[1] = {0x5AU};
    assert_int_equal(iolink_device_pd_input_update(&ctx, input, sizeof(input), true), 0);
    goto_estab(&ctx, &link);

    uint8_t frame[64];
    size_t len;
    send_read_request(&ctx, &link, IOLINK_MSEQ_TYPE_2, 0U, frame, &len);

    assert_int_equal(iolink_device_get_state(&ctx), IOLINK_DLL_STATE_OPERATE);
    assert_int_equal(link.tx_len, 3U); /* OD(1) + PD-in(1) + CKS */
    assert_int_equal(link.tx[0], 0U);
    assert_int_equal(link.tx[1], input[0]);
}

static void test_type_0_fallback_write_in_operate(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    test_link_t link;
    init_device(&ctx, &link, IOLINK_M_SEQ_TYPE_2_V, 2U, 2U);
    goto_estab(&ctx, &link);

    uint8_t enter[64] = {0U};
    const size_t enter_len = 2U + 2U + 2U;
    enter[1] = IOLINK_MSEQ_TYPE_2;
    enter[1] = (uint8_t) (enter[1] | iolink_checksum6(enter, enter_len));
    exchange(&ctx, &link, enter, enter_len);
    assert_int_equal(iolink_device_get_state(&ctx), IOLINK_DLL_STATE_OPERATE);

    uint8_t fallback[3] = {0x20U, 0U, IOLINK_CMD_FALLBACK};
    fallback[1] = iolink_checksum6(fallback, sizeof(fallback));
    exchange(&ctx, &link, fallback, sizeof(fallback));

    assert_int_equal(iolink_device_get_state(&ctx), IOLINK_DLL_STATE_FALLBACK);
    assert_int_equal(link.tx_len, 1U); /* Type-0 write reply is the CKS only */
}

static void test_type_0_page_read_in_operate(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    test_link_t link;
    init_device(&ctx, &link, IOLINK_M_SEQ_TYPE_2_V, 2U, 2U);
    goto_estab(&ctx, &link);

    uint8_t enter[64] = {0U};
    const size_t enter_len = 2U + 2U + 2U;
    enter[1] = IOLINK_MSEQ_TYPE_2;
    enter[1] = (uint8_t) (enter[1] | iolink_checksum6(enter, enter_len));
    exchange(&ctx, &link, enter, enter_len);
    assert_int_equal(iolink_device_get_state(&ctx), IOLINK_DLL_STATE_OPERATE);

    uint8_t read[2] = {0xA2U, 0U}; /* Type-0 page READ, MinCycleTime address */
    read[1] = iolink_checksum6(read, sizeof(read));
    exchange(&ctx, &link, read, sizeof(read));

    assert_int_equal(iolink_device_get_state(&ctx), IOLINK_DLL_STATE_OPERATE);
    assert_int_equal(link.tx_len, 2U); /* OD + CKS */
}

static void test_type_1_2_write_carries_two_od_octets(void** state)
{
    (void) state;
    iolink_device_ctx_t ctx;
    test_link_t link;
    init_device(&ctx, &link, IOLINK_M_SEQ_TYPE_1_2, 0U, 0U);
    goto_estab(&ctx, &link);

    uint8_t frame[4] = {0U, IOLINK_MSEQ_TYPE_1, 0x00U, 0x00U};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, sizeof(frame)));
    exchange(&ctx, &link, frame, sizeof(frame));

    assert_int_equal(iolink_device_get_state(&ctx), IOLINK_DLL_STATE_OPERATE);
    assert_int_equal(link.tx_len, 1U);         /* WRITE reply: CKS only */
    assert_int_equal(ctx.dll.frame_index, 0U); /* all four octets consumed */
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_type_2_v_read_has_no_od),
        cmocka_unit_test(test_type_2_v_write_reply_has_no_od),
        cmocka_unit_test(test_type_1_2_read_has_no_od),
        cmocka_unit_test(test_type_1_v_read_has_eight_od_octets),
        cmocka_unit_test(test_type_2_1_read_has_no_od),
        cmocka_unit_test(test_type_0_fallback_write_in_operate),
        cmocka_unit_test(test_type_0_page_read_in_operate),
        cmocka_unit_test(test_type_1_2_write_carries_two_od_octets),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
