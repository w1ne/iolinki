/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file test_dll.c
 * @brief Unit tests for Data Link Layer (DLL) state machine
 */

#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "iolinki/device.h"
#include "iolinki/dll.h"
#include "iolinki/crc.h"
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

static void test_dll_wakeup_to_preoperate(void** state)
{
    (void) state;
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, NULL, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, true);

    /* Trigger wake-up */
    iolink_phy_mock_set_wakeup(1);
    iolink_device_process(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_AWAITING_COMM);

    /* Wait for t_dwu to expire */
    usleep(200);

    /* Send valid Type 0 frame (idle) to enter PREOPERATE */
    uint8_t mc = 0x00;
    uint8_t ck = test_frame_checksum(mc);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, mc);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, ck);
    will_return(mock_phy_recv_byte, 0);

    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1); /* Type-0 write replies CKS only */
    will_return(mock_phy_send, 0);

    iolink_device_process(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);
}

static void test_dll_preoperate_to_operate(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_1_1, .pd_in_len = 1, .pd_out_len = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, true);

    /* Wake-up */
    iolink_phy_mock_set_wakeup(1);
    iolink_device_process(&dev.ctx);
    usleep(200);

    /* PREOPERATE -> ESTAB_COM */
    uint8_t trans_mc = IOLINK_MC_TRANSITION_COMMAND;
    uint8_t trans_ck = test_frame_checksum(trans_mc);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, trans_mc);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, trans_ck);
    will_return(mock_phy_recv_byte, 0);
    /* No response for Transition Command in PREOPERATE */
    iolink_device_process(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_ESTAB_COM);

    /* ESTAB_COM -> OPERATE on first valid frame */
    uint8_t frame[4] = {0x80, IOLINK_MSEQ_TYPE_1, 0x00, 0x00};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 4));
    for (int i = 0; i < 4; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);

    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 3);
    will_return(mock_phy_send, 0);

    iolink_device_process(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
}

static void test_dll_fallback_on_crc_errors(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_1_1, .pd_in_len = 1, .pd_out_len = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, true);

    /* Wake-up */
    iolink_phy_mock_set_wakeup(1);
    iolink_device_process(&dev.ctx);
    usleep(200);

    /* PREOPERATE -> ESTAB_COM */
    uint8_t mc = IOLINK_MC_TRANSITION_COMMAND;
    uint8_t ck = test_frame_checksum(mc);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, mc);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, ck);
    will_return(mock_phy_recv_byte, 0);
    /* No response for Transition Command */
    iolink_device_process(&dev.ctx);

    /* ESTAB_COM -> OPERATE */
    uint8_t ok_frame[4] = {0x80, IOLINK_MSEQ_TYPE_1, 0x00, 0x00};
    ok_frame[1] = (uint8_t) (ok_frame[1] | iolink_checksum6(ok_frame, 4));
    for (int i = 0; i < 4; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, ok_frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 3);
    will_return(mock_phy_send, 0);
    iolink_device_process(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* Inject CRC errors to trigger fallback */
    for (int r = 0; r < 3; r++) {
        uint8_t bad_frame[4] = {0x80, (uint8_t) (IOLINK_MSEQ_TYPE_1 | 0x3FU), 0x00, 0x00};
        for (int i = 0; i < 4; i++) {
            will_return(mock_phy_recv_byte, 1);
            will_return(mock_phy_recv_byte, bad_frame[i]);
        }
        will_return(mock_phy_recv_byte, 0);
        iolink_device_process(&dev.ctx);
    }

    /* Ensure there is exactly one mock value for the final process cycle check */
    /* will_return(mock_phy_recv_byte, 0); // Removed: in SIO mode we don't call recv_byte */

    /* Next process call applies fallback */
    iolink_device_process(&dev.ctx);

    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_STARTUP);
    assert_int_equal(iolink_device_get_baudrate(&dev.ctx), IOLINK_BAUDRATE_COM1);
}

