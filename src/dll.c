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

#define DLL_LOG(...)

static uint32_t dll_get_t_ren_limit_us(const iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return 0U;
    }
    if (ctx->t_ren_override) {
        return ctx->t_ren_limit_us;
    }

    switch (ctx->baudrate) {
        case IOLINK_BAUDRATE_COM1:
            return IOLINK_T_REN_COM1_US;
        case IOLINK_BAUDRATE_COM2:
            return IOLINK_T_REN_COM2_US;
        case IOLINK_BAUDRATE_COM3:
            return IOLINK_T_REN_COM3_US;
        default:
            return IOLINK_T_REN_COM2_US;
    }
}

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

static bool dll_t_pd_active(const iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->t_pd_deadline_us == 0U)) {
        return false;
    }
    return iolink_time_get_us() < ctx->t_pd_deadline_us;
}

static bool dll_drain_rx(iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->phy == NULL) || (ctx->phy->recv_byte == NULL)) {
        return false;
    }
    bool saw_byte = false;
    uint8_t byte = 0U;
    while (ctx->phy->recv_byte(&byte) > 0) {
        saw_byte = true;
    }
    return saw_byte;
}

static void dll_enter_fallback(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }

    ctx->fallback_count++;
    ctx->total_retries++;

    if (ctx->fallback_count >= ctx->sio_fallback_threshold) {
        iolink_dll_set_sio_mode(ctx);
        iolink_dll_set_baudrate(ctx, IOLINK_BAUDRATE_COM1);
        ctx->state = IOLINK_DLL_STATE_STARTUP;
        ctx->fallback_count = 0U;
        ctx->frame_index = 0U;
        iolink_event_trigger(&ctx->events, IOLINK_EVENT_CODE_COMM_ERR_FRAMING,
                             IOLINK_EVENT_TYPE_WARNING);
    }
    else if (ctx->state != IOLINK_DLL_STATE_OPERATE && ctx->state != IOLINK_DLL_STATE_ESTAB_COM) {
        iolink_dll_set_baudrate(ctx, IOLINK_BAUDRATE_COM1);
        ctx->state = IOLINK_DLL_STATE_STARTUP;
    }
}

static void dll_handle_preoperate(iolink_dll_ctx_t* ctx, uint8_t mc, uint8_t ck)
{
    (void) ck;
    if (mc == IOLINK_MC_TRANSITION_COMMAND) {
        ctx->state = IOLINK_DLL_STATE_ESTAB_COM;
        ctx->fallback_count = 0U;
        /* No response to transition command per spec */
    }
}

static void dll_handle_operate_type0(iolink_dll_ctx_t* ctx, uint8_t mc, uint8_t cks)
{
    (void) cks;
    uint8_t od_resp = 0U;
    iolink_isdu_collect_byte(&ctx->isdu, mc);
    if (iolink_isdu_get_response_byte(&ctx->isdu, &od_resp) == 0) {
        od_resp = 0U;
    }

    uint8_t resp[2];
    resp[0] = od_resp;
    resp[1] = iolink_checksum_ck(resp[0], 0U);
    if (ctx->phy->send != NULL) {
        ctx->phy->send(resp, 2);
    }
}

