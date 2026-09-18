/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/dll.h"
#include "iolinki/crc.h"
#include "iolinki/iolink.h"
#include "iolinki/protocol.h"
#include "iolinki/time_utils.h"
#include "iolinki/utils.h"
#include <string.h>
#include <stdio.h>

/**
 * @file dll.c
 * @brief Data Link Layer (DLL): M-sequence state machine and framing.
 * @ingroup iolinki_dll
 *
 * Drives the IO-Link device DLL: wake-up detection, mode/baudrate management,
 * per-frame reception and CRC validation, the STARTUP -> PREOPERATE ->
 * OPERATE state machine, timing enforcement, fallback handling, and dispatch
 * of Type-0/Type-1/Type-2 M-sequences into the ISDU engine.
 */

#define DLL_LOG(...)

/**
 * @brief Centralised state transition.
 *
 * Updates the state and notifies the optional state-change hook on an actual
 * change, so no transition is ever missed.
 */
static void dll_set_state(iolink_dll_ctx_t* ctx, iolink_dll_state_t new_state)
{
    if (ctx->state != new_state) {
        ctx->state = new_state;
        if (ctx->state_cb != NULL) {
            ctx->state_cb(ctx->state_cb_user, new_state);
        }
    }
}

/** @brief Return the CKT M-sequence type bits (A.1.3, Table A.3) for a configured type. */
static uint8_t dll_mseq_type_bits(uint8_t m_seq_type)
{
    switch (m_seq_type) {
        case IOLINK_M_SEQ_TYPE_1_1:
        case IOLINK_M_SEQ_TYPE_1_2:
        case IOLINK_M_SEQ_TYPE_1_V:
            return IOLINK_MSEQ_TYPE_1;
        case IOLINK_M_SEQ_TYPE_2_1:
        case IOLINK_M_SEQ_TYPE_2_2:
        case IOLINK_M_SEQ_TYPE_2_V:
            return IOLINK_MSEQ_TYPE_2;
        default:
            return IOLINK_MSEQ_TYPE_0;
    }
}

/**
 * @brief Return the CKT type bits the device expects for the current state.
 *
 * In STARTUP and PREOPERATE every message is Type 0 (Table A.3, CKT bits 00);
 * in OPERATE the configured M-sequence type is expected.
 */
static uint8_t dll_expected_type_bits(const iolink_dll_ctx_t* ctx)
{
    if ((ctx->state == IOLINK_DLL_STATE_OPERATE) || (ctx->state == IOLINK_DLL_STATE_ESTAB_COM)) {
        return dll_mseq_type_bits(ctx->m_seq_type);
    }
    return IOLINK_MSEQ_TYPE_0;
}

/**
 * @brief Return the expected request length for the given M-sequence type bits.
 *
 * The request is MC + CKT + PD-out width + OD width; the checksum is carried in
 * the CKT octet (A.1.6), so there is no trailing checksum octet. A Type-0
 * message carries no PD and a single OD octet written only on a write access.
 */
static uint8_t dll_expected_req_len(const iolink_dll_ctx_t* ctx, uint8_t type_bits)
{
    if (type_bits == IOLINK_MSEQ_TYPE_0) {
        return 2U;
    }
    return (uint8_t) (IOLINK_M_SEQ_HEADER_LEN + ctx->pd_out_len_current + ctx->od_len);
}

/** @brief Return the response-time (t_REN) limit in us (C6/Table 10: 500 us). */
static uint32_t dll_get_t_ren_limit_us(const iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return 0U;
    }
    if (ctx->t_ren_override) {
        return ctx->t_ren_limit_us;
    }
    /* Table 10: a single T_REN value at most 500 us, independent of baudrate. */
    return IOLINK_T_REN_US;
}

/** @brief Return the inter-byte timeout (16 bit-times) in us for the active baudrate. */
static uint32_t dll_get_t_byte_limit_us(const iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return 0U;
    }
    uint32_t t_bit_us;
    switch (ctx->baudrate) {
        case IOLINK_BAUDRATE_COM1:
            t_bit_us = 208U;
            break;
        case IOLINK_BAUDRATE_COM2:
            t_bit_us = 26U;
            break;
        case IOLINK_BAUDRATE_COM3:
            t_bit_us = 4U;
            break;
        default:
            t_bit_us = 26U;
            break;
    }
    return t_bit_us * 16U;
}

/** @brief Return true while the power-down/PD guard window (t_PD) is still active. */
static bool dll_t_pd_active(const iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->t_pd_deadline_us == 0U)) {
        return false;
    }
    return iolink_time_get_us() < ctx->t_pd_deadline_us;
}

/** @brief Drain all pending RX bytes from the PHY; returns true if any were seen. */
static bool dll_drain_rx(iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->phy == NULL) || (ctx->phy->recv_byte == NULL)) {
        return false;
    }
    bool saw_byte = false;
    uint8_t byte = 0U;
    while (ctx->phy->recv_byte(ctx->phy->user, &byte) > 0) {
        saw_byte = true;
    }
    return saw_byte;
}