static void test_dll_reject_transition_in_operate(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_1_1, .pd_in_len = 1, .pd_out_len = 1};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    move_to_operate_ctx(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* Master sends 0x0F (Transition) while in OPERATE
     * Type 1_1 frame for pd_out_len=1 is 5 bytes: MC, CKT, PD, OD, CK */
    uint8_t mc = IOLINK_MC_TRANSITION_COMMAND;
    uint8_t frame[4] = {mc, IOLINK_MSEQ_TYPE_1, 0x00, 0x00};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 4));

    for (int i = 0; i < 4; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);

    iolink_device_process(&dev.ctx);

    /* Should NOT change state and should increment framing_errors */
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
    iolink_dll_stats_t stats;
    iolink_device_get_dll_stats(&dev.ctx, &stats);
    assert_int_not_equal(stats.framing_errors, 0);
}

/* Send one valid TYPE_1_1 read frame and expect the [OD][CKS] reply. */
static void type1_1_read_frame(iolink_device_ctx_t* ctx)
{
    /* TYPE_1_1 keeps the legacy interleaved layout: header + PD/OD octet. */
    uint8_t frame[3] = {IOLINK_MC_RW_MASK, IOLINK_MSEQ_TYPE_1, 0x00U};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, sizeof(frame)));
    for (size_t i = 0; i < sizeof(frame); i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);

    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2); /* OD(1) + CKS */
    will_return(mock_phy_send, 0);
    iolink_device_process(ctx);
}

/* 10.7.5: after the ISDU response of Back-to-box, the Device waits three
 * Master cycles and then stops and disables communication. The hook lives in
 * the shared response path, so a TYPE_1_1 link (the default transport for
 * ISDU) must arm the countdown too. */
static void test_dll_back_to_box_stops_after_three_cycles(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_1_1, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);
    move_to_operate_ctx(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* The SystemCommand handler has queued the stop; the next response arms it. */
    dev.ctx.dll.isdu.back_to_box_pending = true;
    type1_1_read_frame(&dev.ctx);
    assert_true(dev.ctx.dll.back_to_box_armed);
    assert_int_equal(dev.ctx.dll.back_to_box_cycles, 3);
    assert_false(dev.ctx.dll.communication_disabled);

    /* Two further Master cycles are still answered: count the actual replies,
       not only the countdown field. */
    const uint32_t sends = iolink_phy_mock_get_send_count();
    type1_1_read_frame(&dev.ctx);
    assert_int_equal(iolink_phy_mock_get_send_count(), sends + 1U);
    assert_int_equal(dev.ctx.dll.back_to_box_cycles, 2);
    type1_1_read_frame(&dev.ctx);
    assert_int_equal(iolink_phy_mock_get_send_count(), sends + 2U);
    assert_int_equal(dev.ctx.dll.back_to_box_cycles, 1);

    /* The third cycle stops communication; no response is sent. */
    uint8_t frame[3] = {IOLINK_MC_RW_MASK, IOLINK_MSEQ_TYPE_1, 0x00U};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, sizeof(frame)));
    for (size_t i = 0; i < sizeof(frame); i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    /* The Device returns as soon as the last cycle is consumed, before it asks
       for another octet, so no terminator is queued here. */
    iolink_device_process(&dev.ctx);

    assert_true(dev.ctx.dll.communication_disabled);
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_INACTIVE);
}

/* Send one Type-1_1 ISDU frame (legacy interleaved layout) and expect a reply
 * of @p reply_len octets (1 = CKS only for a write, 2 = OD + CKS for a read). */
static void type1_1_isdu_frame(iolink_device_ctx_t* ctx, uint8_t mc, uint8_t od, uint8_t reply_len)
{
    uint8_t frame[3] = {mc, IOLINK_MSEQ_TYPE_1, od};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, sizeof(frame)));
    for (size_t i = 0; i < sizeof(frame); i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);

    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, reply_len);
    will_return(mock_phy_send, 0);
    iolink_device_process(ctx);
}

/* 10.7.5 end to end over the real ISDU path: the countdown must not start
 * before the last octet of the 0x83 positive response has been read out. */
