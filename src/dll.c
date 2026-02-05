/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/protocol.h"
#include "iolinki/iolink.h"
#include "iolinki/dll.h"
#include "iolinki/crc.h"
#include "iolinki/isdu.h"
#include "iolinki/events.h"
#include "iolinki/time_utils.h"
#include "iolinki/platform.h"
#include "iolinki/utils.h"
#include "dll_internal.h"
#include <string.h>
#include <stdio.h>

#define DLL_DEBUG 0
#if DLL_DEBUG
#define DLL_LOG(...) printf("[DLL] " __VA_ARGS__)
#else
#define DLL_LOG(...)
#endif

#define IOLINK_DLL_TIMEOUT_MS 1000U

static uint32_t dll_get_t_ren_limit_us(const iolink_dll_ctx_t *ctx)
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

static void dll_note_frame_start(iolink_dll_ctx_t *ctx)
{
    uint64_t now_us = iolink_time_get_us();
    if ((ctx->enforce_timing) && (ctx->min_cycle_time_us > 0U) &&
        (ctx->last_cycle_start_us != 0U)) {
        uint64_t delta = now_us - ctx->last_cycle_start_us;
        if (delta < (uint64_t) ctx->min_cycle_time_us) {
            ctx->timing_errors++;
            ctx->t_cycle_violations++;
            iolink_event_trigger(&ctx->events, IOLINK_EVENT_COMM_TIMING, IOLINK_EVENT_TYPE_WARNING);
        }
    }
    ctx->last_cycle_start_us = now_us;
    ctx->last_frame_us = now_us;
}

static void dll_record_response_time(iolink_dll_ctx_t *ctx)
{
    if (ctx->last_frame_us == 0U) {
        return;
    }
    uint64_t now_us = iolink_time_get_us();
    uint64_t delta = now_us - ctx->last_frame_us;
    ctx->response_time_us = (uint32_t) delta;
    ctx->last_response_us = now_us;

    if (ctx->enforce_timing) {
        uint32_t limit = dll_get_t_ren_limit_us(ctx);
        if ((limit > 0U) && (delta > (uint64_t) limit)) {
            ctx->timing_errors++;
            ctx->t_ren_violations++;
            iolink_event_trigger(&ctx->events, IOLINK_EVENT_COMM_TIMING, IOLINK_EVENT_TYPE_WARNING);
        }
    }
}

static void dll_send_response(iolink_dll_ctx_t *ctx, const uint8_t *data, size_t len)
{
    if ((ctx == NULL) || (ctx->phy == NULL) || (ctx->phy->send == NULL)) {
        return;
    }
    (void) ctx->phy->send(data, len);
    dll_record_response_time(ctx);
}

static void enter_fallback(iolink_dll_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    ctx->fallback_count++;

    /* Check if we should transition to SIO mode for safety */
    if (ctx->fallback_count >= ctx->sio_fallback_threshold) {
        DLL_LOG("Fallback threshold reached (%u). Transitioning to SIO mode.\n",
                ctx->fallback_count);

        /* Transition to SIO mode */
        if (ctx->phy->set_mode != NULL) {
            ctx->phy->set_mode(IOLINK_PHY_MODE_SIO);
        }
        ctx->phy_mode = IOLINK_PHY_MODE_SIO;

        /* Trigger event for SIO fallback */
        iolink_event_trigger(&ctx->events, IOLINK_EVENT_HW_GENERAL_FAULT, IOLINK_EVENT_TYPE_ERROR);

        /* Reset fallback counter - we're now in SIO */
        ctx->fallback_count = 0U;
    }

    ctx->state = IOLINK_DLL_STATE_FALLBACK;
    ctx->frame_index = 0U;
    ctx->req_len = IOLINK_M_SEQ_TYPE0_LEN;
    ctx->retry_count = 0U;

    /* Reset sub-engines to ensure clean recovery */
    iolink_isdu_init(&ctx->isdu);
    ctx->isdu.event_ctx = &ctx->events;
}

/* Helper to calculate checksum for Type 0 */
/* Helper to calculate checksum for Type 0 - inlined or used directly */