/** @brief Handle a communication failure: count retries and revert toward SIO/STARTUP. */
static void dll_enter_fallback(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }

    ctx->fallback_count++;
    ctx->total_retries++;

    if (ctx->fallback_count >= ctx->sio_fallback_threshold) {
        /* Communication could not be sustained: enter FALLBACK, revert the PHY
           to SIO at COM1, then settle back to STARTUP to await a new wake-up. */
        dll_set_state(ctx, IOLINK_DLL_STATE_FALLBACK);
        iolink_dll_set_sio_mode(ctx);
        iolink_dll_set_baudrate(ctx, IOLINK_BAUDRATE_COM1);
        dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
        ctx->fallback_count = 0U;
        ctx->frame_index = 0U;
        iolink_event_trigger(&ctx->events, IOLINK_EVENT_CODE_COMM_ERR_FRAMING,
                             IOLINK_EVENT_TYPE_WARNING);
    }
    else if (ctx->state != IOLINK_DLL_STATE_OPERATE && ctx->state != IOLINK_DLL_STATE_ESTAB_COM) {
        iolink_dll_set_baudrate(ctx, IOLINK_BAUDRATE_COM1);
        dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
    }
}

/** @brief True for a page-channel Type-0 WRITE of MasterCommand FALLBACK (Table B.2). */
static bool dll_is_fallback_command(const iolink_dll_ctx_t* ctx)
{
    uint8_t mc = ctx->frame_buf[0];
    return ((mc & IOLINK_MC_RW_MASK) == 0U) &&
           ((mc & IOLINK_MC_COMM_CHANNEL_MASK) == IOLINK_MC_CHANNEL_PAGE) &&
           ((mc & IOLINK_MC_ADDR_MASK) == 0x00U) && (ctx->frame_buf[2] == IOLINK_CMD_FALLBACK);
}

/**
 * @brief Handle a FALLBACK MasterCommand (0x5A, Table B.2) from the master.
 *
 * The device shall switch to SIO after 3 MasterCycleTimes and within at most
 * 500 ms (T_FBD, Table 43). The deadline is armed here and enforced in
 * iolink_dll_process(); the reply is a Type-0 write CKS-only response. */
static void dll_handle_fallback_command(iolink_dll_ctx_t* ctx)
{
    uint32_t t_fbd_ms = 500U;
    if (ctx->min_cycle_time_us > 0U) {
        t_fbd_ms = (3U * ctx->min_cycle_time_us) / 1000U;
        if (t_fbd_ms > 500U) {
            t_fbd_ms = 500U;
        }
    }
    ctx->fallback_deadline_ms = iolink_time_get_ms() + t_fbd_ms;
    dll_set_state(ctx, IOLINK_DLL_STATE_FALLBACK);

    uint8_t resp[1];
    resp[0] = 0x00U;
    resp[0] = iolink_checksum6(resp, 1U);
    if (ctx->phy->send != NULL) {
        ctx->phy->send(ctx->phy->user, resp, 1);
    }
    ctx->last_response_us = iolink_time_get_us();
}

/** @brief Handle a PREOPERATE master command: advance to ESTAB_COM on the transition command. */
static void dll_handle_preoperate(iolink_dll_ctx_t* ctx, uint8_t mc, uint8_t ck)
{
    (void) ck;
    if (mc == IOLINK_MC_TRANSITION_COMMAND) {
        dll_set_state(ctx, IOLINK_DLL_STATE_ESTAB_COM);
        ctx->fallback_count = 0U;
        /* No response to transition command per spec */
    }
}

/**
 * @brief Answer a startup Type-0 read on the page communication channel.
 *
 * Spec startup transition T1: the master reads a Direct Parameter page octet
 * (address in the MC address field, e.g. 0x02 = MinCycleTime). Reply with a
 * 2-octet Type-0 frame carrying that octet, rather than feeding the MC into the
 * ISDU engine.
 */
static void dll_handle_page_channel_read(iolink_dll_ctx_t* ctx, uint8_t mc)
{
    uint8_t resp[2];
    resp[0] =
        iolink_isdu_direct_param_page1_octet(&ctx->isdu, (uint8_t) (mc & IOLINK_MC_ADDR_MASK));
    resp[1] = 0x00U;
    resp[1] = iolink_checksum6(resp, 2U);
    if (ctx->phy->send != NULL) {
        ctx->phy->send(ctx->phy->user, resp, 2);
    }
    ctx->last_response_us = iolink_time_get_us();
}

/**
 * @brief Dispatch one OD message by its communication channel (C2, 7.3.5.3).
 *
 * Diagnosis channel: event memory (Table 58). ISDU channel: the ISDU
 * transport. The page and process channels are handled by the callers (page by
 * the startup probe and the legacy ISDU path until Task 4). Returns true when
 * the channel was handled here.
 */