static void test_dll_back_to_box_arms_after_isdu_response(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_1_1, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);
    move_to_operate_ctx(&dev.ctx);

    /* Back-to-box write request: I-Service 0x14, index 0x0002, data 0x83. */
    const uint8_t request[4] = {0x14U, 0x02U, 0x83U, (uint8_t) (0x14U ^ 0x02U ^ 0x83U)};
    type1_1_isdu_frame(&dev.ctx, (uint8_t) (IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_START),
                       request[0], 1U);
    for (uint8_t i = 1U; i < sizeof(request); i++) {
        type1_1_isdu_frame(&dev.ctx, (uint8_t) (IOLINK_MC_CHANNEL_ISDU | i), request[i], 1U);
    }

    /* Execute the collected request. */
    will_return(mock_phy_recv_byte, 0);
    iolink_device_process(&dev.ctx);
    assert_true(dev.ctx.dll.isdu.back_to_box_pending);
    assert_false(dev.ctx.dll.back_to_box_armed);

    /* First response octet: the countdown must still wait. */
    uint8_t read_mc =
        (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_START);
    type1_1_isdu_frame(&dev.ctx, read_mc, 0x00U, 2U);
    assert_false(dev.ctx.dll.back_to_box_armed);

    /* Last response octet: now the response is complete and the countdown
       starts. */
    type1_1_isdu_frame(&dev.ctx, (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | 0x01U),
                       0x00U, 2U);
    assert_true(dev.ctx.dll.back_to_box_armed);
    assert_false(dev.ctx.dll.isdu.back_to_box_pending);
}

/* A Master that stops polling must not keep the Device communicating: the
 * bounded wait ends the countdown as well. */
static void test_dll_back_to_box_deadline_stops_without_frames(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_1_1, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    assert_int_equal(iolink_test_device_init(&dev, &config, NULL), 0);
    move_to_operate_ctx(&dev.ctx);

    dev.ctx.dll.back_to_box_armed = true;
    dev.ctx.dll.back_to_box_cycles = 3U;
    dev.ctx.dll.back_to_box_deadline_us = 1U; /* already elapsed */
    iolink_device_process(&dev.ctx);

    assert_true(dev.ctx.dll.communication_disabled);
    assert_int_equal(iolink_device_get_phy_mode(&dev.ctx), IOLINK_PHY_MODE_INACTIVE);
}

/* Bring a Type-0 device to OPERATE: wake, PREOPERATE, transition command,
   then a valid Type-0 idle frame. */
static void move_type0_to_operate(iolink_device_ctx_t* ctx)
{
    iolink_phy_mock_set_wakeup(1);
    iolink_device_process(ctx);
    usleep(200);

    uint8_t mc = IOLINK_MC_TRANSITION_COMMAND;
    uint8_t ck = test_frame_checksum(mc);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, mc);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, ck);
    will_return(mock_phy_recv_byte, 0);
    iolink_device_process(ctx);

    uint8_t idle = 0x00;
    uint8_t idle_ck = test_frame_checksum(idle);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, idle);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, idle_ck);
    will_return(mock_phy_recv_byte, 0);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1); /* Type-0 write replies CKS only */
    will_return(mock_phy_send, 0);
    iolink_device_process(ctx);
}

/* C5/Table 47 T11: a CKT type that does not match the configured M-sequence
   type in OPERATE is an illegal M-sequence and returns the device to STARTUP. */
static void test_dll_illegal_mseq_type(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    move_type0_to_operate(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    /* Type-0 device receives a Type-1 CKT. */
    uint8_t frame[2] = {0x00, IOLINK_MSEQ_TYPE_1};
    frame[1] = (uint8_t) (frame[1] | iolink_checksum6(frame, 2));
    for (int i = 0; i < 2; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);
    iolink_device_process(&dev.ctx);

    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_STARTUP);
    iolink_dll_stats_t stats;
    iolink_device_get_dll_stats(&dev.ctx, &stats);
    assert_int_not_equal(stats.framing_errors, 0);
}

/* C2/7.3.5.3: a read on the ISDU channel with FlowCTRL IDLE is no request for
   transmission; the device answers No Service (0x00) and it is not an error. */
static void test_dll_isdu_channel_idle(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    move_type0_to_operate(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);

    iolink_dll_stats_t before;
    iolink_device_get_dll_stats(&dev.ctx, &before);

    /* MC = READ | ISDU channel | FlowCTRL IDLE (0x11). */
    uint8_t mc = (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_ISDU | IOLINK_FLOWCTRL_IDLE);
    uint8_t frame[2] = {mc, 0x00};
    frame[1] = iolink_checksum6(frame, 1);
    for (int i = 0; i < 2; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);

    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2);
    will_return(mock_phy_send, 0);
    iolink_device_process(&dev.ctx);

    iolink_dll_stats_t after;
    iolink_device_get_dll_stats(&dev.ctx, &after);
    assert_int_equal(after.framing_errors, before.framing_errors);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
}