static void handle_preoperate(iolink_dll_ctx_t *ctx, uint8_t byte);
static void handle_awaiting_comm(iolink_dll_ctx_t *ctx, uint8_t byte);

static bool is_valid_mc_for_state(const iolink_dll_ctx_t *ctx, uint8_t mc)
{
    if (ctx == NULL) {
        return false;
    }

    /* Transition Command is only valid in PREOPERATE */
    if (mc == IOLINK_MC_TRANSITION_COMMAND) {
        return (ctx->state == IOLINK_DLL_STATE_PREOPERATE);
    }

    switch (ctx->state) {
        case IOLINK_DLL_STATE_PREOPERATE:
            /* In PREOPERATE, only Type 0 frames are allowed.
             * Type 0 MC format: [RW][CommunicationChannel(2bits)][Address(5bits)]
             * For Type 0, CommunicationChannel must be 00 (ISDU).
             */
            if ((mc & IOLINK_MC_COMM_CHANNEL_MASK) != 0U) {
                return false;
            }
            return true;

        case IOLINK_DLL_STATE_ESTAB_COM:
        case IOLINK_DLL_STATE_OPERATE:
            /* In OPERATE, MC must follow negotiated type rules.
             * Type 1/2 MC format: [RW][CommunicationChannel(2bits)][Address(5bits)]
             * Channels 01, 10, 11 are reserved.
             */
            if ((mc & IOLINK_MC_COMM_CHANNEL_MASK) != 0U) {
                return false;
            }
            return true;

        default:
            return false;
    }
}

void iolink_dll_init(iolink_dll_ctx_t *ctx, const iolink_phy_api_t *phy)
{
    if ((phy == NULL) || (!iolink_ctx_zero(ctx, sizeof(iolink_dll_ctx_t)))) {
        return;
    }
    ctx->state = IOLINK_DLL_STATE_STARTUP;
    ctx->phy = phy;
    ctx->last_activity_ms = 0U;
    ctx->wakeup_seen = false;
    ctx->wakeup_deadline_us = 0U;
    ctx->last_cycle_start_us = 0U;
    ctx->min_cycle_time_us = 0U;
    ctx->pd_in_toggle = false;
    ctx->enforce_timing = (IOLINK_TIMING_ENFORCE_DEFAULT != 0U);
    ctx->t_ren_override = false;

    /* Set OD length based on M-sequence type */
    if ((ctx->m_seq_type == IOLINK_M_SEQ_TYPE_2_1) || ctx->m_seq_type == IOLINK_M_SEQ_TYPE_2_2 ||
        ctx->m_seq_type == IOLINK_M_SEQ_TYPE_2_V) {
        ctx->od_len = 2U; /* Type 2 uses 2-byte OD */
    }
    else {
        ctx->od_len = 1U; /* Type 0, 1_x use 1-byte OD */
    }

    /* Initialize variable PD fields for Type 1_V and 2_V */
    if ((ctx->m_seq_type == IOLINK_M_SEQ_TYPE_1_V) || ctx->m_seq_type == IOLINK_M_SEQ_TYPE_2_V) {
        ctx->pd_in_len_current = ctx->pd_in_len;
        ctx->pd_out_len_current = ctx->pd_out_len;
        ctx->pd_in_len_max = ctx->pd_in_len;
        ctx->pd_out_len_max = ctx->pd_out_len;
    }
    else {
        /* For fixed types, current = max = configured */
        ctx->pd_in_len_current = ctx->pd_in_len;
        ctx->pd_out_len_current = ctx->pd_out_len;
        ctx->pd_in_len_max = ctx->pd_in_len;
        ctx->pd_out_len_max = ctx->pd_out_len;
    }

    /* Initialize PHY mode to SDCI (default) */
    ctx->phy_mode = IOLINK_PHY_MODE_SDCI;
    if (ctx->phy->set_mode != NULL) {
        ctx->phy->set_mode(IOLINK_PHY_MODE_SDCI);
    }

    /* Initialize error handling */
    ctx->max_retries = 3U; /* Default: 3 retry attempts */
    ctx->crc_errors = 0U;
    ctx->timeout_errors = 0U;
    ctx->framing_errors = 0U;
    ctx->timing_errors = 0U;
    ctx->t_ren_violations = 0U;
    ctx->t_cycle_violations = 0U;
    ctx->retry_count = 0U;
    ctx->retry_count = 0U;
    ctx->total_retries = 0U;
    ctx->voltage_faults = 0U;
    ctx->short_circuits = 0U;
    ctx->fallback_count = 0U;
    ctx->sio_fallback_threshold = 3U; /* Default: 3 consecutive fallbacks trigger SIO */

    /* Init Sub-modules */
    iolink_events_init(&ctx->events);
    iolink_isdu_init(&ctx->isdu);
    iolink_ds_init(&ctx->ds, NULL);

    /* Link Event context to ISDU context */
    ctx->isdu.event_ctx = &ctx->events;
    ctx->isdu.dll_ctx = ctx;

    /* Initialize baudrate to COM2 (38.4 kbit/s) */
    ctx->baudrate = IOLINK_BAUDRATE_COM2;
    if (ctx->phy->set_baudrate != NULL) {
        ctx->phy->set_baudrate(IOLINK_BAUDRATE_COM2);
    }
    ctx->t_ren_limit_us = dll_get_t_ren_limit_us(ctx);
}