static bool dll_dispatch_od(iolink_dll_ctx_t* ctx, uint8_t mc, const uint8_t* od_in, uint8_t od_len,
                            uint8_t* od_out)
{
    const uint8_t channel = (uint8_t) (mc & IOLINK_MC_COMM_CHANNEL_MASK);

    if (channel == IOLINK_MC_CHANNEL_PAGE) {
        /* 7.3.5.3: Direct Parameter page read/write. A read returns the page
           octet at the address; unimplemented addresses return 0, writes are
           ignored (A.1.2). MasterCommand at address 0 is handled by the
           PREOPERATE branch. */
        if (((mc & IOLINK_MC_RW_MASK) != 0U) && (od_len > 0U)) {
            od_out[0] = iolink_isdu_direct_param_page1_octet(&ctx->isdu,
                                                             (uint8_t) (mc & IOLINK_MC_ADDR_MASK));
        }
        return true;
    }

    if (channel == IOLINK_MC_CHANNEL_DIAGNOSIS) {
        /* 7.3.8 Table 58: event memory is served by the diagnosis channel.
           A read returns the memory octet(s) at the address; a write to
           address 0 confirms the event readout and clears the Event flag. */
        const uint8_t addr = (uint8_t) (mc & IOLINK_MC_ADDR_MASK);
        if ((mc & IOLINK_MC_RW_MASK) != 0U) {
            for (uint8_t i = 0U; i < od_len; i++) {
                od_out[i] = iolink_events_memory_read(&ctx->events, (uint8_t) (addr + i));
            }
        }
        else if (od_len > 0U) {
            iolink_events_memory_write(&ctx->events, addr, od_in[0]);
        }
        return true;
    }

    if (channel == IOLINK_MC_CHANNEL_ISDU) {
        /* C3/Table 52: FlowCTRL lives in the MC address bits. A write carries
           request octets; a read fetches the framed response (or Busy). */
        const uint8_t flowctrl = (uint8_t) (mc & IOLINK_MC_ADDR_MASK);
        if ((mc & IOLINK_MC_RW_MASK) != 0U) {
            iolink_isdu_od_read(&ctx->isdu, flowctrl, od_out, od_len);
        }
        else {
            iolink_isdu_od_write(&ctx->isdu, flowctrl, od_in, od_len);
        }
        return true;
    }

    (void) od_in;
    (void) ctx;
    return false;
}

/** @brief Process a 2-octet Type-0 request through ISDU and send the OD response. */
static void dll_handle_operate_type0(iolink_dll_ctx_t* ctx, uint8_t mc, uint8_t cks)
{
    (void) cks;
    uint8_t od_resp = 0U;
    (void) dll_dispatch_od(ctx, mc, &cks, 1U, &od_resp);

    uint8_t resp[2];
    uint8_t ck_flags = 0x00U;
    if (iolink_events_flag(&ctx->events)) {
        ck_flags |= 0x80U;
    }
    if (!ctx->pd_valid) {
        ck_flags |= 0x40U;
    }

    /* Figure A.5: a Type-0 WRITE is answered by the CKS only, on every
       communication channel; every Type-0 READ is answered by OD followed by
       the CKS. */
    const bool type0_write = ((mc & IOLINK_MC_RW_MASK) == 0U);
    if (type0_write) {
        resp[0] = ck_flags;
        resp[0] = (uint8_t) (resp[0] | iolink_checksum6(resp, 1U));
        if (ctx->phy->send != NULL) {
            ctx->phy->send(ctx->phy->user, resp, 1);
        }
    }
    else {
        resp[0] = od_resp;
        resp[1] = ck_flags;
        resp[1] = (uint8_t) (resp[1] | iolink_checksum6(resp, 2U));
        if (ctx->phy->send != NULL) {
            ctx->phy->send(ctx->phy->user, resp, 2);
        }
    }
    ctx->last_response_us = iolink_time_get_us();
}

/** @brief Process a Type-1/Type-2 OPERATE frame (PD+OD) and build the response, enforcing t_REN. */
static void dll_handle_operate_type1_2(iolink_dll_ctx_t* ctx)
{
    /* Type-1/2 master message: MC | CKT | PD | OD; the checksum is in CKT. */
    uint16_t pd_offset = IOLINK_M_SEQ_HEADER_LEN;
    uint16_t od_offset = (uint16_t) (pd_offset + ctx->pd_out_len_current);

    if (ctx->pd_out_len_current > 0U) {
        memcpy(ctx->pd_out, &ctx->frame_buf[pd_offset], ctx->pd_out_len_current);
    }

    uint8_t od_in[2] = {0, 0};
    uint8_t od_out[2] = {0, 0};
    memcpy(od_in, &ctx->frame_buf[od_offset], ctx->od_len);

    if (!dll_dispatch_od(ctx, ctx->frame_buf[0], od_in, ctx->od_len, od_out)) {
        for (uint16_t i = 0; i < ctx->od_len; i++) {
            od_out[i] = 0U;
        }
    }

    /* A.1.5 reply layout: [PD-in octets][OD octets] CKS, with no leading status
       octet. CKS carries the Event flag (bit 7), the PD-validity flag (bit 6,
       1 = invalid) and the 6-bit message checksum (bits 0-5). */
    /* A Type-0 WRITE is answered by the CKS only; a Type-0 READ by OD + CKS
       (A.1.5, Figure A.5). Multi-type replies always carry the OD octets. */
    const bool type0_write = (dll_expected_type_bits(ctx) == IOLINK_MSEQ_TYPE_0) &&
                             ((ctx->frame_buf[0] & IOLINK_MC_RW_MASK) == 0U);
    const uint8_t od_reply_len = type0_write ? 0U : ctx->od_len;

    uint8_t resp[IOLINK_PD_IN_MAX_SIZE + 5];
    uint16_t pos = 0U;
    if (ctx->pd_in_len_current > 0U) {
        memcpy(&resp[pos], ctx->pd_in, ctx->pd_in_len_current);
        pos += ctx->pd_in_len_current;
    }
    if (od_reply_len > 0U) {
        memcpy(&resp[pos], od_out, od_reply_len);
        pos += od_reply_len;
    }

    uint8_t cks = 0x00U;
    if (iolink_events_flag(&ctx->events)) {
        cks |= 0x80U;
    }
    if (!ctx->pd_valid) {
        cks |= 0x40U;
    }
    resp[pos] = cks;
    resp[pos] = (uint8_t) (resp[pos] | iolink_checksum6(resp, pos + 1U));
    pos++;

    if (ctx->phy->send != NULL) {
        ctx->phy->send(ctx->phy->user, resp, pos);
        ctx->fallback_count = 0U;
        uint32_t end_tx_us = (uint32_t) iolink_time_get_us();
        ctx->response_time_us = end_tx_us - (uint32_t) ctx->last_cycle_start_us;

        if (ctx->enforce_timing) {
            uint32_t limit = dll_get_t_ren_limit_us(ctx);
            if ((limit > 0U) && (ctx->response_time_us > limit)) {
                ctx->timing_errors++;
                ctx->t_ren_violations++;
                iolink_event_trigger(&ctx->events, IOLINK_EVENT_COMM_TIMING,
                                     IOLINK_EVENT_TYPE_WARNING);
            }
        }
    }
    ctx->last_response_us = iolink_time_get_us();
}