/* C2/C4: a read on the diagnosis channel address 0 is served (StatusCode) and
   is not an error. */
static void test_dll_diagnosis_channel(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    move_type0_to_operate(&dev.ctx);

    iolink_dll_stats_t before;
    iolink_device_get_dll_stats(&dev.ctx, &before);

    /* MC = READ | diagnosis channel | address 0. */
    uint8_t mc = (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_DIAGNOSIS);
    uint8_t frame[2] = {mc, 0x00};
    frame[1] = iolink_checksum6(frame, 1);
    for (int i = 0; i < 2; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);

    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2);
    will_return(mock_phy_send, 0);
    iolink_device_process(&dev.ctx);

    iolink_dll_stats_t after;
    iolink_device_get_dll_stats(&dev.ctx, &after);
    assert_int_equal(after.framing_errors, before.framing_errors);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_OPERATE);
}

/* C4/7.3.8.2: the reply CKS Event bit (bit 7) follows the event flag, and a
   diagnosis read returns the Table 58 StatusCode. */
static void test_dll_event_flag_in_cks(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_0, .pd_in_len = 0, .pd_out_len = 0};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);

    move_type0_to_operate(&dev.ctx);

    /* Trigger an error event: flag set, memory slot 1 populated. */
    iolink_event_trigger(iolink_device_get_events_ctx(&dev.ctx), 0x1801U, IOLINK_EVENT_TYPE_ERROR);

    /* MC = READ | diagnosis channel | address 0. Reply [StatusCode][CKS]. */
    uint8_t mc = (uint8_t) (IOLINK_MC_RW_MASK | IOLINK_MC_CHANNEL_DIAGNOSIS);
    uint8_t frame[2] = {mc, 0x00};
    frame[1] = iolink_checksum6(frame, 1);
    for (int i = 0; i < 2; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);

    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2);
    will_return(mock_phy_send, 0);
    iolink_device_process(&dev.ctx);

    /* Clean channel: flag stays set until the StatusCode write confirmation. */
    uint8_t mc_write = (uint8_t) (IOLINK_MC_CHANNEL_DIAGNOSIS); /* WRITE, addr 0 */
    uint8_t wframe[3] = {mc_write, 0x00, 0x00};
    wframe[1] = iolink_checksum6(wframe, 2);
    for (int i = 0; i < 3; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, wframe[i]);
    }
    will_return(mock_phy_recv_byte, 0);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1); /* Type-0 write replies CKS only */
    will_return(mock_phy_send, 0);
    iolink_device_process(&dev.ctx);

    assert_false(iolink_events_flag(iolink_device_get_events_ctx(&dev.ctx)));
}

/* Regression: after OPERATE with PD widths configured, a re-startup followed by
   a 3-octet Type-0 ISDU write in PREOPERATE must be parsed as a Type-0 frame
   (OD at offset 2) and answered with the CKS only, not with the OPERATE PD
   widths (which shifted the byte stream and broke ISDU after recovery). */
static void test_dll_preoperate_type0_od_write_ignores_operate_widths(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_1_2, .pd_in_len = 2, .pd_out_len = 2};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, true);

    iolink_phy_mock_set_wakeup(1);
    iolink_device_process(&dev.ctx);
    usleep(200);

    /* Startup probe: Type-0 READ of MinCycleTime (MC 0xA2) -> PREOPERATE. */
    uint8_t probe[2] = {0xA2U, 0x00U};
    probe[1] = (uint8_t) (probe[1] | iolink_checksum6(probe, 2U));
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, probe[0]);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, probe[1]);
    will_return(mock_phy_recv_byte, 0);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2); /* OD + CKS */
    will_return(mock_phy_send, 0);
    iolink_device_process(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);

    /* ISDU write START in PREOPERATE: MC 0x70 (W, ISDU, START), CKT, OD 0xB5. */
    uint8_t req[3] = {0x70U, 0x00U, 0xB5U};
    req[1] = (uint8_t) (req[1] | iolink_checksum6(req, 3U));
    for (size_t i = 0U; i < 3U; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, req[i]);
    }
    will_return(mock_phy_recv_byte, 0);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1); /* Figure A.5: Type-0 write -> CKS only */
    will_return(mock_phy_send, 0);
    iolink_device_process(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);
}