static void handle_startup(iolink_dll_ctx_t *ctx, uint8_t byte)
{
    /* Transition to PREOPERATE on any activity; ignore wake-up dummy byte */
    (void) byte;
    ctx->state = IOLINK_DLL_STATE_PREOPERATE;
    ctx->frame_index = 0U;
    ctx->req_len = IOLINK_M_SEQ_TYPE0_LEN;
}

static void handle_awaiting_comm(iolink_dll_ctx_t *ctx, uint8_t byte)
{
    /* Transition to PREOPERATE and keep the first byte */
    ctx->state = IOLINK_DLL_STATE_PREOPERATE;
    ctx->frame_index = 0U;
    ctx->req_len = IOLINK_M_SEQ_TYPE0_LEN;
    handle_preoperate(ctx, byte);
}

static uint8_t get_req_len(iolink_dll_ctx_t *ctx)
{
    uint8_t len = IOLINK_M_SEQ_TYPE0_LEN;

    switch (ctx->m_seq_type) {
        case IOLINK_M_SEQ_TYPE_0:
            len = IOLINK_M_SEQ_TYPE0_LEN;
            break;
        case IOLINK_M_SEQ_TYPE_1_1:
        case IOLINK_M_SEQ_TYPE_1_2:
            /* Type 1 (fixed): MC + CKT + PD_OUT + OD(1) + CK */
            len = (uint8_t) (IOLINK_M_SEQ_HEADER_LEN + ctx->pd_out_len + 1U + 1U);
            break;
        case IOLINK_M_SEQ_TYPE_1_V:
            /* Type 1_V (variable): MC + CKT + PD_OUT(current) + OD(1) + CK */
            len = (uint8_t) (IOLINK_M_SEQ_HEADER_LEN + ctx->pd_out_len_current + 1U + 1U);
            break;
        case IOLINK_M_SEQ_TYPE_2_1:
        case IOLINK_M_SEQ_TYPE_2_2:
            /* Type 2 (fixed): MC + CKT + PD_OUT + OD(2) + CK */
            len =
                (uint8_t) (IOLINK_M_SEQ_HEADER_LEN + ctx->pd_out_len + (uint8_t) ctx->od_len + 1U);
            break;
        case IOLINK_M_SEQ_TYPE_2_V:
            /* Type 2_V (variable): MC + CKT + PD_OUT(current) + OD(2) + CK */
            len = (uint8_t) (IOLINK_M_SEQ_HEADER_LEN + ctx->pd_out_len_current +
                             (uint8_t) ctx->od_len + 1U);
            break;
        default:
            /* Handled by initialization of len */
            break;
    }
    return len;
}