/** @brief Poll PHY voltage and short-circuit status and raise events on faults. */
static void dll_poll_diagnostics(iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->phy == NULL)) {
        return;
    }

    if (ctx->phy->get_voltage_mv != NULL) {
        int voltage = ctx->phy->get_voltage_mv(ctx->phy->user);
        if ((voltage < 18000) || (voltage > 30000)) {
            ctx->voltage_faults++;
            iolink_event_trigger(&ctx->events, IOLINK_EVENT_PHY_VOLTAGE_FAULT,
                                 IOLINK_EVENT_TYPE_WARNING);
        }
    }

    if (ctx->phy->is_short_circuit != NULL) {
        if (ctx->phy->is_short_circuit(ctx->phy->user)) {
            ctx->short_circuits++;
            iolink_event_trigger(&ctx->events, IOLINK_EVENT_PHY_SHORT_CIRCUIT,
                                 IOLINK_EVENT_TYPE_ERROR);
        }
    }
}

void iolink_dll_init(iolink_dll_ctx_t* ctx, const iolink_phy_api_t* phy)
{
    if ((phy == NULL) || (!iolink_ctx_zero(ctx, sizeof(iolink_dll_ctx_t)))) {
        return;
    }
    dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
    ctx->phy = phy;
    ctx->enforce_timing = (IOLINK_TIMING_ENFORCE_DEFAULT != 0U);
    ctx->sio_fallback_threshold = 3U;

    if ((ctx->m_seq_type == IOLINK_M_SEQ_TYPE_2_1) || ctx->m_seq_type == IOLINK_M_SEQ_TYPE_2_2 ||
        ctx->m_seq_type == IOLINK_M_SEQ_TYPE_2_V) {
        ctx->od_len = 2U;
    }
    else {
        ctx->od_len = 1U;
    }

    ctx->pd_in_len_current = ctx->pd_in_len;
    ctx->pd_out_len_current = ctx->pd_out_len;
    ctx->pd_in_len_max = ctx->pd_in_len;
    ctx->pd_out_len_max = ctx->pd_out_len;
    ctx->pd_valid = true;

    ctx->baudrate = IOLINK_BAUDRATE_COM2;
    if (ctx->phy->set_baudrate != NULL) {
        ctx->phy->set_baudrate(ctx->phy->user, IOLINK_BAUDRATE_COM2);
    }

    iolink_events_init(&ctx->events);
    iolink_isdu_init(&ctx->isdu);
    iolink_ds_init(&ctx->ds, NULL);
    ctx->isdu.event_ctx = &ctx->events;
    ctx->isdu.dll_ctx = ctx;
    ctx->isdu.ds_ctx = &ctx->ds;

    ctx->t_ren_limit_us = dll_get_t_ren_limit_us(ctx);
    ctx->t_byte_limit_us = dll_get_t_byte_limit_us(ctx);

    /*
     * Initial line mode depends on whether the PHY can observe the electrical
     * C/Q wake-up pulse:
     *
     *  - PHYs that can (detect_wakeup != NULL, e.g. a transceiver front-end or
     *    the virtual PHY) start in SIO and switch to SDCI when a wake-up is
     *    detected, matching the IO-Link startup handshake.
     *
     *  - PHYs that cannot (detect_wakeup == NULL, e.g. a plain UART sitting
     *    behind a transceiver that has already established COM) would otherwise
     *    sit in SIO forever and never service M-sequences. Such a link is, by
     *    contract, an already-established SDCI link, so start in SDCI and accept
     *    M-sequences immediately.
     */
    if (ctx->phy->detect_wakeup != NULL) {
        iolink_dll_set_sio_mode(ctx);
    }
    else {
        iolink_dll_set_sdci_mode(ctx);
    }
}