/* Regression: with a 2-octet-OD M-sequence configured (TYPE_2_V, OPERATE code
   5), the Type-0 DeviceOperate write is still a 3-octet frame (Figure A.5) and
   must be answered with the CKS; a 4-octet expectation left the frame incomplete
   and the device silent, so a real master never saw the ack. */
static void test_dll_type0_write_is_three_octets_with_two_octet_od(void** state)
{
    (void) state;
    iolink_config_t config = {.m_seq_type = IOLINK_M_SEQ_TYPE_2_V, .pd_in_len = 2, .pd_out_len = 2};
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, &config, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, true);

    iolink_phy_mock_set_wakeup(1);
    iolink_device_process(&dev.ctx);
    usleep(200);

    uint8_t probe[2] = {0xA2U, 0x00U};
    probe[1] = (uint8_t) (probe[1] | iolink_checksum6(probe, 2U));
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, probe[0]);
    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, probe[1]);
    will_return(mock_phy_recv_byte, 0);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2);
    will_return(mock_phy_send, 0);
    iolink_device_process(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);

    /* DeviceOperate: MC 0x20, CKT, OD 0x99 -> CKS-only reply, ESTAB_COM. */
    uint8_t op[3] = {0x20U, 0x00U, IOLINK_CMD_DEVICE_OPERATE};
    op[1] = (uint8_t) (op[1] | iolink_checksum6(op, 3U));
    for (size_t i = 0U; i < 3U; i++) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, op[i]);
    }
    will_return(mock_phy_recv_byte, 0);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 1);
    will_return(mock_phy_send, 0);
    iolink_device_process(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_ESTAB_COM);
}
static void test_dll_receive_error_discards_partial_frame(void** state)
{
    (void) state;
    setup_mock_phy();
    will_return(mock_phy_init, 0);
    iolink_test_device_t dev;
    iolink_test_device_init(&dev, NULL, NULL);
    iolink_device_set_timing_enforcement(&dev.ctx, false);
    iolink_phy_mock_set_wakeup(1);
    iolink_device_process(&dev.ctx);

    will_return(mock_phy_recv_byte, 1);
    will_return(mock_phy_recv_byte, 0xA2U);
    will_return(mock_phy_recv_byte, -1);
    iolink_device_process(&dev.ctx);
    assert_int_equal(dev.ctx.dll.frame_index, 0);
    assert_int_equal(dev.ctx.dll.framing_errors, 1);

    uint8_t frame[2] = {0xA2U, 0};
    frame[1] = iolink_checksum6(frame, sizeof(frame));
    for (size_t i = 0; i < sizeof(frame); ++i) {
        will_return(mock_phy_recv_byte, 1);
        will_return(mock_phy_recv_byte, frame[i]);
    }
    will_return(mock_phy_recv_byte, 0);
    expect_any(mock_phy_send, data);
    expect_value(mock_phy_send, len, 2);
    will_return(mock_phy_send, 0);
    iolink_device_process(&dev.ctx);
    assert_int_equal(iolink_device_get_state(&dev.ctx), IOLINK_DLL_STATE_PREOPERATE);
    assert_int_equal(dev.ctx.dll.frame_index, 0);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(test_dll_receive_error_discards_partial_frame, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_wakeup_to_preoperate, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_preoperate_to_operate, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_fallback_on_crc_errors, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_reject_transition_in_operate, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_illegal_mseq_type, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_isdu_channel_idle, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_diagnosis_channel, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_event_flag_in_cks, test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_preoperate_type0_od_write_ignores_operate_widths,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_type0_write_is_three_octets_with_two_octet_od,
                                        test_setup, test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_back_to_box_stops_after_three_cycles, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_back_to_box_arms_after_isdu_response, test_setup,
                                        test_teardown),
        cmocka_unit_test_setup_teardown(test_dll_back_to_box_deadline_stops_without_frames,
                                        test_setup, test_teardown),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