static void handle_preoperate(iolink_dll_ctx_t *ctx, uint8_t byte)
{
    if (ctx->frame_index == 0U) {
        dll_note_frame_start(ctx);
    }
    ctx->frame_buf[ctx->frame_index++] = byte;

    if (ctx->frame_index >= IOLINK_M_SEQ_TYPE0_LEN) {
        ctx->frame_index = 0U;
        uint8_t mc = ctx->frame_buf[0];
        uint8_t ck = ctx->frame_buf[1];

        if (iolink_checksum_ck(mc, 0U) == ck) {
            /* Handle Transition Command (0x0F) or standard ISDU MC */
            if (mc == IOLINK_MC_TRANSITION_COMMAND) {
                ctx->state = IOLINK_DLL_STATE_ESTAB_COM;
                ctx->req_len = get_req_len(ctx);
                DLL_LOG("State -> ESTAB_COM. req_len=%u, m_seq_type=%u\n", ctx->req_len,
                        ctx->m_seq_type);
                ctx->retry_count = 0U;
            }
            else {
                (void) iolink_isdu_collect_byte(&ctx->isdu, mc);
                iolink_isdu_process(&ctx->isdu);

                uint8_t resp[2];
                resp[0] = 0U;
                (void) iolink_isdu_get_response_byte(&ctx->isdu, &resp[0]);
                /* For Type 0, first byte is just ISDU byte. */
                resp[1] = iolink_checksum_ck(0U, resp[0]);
                dll_send_response(ctx, resp, 2U);
                ctx->retry_count = 0U;
            }
        }
        else {
            ctx->framing_errors++;
            ctx->crc_errors++;
            ctx->retry_count++;
            ctx->total_retries++;
            iolink_event_trigger(&ctx->events, IOLINK_EVENT_COMM_CRC, IOLINK_EVENT_TYPE_WARNING);
            if (ctx->retry_count >= ctx->max_retries) {
                ctx->retry_count = 0U;
                iolink_event_trigger(&ctx->events, IOLINK_EVENT_COMM_CRC, IOLINK_EVENT_TYPE_ERROR);
                enter_fallback(ctx);
            }
        }
    }
}

static bool handle_operate_type0(iolink_dll_ctx_t *ctx)
{
    uint8_t mc = ctx->frame_buf[0];
    uint8_t ck = ctx->frame_buf[1];

    /* Guard: Verify MC is valid for state */
    if (!is_valid_mc_for_state(ctx, mc)) {
        ctx->framing_errors++;
        iolink_event_trigger(&ctx->events, IOLINK_EVENT_COMM_FRAMING, IOLINK_EVENT_TYPE_WARNING);
        return false;
    }

    if (iolink_checksum_ck(mc, 0U) == ck) {
        ctx->retry_count = 0U; /* Reset retry counter on success */
        (void) iolink_isdu_collect_byte(&ctx->isdu, mc);
        iolink_isdu_process(&ctx->isdu);
        uint8_t resp[2];
        resp[0] = 0U;
        (void) iolink_isdu_get_response_byte(&ctx->isdu, &resp[0]);
        uint8_t status = iolink_events_pending(&ctx->events) ? 0x04U : 0U;
        resp[1] = iolink_checksum_ck(status, resp[0]);
        dll_send_response(ctx, resp, 2U);
        return true;
    }
    else {
        /* CRC error in Type 0 */
        ctx->crc_errors++;
        ctx->retry_count++;
        ctx->total_retries++;
        iolink_event_trigger(&ctx->events, IOLINK_EVENT_COMM_CRC, IOLINK_EVENT_TYPE_WARNING);
        if (ctx->retry_count >= ctx->max_retries) {
            ctx->retry_count = 0U;
            iolink_event_trigger(&ctx->events, IOLINK_EVENT_COMM_CRC, IOLINK_EVENT_TYPE_ERROR);
            /* Could trigger error event here */
            enter_fallback(ctx);
        }
    }
    return false;
}