void iolink_dll_process(iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->phy == NULL)) {
        return;
    }

    /* Process acyclic ISDU state machine */
    iolink_isdu_process(&ctx->isdu);

    dll_poll_diagnostics(ctx);

    uint32_t now_ms = iolink_time_get_ms();
    if ((ctx->last_activity_ms != 0U) && (now_ms - ctx->last_activity_ms > 1000U)) {
        ctx->last_activity_ms = 0U; /* Prevent repeated resets */
        ctx->timeout_errors++;      /* Link went idle past the inactivity window */
        if (ctx->phy_mode != IOLINK_PHY_MODE_SIO) {
            iolink_dll_set_baudrate(ctx, IOLINK_BAUDRATE_COM1);
            iolink_dll_set_sio_mode(ctx);
        }
        dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
        ctx->frame_index = 0U;
    }

    /* Table 47 T10: a wake-up without a valid message within T_DSIO returns the
       device to SIO. Only applies while still waiting for the first message. */
    if ((ctx->phy_mode != IOLINK_PHY_MODE_SIO) && (ctx->dsio_deadline_ms != 0U) &&
        ((ctx->state == IOLINK_DLL_STATE_AWAITING_COMM) ||
         (ctx->state == IOLINK_DLL_STATE_STARTUP))) {
        if (now_ms > ctx->dsio_deadline_ms) {
            ctx->dsio_deadline_ms = 0U;
            ctx->wakeup_seen = false;
            ctx->timeout_errors++;
            iolink_dll_set_baudrate(ctx, IOLINK_BAUDRATE_COM1);
            iolink_dll_set_sio_mode(ctx);
            dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
            ctx->frame_index = 0U;
        }
    }

    /* Table 47 T8/T9 + Table 43: after a FALLBACK MasterCommand the device
       switches to SIO after T_FBD (3 MasterCycleTimes, at most 500 ms) at the
       latest. The reply was already sent when the command was handled. */
    if ((ctx->fallback_deadline_ms != 0U) &&
        ((ctx->state == IOLINK_DLL_STATE_FALLBACK) || (ctx->state == IOLINK_DLL_STATE_OPERATE) ||
         (ctx->state == IOLINK_DLL_STATE_ESTAB_COM) ||
         (ctx->state == IOLINK_DLL_STATE_PREOPERATE))) {
        if (now_ms >= ctx->fallback_deadline_ms) {
            ctx->fallback_deadline_ms = 0U;
            iolink_dll_set_baudrate(ctx, IOLINK_BAUDRATE_COM1);
            iolink_dll_set_sio_mode(ctx);
            dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
            ctx->frame_index = 0U;
        }
    }

    if (dll_t_pd_active(ctx)) {
        if (dll_drain_rx(ctx)) {
            ctx->timing_errors++;
            ctx->t_pd_violations++;
            iolink_event_trigger(&ctx->events, IOLINK_EVENT_COMM_TIMING, IOLINK_EVENT_TYPE_WARNING);
        }
        return;
    }

    if (ctx->phy_mode == IOLINK_PHY_MODE_SIO) {
        if ((ctx->frame_index == 0U) && (ctx->phy->detect_wakeup != NULL)) {
            if (ctx->phy->detect_wakeup(ctx->phy->user) > 0) {
                ctx->wakeup_seen = true;
                dll_set_state(ctx, IOLINK_DLL_STATE_AWAITING_COMM);
                ctx->wakeup_deadline_us = iolink_time_get_us() + IOLINK_T_WU_US;
                /* Table 47 T10: without a valid message the device returns to
                   SIO within T_DSIO (60..300 ms, default 300 ms). */
                ctx->dsio_deadline_ms = iolink_time_get_ms() + IOLINK_T_DSIO_MS;
                iolink_dll_set_sdci_mode(ctx);
                /* A wake-up starts a fresh communication-establishment window.
                   Reset the inactivity timer so a re-wake after a prior exchange
                   is not immediately killed by the stale activity timestamp. */
                ctx->last_activity_ms = iolink_time_get_ms();
            }
        }
        return;
    }

    if ((ctx->frame_index > 0U) && (ctx->enforce_timing) && (ctx->t_byte_limit_us > 0U)) {
        if (ctx->last_byte_us != 0U) {
            uint64_t now_us = iolink_time_get_us();
            if (now_us - ctx->last_byte_us > (uint64_t) ctx->t_byte_limit_us) {
                ctx->timing_errors++;
                ctx->t_byte_violations++;
                ctx->framing_errors++;
                iolink_event_trigger(&ctx->events, IOLINK_EVENT_COMM_TIMING,
                                     IOLINK_EVENT_TYPE_WARNING);
                ctx->frame_index = 0U;
                dll_enter_fallback(ctx);
            }
        }
    }

    if ((ctx->state == IOLINK_DLL_STATE_AWAITING_COMM) && (ctx->enforce_timing)) {
        if ((ctx->wakeup_deadline_us != 0U) && (iolink_time_get_us() < ctx->wakeup_deadline_us)) {
            return;
        }
    }

    uint8_t byte;
    while ((ctx->phy->recv_byte != NULL) && (ctx->phy->recv_byte(ctx->phy->user, &byte) > 0)) {
        uint64_t now_us = iolink_time_get_us();
        ctx->last_activity_ms = iolink_time_get_ms();
        if ((ctx->frame_index > 0U) && (ctx->enforce_timing) && (ctx->t_byte_limit_us > 0U)) {
            if (ctx->last_byte_us != 0U) {
                if (now_us - ctx->last_byte_us > (uint64_t) ctx->t_byte_limit_us) {
                    ctx->timing_errors++;
                    ctx->t_byte_violations++;
                    ctx->framing_errors++;
                    iolink_event_trigger(&ctx->events, IOLINK_EVENT_COMM_TIMING,
                                         IOLINK_EVENT_TYPE_WARNING);
                    ctx->frame_index = 0U;
                }
            }
        }
        ctx->last_byte_us = now_us;

        if (ctx->frame_index == 0U) {
            ctx->frame_buf[0] = byte;
            ctx->frame_index = 1U;
            ctx->last_frame_us = now_us;

            /* A Type-0 WRITE on an OD-carrying channel (page, diagnosis, ISDU)
               carries one OD octet after the CKT: MC + CKT + OD. This covers
               DeviceOperate/FALLBACK MasterCommands and every ISDU/event write.
               Type-0 reads carry no OD input; their reply is OD + CKS (A.1.5). */
            const uint8_t type0_channel = (uint8_t) (byte & IOLINK_MC_COMM_CHANNEL_MASK);
            const bool type0_od_write = (dll_expected_type_bits(ctx) == IOLINK_MSEQ_TYPE_0) &&
                                        ((byte & IOLINK_MC_RW_MASK) == 0U) &&
                                        ((type0_channel == IOLINK_MC_CHANNEL_PAGE) ||
                                         (type0_channel == IOLINK_MC_CHANNEL_DIAGNOSIS) ||
                                         (type0_channel == IOLINK_MC_CHANNEL_ISDU));
            if (type0_od_write) {
                ctx->req_len = (uint8_t) (IOLINK_M_SEQ_HEADER_LEN + ctx->od_len);
            }
            else {
                /* STARTUP/PREOPERATE expect Type 0; ESTAB_COM and OPERATE use
                   the configured type (C5/Table 47). The transition command is
                   the sole Type-0 exchange once a multi type is configured.
                   The M-sequence length is independent of the baudrate. */
                uint8_t type_bits = dll_expected_type_bits(ctx);
                bool transition_cmd = (ctx->state == IOLINK_DLL_STATE_ESTAB_COM) &&
                                      (byte == IOLINK_MC_TRANSITION_COMMAND);
                if (!transition_cmd && (type_bits != IOLINK_MSEQ_TYPE_0)) {
                    ctx->req_len = dll_expected_req_len(ctx, type_bits);
                }
                else {
                    ctx->req_len = 2U;
                }
            }
        }
        else {
            if (ctx->frame_index < sizeof(ctx->frame_buf)) {
                ctx->frame_buf[ctx->frame_index++] = byte;
            }
            else {
                ctx->frame_index = 0U;
                ctx->framing_errors++;
            }
        }

        if ((ctx->frame_index > 0U) && (ctx->frame_index >= ctx->req_len)) {
            uint64_t now_us_proc = iolink_time_get_us();
            if ((ctx->enforce_timing) && (ctx->min_cycle_time_us > 0U) &&
                (ctx->last_cycle_start_us != 0U)) {
                if (now_us_proc - ctx->last_cycle_start_us < (uint64_t) ctx->min_cycle_time_us) {
                    ctx->timing_errors++;
                    ctx->t_cycle_violations++;
                    iolink_event_trigger(&ctx->events, IOLINK_EVENT_COMM_TIMING,
                                         IOLINK_EVENT_TYPE_WARNING);
                }
            }
            ctx->last_cycle_start_us = now_us_proc;

            /* A.1.6/Figure A.2: the master message carries the checksum in the
               CKT octet (the second octet); its bits 0-5 are zeroed before the
               A.1.6 checksum is computed over every message octet. The CKT is
               also where the M-sequence type (bits 6-7) lives. */
            uint8_t crc_buf[sizeof(ctx->frame_buf)];
            (void) memcpy(crc_buf, ctx->frame_buf, ctx->req_len);
            const uint8_t expected_ck = (uint8_t) (crc_buf[1] & 0x3FU);
            crc_buf[1] = (uint8_t) (crc_buf[1] & IOLINK_MSEQ_TYPE_MASK);
            bool crc_ok = (iolink_checksum6(crc_buf, ctx->req_len) == expected_ck);

            /* C5/Table 47: the CKT type bits must match the M-sequence type in
               force. STARTUP and PREOPERATE are Type-0 only; in OPERATE the
               configured type is expected. An illegal type is an illegal
               M-sequence: report it and return to STARTUP (Table 47 T8/T11). */
            uint8_t rx_type = (uint8_t) (ctx->frame_buf[1] & IOLINK_MSEQ_TYPE_MASK);
            uint8_t exp_type = IOLINK_MSEQ_TYPE_0;
            if ((ctx->state == IOLINK_DLL_STATE_OPERATE) ||
                ((ctx->state == IOLINK_DLL_STATE_ESTAB_COM) &&
                 (ctx->frame_buf[0] != IOLINK_MC_TRANSITION_COMMAND))) {
                exp_type = dll_mseq_type_bits(ctx->m_seq_type);
            }
            bool type_ok = (rx_type == exp_type);

            if (crc_ok && type_ok) {
                bool was_establishing = (ctx->state == IOLINK_DLL_STATE_AWAITING_COMM) ||
                                        (ctx->state == IOLINK_DLL_STATE_STARTUP);
                if (was_establishing) {
                    dll_set_state(ctx, IOLINK_DLL_STATE_PREOPERATE);
                    ctx->dsio_deadline_ms = 0U; /* valid message cancels T_DSIO */
                }

                if (ctx->state == IOLINK_DLL_STATE_PREOPERATE) {
                    uint8_t mc = ctx->frame_buf[0];
                    bool page_read = ((mc & IOLINK_MC_RW_MASK) != 0U) &&
                                     ((mc & IOLINK_MC_COMM_CHANNEL_MASK) == 0x20U);
                    if (was_establishing && (ctx->req_len == 2U) && page_read) {
                        /* First message after wake-up is the spec startup probe
                           (transition T1): a Type-0 READ of a Direct Parameter page
                           octet on the page channel. Answer from the direct-parameter
                           source rather than feeding the MC into ISDU. Later PREOPERATE
                           Type-0 frames are ISDU traffic and are not intercepted. */
                        dll_handle_page_channel_read(ctx, mc);
                    }
                    else if (ctx->req_len == 2U) {
                        if (mc == IOLINK_MC_TRANSITION_COMMAND) {
                            dll_handle_preoperate(ctx, mc, ctx->frame_buf[1]);
                        }
                        else {
                            dll_handle_operate_type0(ctx, mc, ctx->frame_buf[1]);
                        }
                    }
                    else if ((ctx->req_len == 3U) && dll_is_fallback_command(ctx)) {
                        /* Table B.2/43: FALLBACK MasterCommand 0x5A. */
                        dll_handle_fallback_command(ctx);
                    }
                    else if ((ctx->req_len == 3U) && ((mc & IOLINK_MC_RW_MASK) == 0U) &&
                             ((mc & IOLINK_MC_COMM_CHANNEL_MASK) == 0x20U) &&
                             ((mc & IOLINK_MC_ADDR_MASK) == 0x00U) &&
                             (ctx->frame_buf[2] == IOLINK_CMD_DEVICE_OPERATE)) {
                        /* Spec DeviceOperate: page-channel Type-0 WRITE of
                           MasterCommand 0x99 to Direct Parameter address 0x00.
                           Figure A.5: a Type-0 write reply is the CKS only. */
                        dll_set_state(ctx, IOLINK_DLL_STATE_ESTAB_COM);
                        ctx->fallback_count = 0U;

                        uint8_t resp[1];
                        uint8_t cks = 0x00U;
                        if (iolink_events_flag(&ctx->events)) {
                            cks |= 0x80U;
                        }
                        if (!ctx->pd_valid) {
                            cks |= 0x40U;
                        }
                        resp[0] = cks;
                        resp[0] = (uint8_t) (resp[0] | iolink_checksum6(resp, 1U));
                        if (ctx->phy->send != NULL) {
                            ctx->phy->send(ctx->phy->user, resp, 1);
                        }
                        ctx->last_response_us = iolink_time_get_us();
                    }
                    else if (ctx->req_len == 3U) {
                        /* Any other 3-octet Type-0 OD write in PREOPERATE is
                           ISDU or diagnosis traffic (7.3.6 allows ISDU before
                           OPERATE); dispatch it through the OD channel handler. */
                        dll_handle_operate_type1_2(ctx);
                    }
                }
                else if (ctx->state == IOLINK_DLL_STATE_ESTAB_COM) {
                    if (ctx->frame_buf[0] == IOLINK_MC_TRANSITION_COMMAND) {
                        dll_handle_preoperate(ctx, ctx->frame_buf[0], ctx->frame_buf[1]);
                    }
                    else {
                        /* The first valid non-transition frame establishes
                           communication and moves to OPERATE, Type 0 included. */
                        dll_set_state(ctx, IOLINK_DLL_STATE_OPERATE);
                        if (ctx->req_len == 2U) {
                            dll_handle_operate_type0(ctx, ctx->frame_buf[0], ctx->frame_buf[1]);
                        }
                        else {
                            dll_handle_operate_type1_2(ctx);
                        }
                    }
                }
                else if (ctx->state == IOLINK_DLL_STATE_OPERATE) {
                    /* Transitions are forbidden; every communication channel is
                       valid in OPERATE (page/diagnosis/ISDU/process). */
                    if ((ctx->req_len == 3U) && (dll_is_fallback_command(ctx))) {
                        /* Table B.2/43, Table 47 T9: OPERATE -> SIO after T_FBD. */
                        dll_handle_fallback_command(ctx);
                    }
                    else if (ctx->frame_buf[0] == IOLINK_MC_TRANSITION_COMMAND) {
                        ctx->framing_errors++;
                        dll_enter_fallback(ctx);
                    }
                    else if (ctx->req_len == 2U) {
                        dll_handle_operate_type0(ctx, ctx->frame_buf[0], ctx->frame_buf[1]);
                    }
                    else {
                        dll_handle_operate_type1_2(ctx);
                    }
                }
            }
            else if (crc_ok && !type_ok) {
                /* Illegal M-sequence type: report it and go to STARTUP (T11). */
                ctx->framing_errors++;
                iolink_event_trigger(&ctx->events, IOLINK_EVENT_CODE_COMM_ERR_GENERAL,
                                     IOLINK_EVENT_TYPE_ERROR);
                dll_set_state(ctx, IOLINK_DLL_STATE_STARTUP);
                ctx->frame_index = 0U;
                continue;
            }
            else {
                ctx->crc_errors++;
                ctx->framing_errors++;
                dll_enter_fallback(ctx);
            }
            ctx->frame_index = 0U;
        }
    }
}