static void dll_handle_operate_type1_2(iolink_dll_ctx_t* ctx)
{
    /* IO-Link V1.1 M-sequence structure: MC | CKT | PD | OD | CK */
    uint16_t pd_offset = IOLINK_M_SEQ_HEADER_LEN;
    uint16_t od_offset = (uint16_t) (pd_offset + ctx->pd_out_len_current);

    if (ctx->pd_out_len_current > 0U) {
        memcpy(ctx->pd_out, &ctx->frame_buf[pd_offset], ctx->pd_out_len_current);
    }

    uint8_t od_in[2] = {0, 0};
    uint8_t od_out[2] = {0, 0};
    memcpy(od_in, &ctx->frame_buf[od_offset], ctx->od_len);

    for (uint16_t i = 0; i < ctx->od_len; i++) {
        iolink_isdu_collect_byte(&ctx->isdu, od_in[i]);
        if (iolink_isdu_get_response_byte(&ctx->isdu, &od_out[i]) == 0) {
            od_out[i] = 0U;
        }
    }

    uint8_t resp[IOLINK_PD_IN_MAX_SIZE + 5];
    uint8_t status = 0x00;
    if (iolink_events_pending(&ctx->events)) status |= IOLINK_OD_STATUS_EVENT;
    if (ctx->pd_in_toggle) status |= IOLINK_OD_STATUS_PD_TOGGLE;
    if (ctx->pd_valid) status |= IOLINK_OD_STATUS_PD_VALID;

    resp[0] = status;
    uint16_t pos = 1U;
    if (ctx->pd_in_len_current > 0U) {
        memcpy(&resp[pos], ctx->pd_in, ctx->pd_in_len_current);
        pos += ctx->pd_in_len_current;
    }
    memcpy(&resp[pos], od_out, ctx->od_len);
    pos += ctx->od_len;

    resp[pos] = iolink_crc6(resp, (uint8_t) pos);
    pos++;

    if (ctx->phy->send != NULL) {
        ctx->phy->send(resp, pos);
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

static void dll_poll_diagnostics(iolink_dll_ctx_t* ctx)
{
    if ((ctx == NULL) || (ctx->phy == NULL)) {
        return;
    }

    if (ctx->phy->get_voltage_mv != NULL) {
        int voltage = ctx->phy->get_voltage_mv();
        if ((voltage < 18000) || (voltage > 30000)) {
            ctx->voltage_faults++;
            iolink_event_trigger(&ctx->events, IOLINK_EVENT_PHY_VOLTAGE_FAULT,
                                 IOLINK_EVENT_TYPE_WARNING);
        }
    }

    if (ctx->phy->is_short_circuit != NULL) {
        if (ctx->phy->is_short_circuit()) {
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
    ctx->state = IOLINK_DLL_STATE_STARTUP;
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
        ctx->phy->set_baudrate(IOLINK_BAUDRATE_COM2);
    }

    iolink_events_init(&ctx->events);
    iolink_isdu_init(&ctx->isdu);
    iolink_ds_init(&ctx->ds, NULL);
    ctx->isdu.event_ctx = &ctx->events;
    ctx->isdu.dll_ctx = ctx;

    ctx->t_ren_limit_us = dll_get_t_ren_limit_us(ctx);
    ctx->t_byte_limit_us = dll_get_t_byte_limit_us(ctx);

    iolink_dll_set_sio_mode(ctx);
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
    if ((ctx->last_activity_ms != 0U) && (now_ms - ctx->last_activity_ms > 20000U)) {
        ctx->last_activity_ms = 0U; /* Prevent repeated resets */
        if (ctx->phy_mode != IOLINK_PHY_MODE_SIO) {
            iolink_dll_set_baudrate(ctx, IOLINK_BAUDRATE_COM1);
            iolink_dll_set_sio_mode(ctx);
        }
        ctx->state = IOLINK_DLL_STATE_STARTUP;
        ctx->frame_index = 0U;
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
            if (ctx->phy->detect_wakeup() > 0) {
                ctx->wakeup_seen = true;
                ctx->state = IOLINK_DLL_STATE_AWAITING_COMM;
                ctx->wakeup_deadline_us = iolink_time_get_us() + IOLINK_T_DWU_US;
                iolink_dll_set_sdci_mode(ctx);
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
    while ((ctx->phy->recv_byte != NULL) && (ctx->phy->recv_byte(&byte) > 0)) {
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

            if (ctx->baudrate == IOLINK_BAUDRATE_COM1) {
                ctx->req_len = 2U;
            }
            else {
                bool can_be_multi = (ctx->m_seq_type != IOLINK_M_SEQ_TYPE_0);
                if (can_be_multi && (ctx->state == IOLINK_DLL_STATE_OPERATE)) {
                    ctx->req_len = (uint8_t) (IOLINK_M_SEQ_HEADER_LEN + ctx->pd_out_len_current +
                                              ctx->od_len + 1U);
                }
                else if (can_be_multi && (ctx->state == IOLINK_DLL_STATE_ESTAB_COM) &&
                         (byte != IOLINK_MC_TRANSITION_COMMAND)) {
                    /* Initial Type 1/2 frame to move from ESTAB_COM to OPERATE.
                     * Any non-Type0 command (MC starting with 00) is a Type 1/2 frame. */
                    ctx->req_len = (uint8_t) (IOLINK_M_SEQ_HEADER_LEN + ctx->pd_out_len_current +
                                              ctx->od_len + 1U);
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

            bool crc_ok;
            if (ctx->req_len == 2U) {
                crc_ok = (iolink_checksum_ck(ctx->frame_buf[0], 0U) == ctx->frame_buf[1]);
            }
            else {
                crc_ok = (iolink_crc6(ctx->frame_buf, (uint8_t) (ctx->req_len - 1)) ==
                          ctx->frame_buf[ctx->req_len - 1]);
            }

            if (crc_ok) {
                if ((ctx->state == IOLINK_DLL_STATE_AWAITING_COMM) ||
                    (ctx->state == IOLINK_DLL_STATE_STARTUP)) {
                    ctx->state = IOLINK_DLL_STATE_PREOPERATE;
                }

                if (ctx->state == IOLINK_DLL_STATE_PREOPERATE) {
                    if (ctx->req_len == 2U) {
                        if (ctx->frame_buf[0] == IOLINK_MC_TRANSITION_COMMAND)
                            dll_handle_preoperate(ctx, ctx->frame_buf[0], ctx->frame_buf[1]);
                        else
                            dll_handle_operate_type0(ctx, ctx->frame_buf[0], ctx->frame_buf[1]);
                    }
                }
                else if (ctx->state == IOLINK_DLL_STATE_ESTAB_COM) {
                    if (ctx->req_len > 2U) {
                        uint8_t channel = ctx->frame_buf[0] & 0x60U;
                        if (channel == 0x20U || channel == 0x60U) {
                            ctx->framing_errors++;
                            dll_enter_fallback(ctx);
                        }
                        else {
                            ctx->state = IOLINK_DLL_STATE_OPERATE;
                            dll_handle_operate_type1_2(ctx);
                        }
                    }
                    else if (ctx->frame_buf[0] == IOLINK_MC_TRANSITION_COMMAND) {
                        dll_handle_preoperate(ctx, ctx->frame_buf[0], ctx->frame_buf[1]);
                    }
                    else {
                        dll_handle_operate_type0(ctx, ctx->frame_buf[0], ctx->frame_buf[1]);
                    }
                }
                else if (ctx->state == IOLINK_DLL_STATE_OPERATE) {
                    uint8_t channel = ctx->frame_buf[0] & 0x60U;
                    /* Transitions forbidden. Page Address (0x20) and Reserved (0x60) channels
                     * rejected. */
                    if (ctx->frame_buf[0] == IOLINK_MC_TRANSITION_COMMAND || channel == 0x20U ||
                        channel == 0x60U) {
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
    if (ctx->phy->set_baudrate != NULL) ctx->phy->set_baudrate(baudrate);
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
    if (ctx->phy->set_mode != NULL) ctx->phy->set_mode(IOLINK_PHY_MODE_SIO);
    ctx->phy_mode = IOLINK_PHY_MODE_SIO;
    return 0;
}

int iolink_dll_set_sdci_mode(iolink_dll_ctx_t* ctx)
{
    if (ctx == NULL) return -1;
    if (ctx->phy->set_mode != NULL) ctx->phy->set_mode(IOLINK_PHY_MODE_SDCI);
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