static bool handle_operate_type1_2(iolink_dll_ctx_t *ctx)
{
    uint8_t mc = ctx->frame_buf[0];

    /* Guard: Verify MC is valid for state */
    if (!is_valid_mc_for_state(ctx, mc)) {
        ctx->framing_errors++;
        iolink_event_trigger(&ctx->events, IOLINK_EVENT_COMM_FRAMING, IOLINK_EVENT_TYPE_WARNING);
        return false;
    }

    /* Calculate CRC */
    uint8_t received_ck = ctx->frame_buf[(uint8_t) (ctx->req_len - 1U)];
    uint8_t calculated_ck = iolink_crc6(ctx->frame_buf, (uint8_t) (ctx->req_len - 1U));

    DLL_LOG("Type1/2: Len=%u RecvCK=%02X CalcCK=%02X\n", ctx->req_len, received_ck, calculated_ck);

    if (calculated_ck == received_ck) {
        ctx->retry_count = 0U; /* Reset retry counter on success */
        // DLL_LOG("Type 1/2 Frame Valid. PD_Out len=%u\n", ctx->pd_out_len);

        iolink_critical_enter();
        /* Extract PD (Starts after MC and CKT) */
        if (ctx->pd_out_len > 0U) {
            (void) memcpy(ctx->pd_out, &ctx->frame_buf[2], ctx->pd_out_len);
        }

        /* Extract OD and feed ISDU (OD can be 1 or 2 bytes) */
        uint8_t od_idx = (uint8_t) (IOLINK_M_SEQ_HEADER_LEN + ctx->pd_out_len);

        /* For Type 1 (od_len=1): Feed single OD byte to ISDU */
        /* For Type 2 (od_len=2): Feed first OD byte only */
        uint8_t od = ctx->frame_buf[od_idx];
        (void) iolink_isdu_collect_byte(&ctx->isdu, od);

        /* Run ISDU engine */
        iolink_isdu_process(&ctx->isdu);

        /* Prepare Response: Status + PD_In + OD(od_len bytes) + CK */
        uint8_t resp[IOLINK_PD_IN_MAX_SIZE + 5]; /* Status + PD + OD(2) + CK */
        uint8_t resp_idx = 0U;

        /* Status Byte: [Event(7)] [R(6)] [PDStatus(5)] [ODStatus(4-0)] */
        uint8_t status = 0U;
        if (iolink_events_pending(&ctx->events)) {
            status |= IOLINK_EVENT_BIT_STATUS;
        }
        if (ctx->pd_valid) {
            status |= IOLINK_OD_STATUS_PD_VALID;
        }
        if (ctx->pd_in_toggle) {
            status |= IOLINK_OD_STATUS_PD_TOGGLE;
        }

        resp[resp_idx++] = status;

        if (ctx->pd_in_len > 0U) {
            (void) memcpy(&resp[resp_idx], ctx->pd_in, ctx->pd_in_len);
            resp_idx = (uint8_t) (resp_idx + ctx->pd_in_len);
        }

        /* OD response (1 or 2 bytes based on od_len) */
        uint8_t od_in = 0U;
        (void) iolink_isdu_get_response_byte(&ctx->isdu, &od_in);
        resp[resp_idx++] = od_in;

        /* For Type 2, add second OD byte (reserved, set to 0) */
        if (ctx->od_len == 2U) {
            resp[resp_idx++] = 0x00U;
        }

        /* CK */
        resp[resp_idx] = iolink_crc6(resp, resp_idx);
        resp_idx = (uint8_t) (resp_idx + 1U);

        dll_send_response(ctx, resp, resp_idx);
        iolink_critical_exit();
        return true;
    }
    else {
        /* CRC error in Type 1/2 */
        ctx->crc_errors++;
        ctx->retry_count++;
        ctx->total_retries++;
        if (ctx->retry_count >= ctx->max_retries) {
            ctx->retry_count = 0U;
            iolink_event_trigger(&ctx->events, IOLINK_EVENT_COMM_CRC, IOLINK_EVENT_TYPE_ERROR);
            DLL_LOG("CRC Error Limit Reached! -> Fallback. RecvCK=%02X CalcCK=%02X\n", received_ck,
                    calculated_ck);
            enter_fallback(ctx);
        }
        else {
            DLL_LOG("CRC Error. RecvCK=%02X CalcCK=%02X Retry=%u\n", received_ck, calculated_ck,
                    ctx->retry_count);
        }
    }
    return false;
}