iolink_dll_state_t iolink_dll_get_state(const iolink_dll_ctx_t* ctx)
{
    return (ctx != NULL) ? ctx->state : IOLINK_DLL_STATE_STARTUP;
}

iolink_phy_mode_t iolink_dll_get_phy_mode(const iolink_dll_ctx_t* ctx)
{
    return (ctx != NULL) ? ctx->phy_mode : IOLINK_PHY_MODE_SIO;
}

iolink_baudrate_t iolink_dll_get_baudrate(const iolink_dll_ctx_t* ctx)
{
    return (ctx != NULL) ? ctx->baudrate : IOLINK_BAUDRATE_COM2;
}

int iolink_dll_set_baudrate(iolink_dll_ctx_t* ctx, iolink_baudrate_t baudrate)
{
    if (ctx == NULL) return -1;
    ctx->baudrate = baudrate;
    if (ctx->phy->set_baudrate != NULL) ctx->phy->set_baudrate(ctx->phy->user, baudrate);
    ctx->t_ren_limit_us = dll_get_t_ren_limit_us(ctx);
    ctx->t_byte_limit_us = dll_get_t_byte_limit_us(ctx);
    return 0;
}

int iolink_dll_set_pd_length(iolink_dll_ctx_t* ctx, uint8_t pd_in_len, uint8_t pd_out_len)
{
    if ((ctx == NULL) || (pd_in_len > IOLINK_PD_IN_MAX_SIZE) ||
        (pd_out_len > IOLINK_PD_OUT_MAX_SIZE))
        return -1;
    ctx->pd_in_len_current = pd_in_len;
    ctx->pd_out_len_current = pd_out_len;
    return 0;
}

void iolink_dll_get_pd_length(const iolink_dll_ctx_t* ctx, uint8_t* pd_in_len, uint8_t* pd_out_len)
{
    if (ctx == NULL) return;
    if (pd_in_len) *pd_in_len = ctx->pd_in_len_current;
    if (pd_out_len) *pd_out_len = ctx->pd_out_len_current;
}

int iolink_dll_set_sio_mode(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) return -1;
    if (ctx->phy->set_mode != NULL) ctx->phy->set_mode(ctx->phy->user, IOLINK_PHY_MODE_SIO);
    ctx->phy_mode = IOLINK_PHY_MODE_SIO;
    return 0;
}

int iolink_dll_set_sdci_mode(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) return -1;
    if (ctx->phy->set_mode != NULL) ctx->phy->set_mode(ctx->phy->user, IOLINK_PHY_MODE_SDCI);
    ctx->phy_mode = IOLINK_PHY_MODE_SDCI;
    return 0;
}

void iolink_dll_get_stats(const iolink_dll_ctx_t* ctx, iolink_dll_stats_t* out_stats)
{
    if ((ctx == NULL) || (out_stats == NULL)) return;
    out_stats->crc_errors = ctx->crc_errors;
    out_stats->timeout_errors = ctx->timeout_errors;
    out_stats->framing_errors = ctx->framing_errors;
    out_stats->timing_errors = ctx->timing_errors;
    out_stats->t_ren_violations = ctx->t_ren_violations;
    out_stats->t_cycle_violations = ctx->t_cycle_violations;
    out_stats->t_byte_violations = ctx->t_byte_violations;
    out_stats->t_pd_violations = ctx->t_pd_violations;
    out_stats->total_retries = ctx->total_retries;
    out_stats->voltage_faults = ctx->voltage_faults;
    out_stats->short_circuits = ctx->short_circuits;
}

void iolink_dll_set_timing_enforcement(iolink_dll_ctx_t* ctx, bool enable)
{
    if (ctx != NULL) ctx->enforce_timing = enable;
}

void iolink_dll_set_t_ren_limit_us(iolink_dll_ctx_t* ctx, uint32_t limit_us)
{
    if (ctx == NULL) return;
    ctx->t_ren_limit_us = limit_us;
    ctx->t_ren_override = (limit_us != 0U);
}