static void handle_operate(iolink_dll_ctx_t *ctx, uint8_t byte)
{
    if (ctx->frame_index == 0U) {
        dll_note_frame_start(ctx);
        DLL_LOG("Operate Frame Start. req_len=%u\n", ctx->req_len);
    }
    ctx->frame_buf[ctx->frame_index++] = byte;

    if (ctx->frame_index >= ctx->req_len) {
        ctx->frame_index = 0U;

        if (ctx->m_seq_type == IOLINK_M_SEQ_TYPE_0) {
            (void) handle_operate_type0(ctx);
        }
        else {
            (void) handle_operate_type1_2(ctx);
        }
    }
}

static void handle_estab_com(iolink_dll_ctx_t *ctx, uint8_t byte)
{
    if (ctx->frame_index == 0U) {
        dll_note_frame_start(ctx);
        DLL_LOG("EstabCom Frame Start. req_len=%u\n", ctx->req_len);
    }
    ctx->frame_buf[ctx->frame_index++] = byte;

    if (ctx->frame_index >= ctx->req_len) {
        ctx->frame_index = 0U;

        bool ok = false;
        if (ctx->m_seq_type == IOLINK_M_SEQ_TYPE_0) {
            ok = handle_operate_type0(ctx);
        }
        else {
            ok = handle_operate_type1_2(ctx);
        }

        if (ok) {
            ctx->retry_count = 0U;
            ctx->state = IOLINK_DLL_STATE_OPERATE;
            DLL_LOG("State -> OPERATE\n");
        }
    }
}

void iolink_dll_process(iolink_dll_ctx_t *ctx)
{
    if ((ctx == NULL) || (ctx->phy == NULL)) {
        return;
    }

    /* Handle fallback state */
    if (ctx->state == IOLINK_DLL_STATE_FALLBACK) {
        /* Only reset to SDCI if we're not in SIO fallback mode */
        if (ctx->phy_mode != IOLINK_PHY_MODE_SIO) {
            if (ctx->phy->set_baudrate != NULL) {
                ctx->phy->set_baudrate(IOLINK_BAUDRATE_COM1);
            }
            ctx->baudrate = IOLINK_BAUDRATE_COM1;
            ctx->t_ren_limit_us = dll_get_t_ren_limit_us(ctx);

            if (ctx->phy->set_mode != NULL) {
                ctx->phy->set_mode(IOLINK_PHY_MODE_SDCI);
            }
            ctx->phy_mode = IOLINK_PHY_MODE_SDCI;
        }
        /* If in SIO mode, stay in SIO - don't auto-recover to SDCI */

        ctx->wakeup_seen = false;
        ctx->wakeup_deadline_us = 0U;
        ctx->last_activity_ms = 0U;
        ctx->last_cycle_start_us = 0U;
        ctx->last_frame_us = 0U;
        ctx->state = IOLINK_DLL_STATE_STARTUP;
    }

    /* Wake-up detection (Global - allowed in any state if frame not started) */
    /* Since we use 0x55 for Wakeup and 0x55 is invalid MC, collision risk is minimal */
    if ((ctx->frame_index == 0U) && (ctx->phy->detect_wakeup != NULL)) {
        int wake = ctx->phy->detect_wakeup();
        if (wake > 0) {
            ctx->wakeup_seen = true;
            ctx->state = IOLINK_DLL_STATE_AWAITING_COMM;
            ctx->wakeup_deadline_us = iolink_time_get_us() + IOLINK_T_DWU_US;
            /* Reset stats/state on wakeup */
            ctx->frame_index = 0U;
            ctx->last_activity_ms = iolink_time_get_ms();
            /* Ensure we are ready for new communication */
            enter_fallback(ctx);
            ctx->state =
                IOLINK_DLL_STATE_AWAITING_COMM; /* Restore AWAITING_COMM after fallback reset */
            return;
        }
    }

    /* Enforce wake-up delay before accepting frames */
    if ((ctx->state == IOLINK_DLL_STATE_AWAITING_COMM) && (ctx->enforce_timing)) {
        uint64_t now_us = iolink_time_get_us();
        if ((ctx->wakeup_deadline_us != 0U) && (now_us < ctx->wakeup_deadline_us)) {
            return;
        }
    }

    uint8_t byte;
    while ((ctx->phy->recv_byte != NULL) && (ctx->phy->recv_byte(&byte) > 0)) {
        ctx->last_activity_ms = iolink_time_get_ms();
        DLL_LOG("Rx %02X State %d Idx %u\n", byte, ctx->state, ctx->frame_index);

        switch (ctx->state) {
            case IOLINK_DLL_STATE_STARTUP:
                handle_startup(ctx, byte);
                break;
            case IOLINK_DLL_STATE_AWAITING_COMM:
                handle_awaiting_comm(ctx, byte);
                break;
            case IOLINK_DLL_STATE_PREOPERATE:
                handle_preoperate(ctx, byte);
                break;
            case IOLINK_DLL_STATE_ESTAB_COM:
                handle_estab_com(ctx, byte);
                break;
            case IOLINK_DLL_STATE_OPERATE:
                /* Reset fallback counter on successful OPERATE - communication is stable */
                if (ctx->fallback_count > 0U) {
                    DLL_LOG("Communication stable. Resetting fallback counter.\n");
                    ctx->fallback_count = 0U;

                    /* If we were in SIO mode and communication is now stable,
                       allow recovery back to SDCI on next master request */
                    if (ctx->phy_mode == IOLINK_PHY_MODE_SIO) {
                        DLL_LOG("SIO mode recovery: transitioning back to SDCI.\n");
                        if (ctx->phy->set_mode != NULL) {
                            ctx->phy->set_mode(IOLINK_PHY_MODE_SDCI);
                        }
                        ctx->phy_mode = IOLINK_PHY_MODE_SDCI;
                    }
                }

                handle_operate(ctx, byte);
                break;
            default:
                ctx->state = IOLINK_DLL_STATE_STARTUP;
                break;
        }
    }

    /* Timeout check: Reset frame assembly if no activity */
    if ((ctx->last_activity_ms != 0U) &&
        ((iolink_time_get_ms() - ctx->last_activity_ms) > IOLINK_DLL_TIMEOUT_MS)) {
        ctx->timeout_errors++;
        iolink_event_trigger(&ctx->events, IOLINK_EVENT_COMM_TIMEOUT, IOLINK_EVENT_TYPE_ERROR);
        enter_fallback(ctx);
        ctx->last_activity_ms = 0U;
        ctx->frame_index = 0U;
    }

    /* Check PHY diagnostics */
    if (ctx->phy->get_voltage_mv != NULL) {
        int mv = ctx->phy->get_voltage_mv();
        /* Standard IO-Link range 18V - 30V. Detailed spec might vary, but this covers tests. */
        if ((mv < 18000) || (mv > 30000)) {
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

/* Variable PD API Functions */

int iolink_dll_set_pd_length(iolink_dll_ctx_t *ctx, uint8_t pd_in_len, uint8_t pd_out_len)
{
    if (ctx == NULL) {
        return -1;
    }

    /* Validate M-sequence type */
    if ((ctx->m_seq_type != IOLINK_M_SEQ_TYPE_1_V) && ctx->m_seq_type != IOLINK_M_SEQ_TYPE_2_V) {
        return -1; /* Only variable types support PD length changes */
    }

    /* Validate PD lengths (2-32 bytes per IO-Link spec) */
    if ((pd_in_len < 2U) || (pd_in_len > 32U) || (pd_out_len < 2U) || (pd_out_len > 32U)) {
        return -1;
    }

    /* Validate against maximum lengths */
    if ((pd_in_len > ctx->pd_in_len_max) || (pd_out_len > ctx->pd_out_len_max)) {
        return -1;
    }

    /* Update current lengths */
    ctx->pd_in_len_current = pd_in_len;
    ctx->pd_out_len_current = pd_out_len;

    /* Recalculate request length */
    ctx->req_len = get_req_len(ctx);

    return 0;
}

void iolink_dll_get_pd_length(const iolink_dll_ctx_t *ctx, uint8_t *pd_in_len, uint8_t *pd_out_len)
{
    if (pd_in_len != NULL) {
        *pd_in_len = ctx->pd_in_len_current;
    }
    if (pd_out_len != NULL) {
        *pd_out_len = ctx->pd_out_len_current;
    }
}

/* SIO Mode API Functions */

int iolink_dll_set_sio_mode(iolink_dll_ctx_t *ctx)
{
    if ((ctx == NULL) || (ctx->phy == NULL)) {
        return -1;
    }

    /* Can only switch modes in OPERATE state */
    if (ctx->state != IOLINK_DLL_STATE_OPERATE) {
        return -1;
    }

    /* Check if PHY supports mode switching */
    if (ctx->phy->set_mode == NULL) {
        return -1; /* PHY doesn't support mode switching */
    }

    /* Switch to SIO mode */
    ctx->phy->set_mode(IOLINK_PHY_MODE_SIO);
    ctx->phy_mode = IOLINK_PHY_MODE_SIO;

    return 0;
}

int iolink_dll_set_sdci_mode(iolink_dll_ctx_t *ctx)
{
    if ((ctx == NULL) || (ctx->phy == NULL)) {
        return -1;
    }

    /* Check if PHY supports mode switching */
    if (ctx->phy->set_mode == NULL) {
        return -1;
    }

    /* Switch to SDCI mode */
    ctx->phy->set_mode(IOLINK_PHY_MODE_SDCI);
    ctx->phy_mode = IOLINK_PHY_MODE_SDCI;

    return 0;
}

iolink_phy_mode_t iolink_dll_get_phy_mode(const iolink_dll_ctx_t *ctx)
{
    if (ctx == NULL) {
        return IOLINK_PHY_MODE_INACTIVE;
    }

    return ctx->phy_mode;
}

int iolink_dll_set_baudrate(iolink_dll_ctx_t *ctx, iolink_baudrate_t baudrate)
{
    if ((ctx == NULL) || (ctx->phy == NULL)) {
        return -1;
    }

    /* Check if PHY supports baudrate switching */
    if (ctx->phy->set_baudrate == NULL) {
        return -1;
    }

    /* Switch baudrate */
    ctx->phy->set_baudrate(baudrate);
    ctx->baudrate = baudrate;
    if (!ctx->t_ren_override) {
        ctx->t_ren_limit_us = dll_get_t_ren_limit_us(ctx);
    }

    return 0;
}

iolink_baudrate_t iolink_dll_get_baudrate(const iolink_dll_ctx_t *ctx)
{
    if (ctx == NULL) {
        return IOLINK_BAUDRATE_COM2; /* Default */
    }

    return ctx->baudrate;
}

void iolink_dll_get_stats(const iolink_dll_ctx_t *ctx, iolink_dll_stats_t *out_stats)
{
    if ((ctx == NULL) || (out_stats == NULL)) {
        return;
    }

    out_stats->crc_errors = ctx->crc_errors;
    out_stats->timeout_errors = ctx->timeout_errors;
    out_stats->framing_errors = ctx->framing_errors;
    out_stats->timing_errors = ctx->timing_errors;
    out_stats->t_ren_violations = ctx->t_ren_violations;
    out_stats->t_cycle_violations = ctx->t_cycle_violations;
    out_stats->total_retries = ctx->total_retries;
    out_stats->voltage_faults = ctx->voltage_faults;
    out_stats->short_circuits = ctx->short_circuits;
}

void iolink_dll_set_timing_enforcement(iolink_dll_ctx_t *ctx, bool enable)
{
    if (ctx == NULL) {
        return;
    }
    ctx->enforce_timing = enable;
}

void iolink_dll_set_t_ren_limit_us(iolink_dll_ctx_t *ctx, uint32_t limit_us)
{
    if (ctx == NULL) {
        return;
    }
    ctx->t_ren_override = true;
    ctx->t_ren_limit_us = limit_us;
}
