/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/iolink.h"
#include "iolinki/dll.h"
#include "iolinki/crc.h"
#include "iolinki/isdu.h"
#include "iolinki/events.h"
#include "iolinki/time_utils.h"
#include "iolinki/platform.h"
#include "dll_internal.h"
#include <string.h>
#include <stdio.h>

/* Helper to calculate checksum for Type 0 */
/* Helper to calculate checksum for Type 0 - inlined or used directly */

void iolink_dll_init(iolink_dll_ctx_t *ctx, const iolink_phy_api_t *phy)
{
    memset(ctx, 0, sizeof(iolink_dll_ctx_t));
    ctx->state = IOLINK_DLL_STATE_STARTUP;
    ctx->phy = phy;
    ctx->last_activity_ms = 0;
    
    /* Set OD length based on M-sequence type */
    if (ctx->m_seq_type == IOLINK_M_SEQ_TYPE_2_1 || 
        ctx->m_seq_type == IOLINK_M_SEQ_TYPE_2_2) {
        ctx->od_len = 2;  /* Type 2 uses 2-byte OD */
    } else {
        ctx->od_len = 1;  /* Type 0, 1, and 2_V use 1-byte OD */
    }
    
    /* Initialize error handling */
    ctx->max_retries = 3;  /* Default: 3 retry attempts */
    ctx->crc_errors = 0;
    ctx->timeout_errors = 0;
    ctx->framing_errors = 0;
    ctx->retry_count = 0;
    
    /* Init Sub-modules */
    iolink_events_init(&ctx->events);
    iolink_isdu_init(&ctx->isdu);
    iolink_ds_init(&ctx->ds, NULL);
    
    /* Link Event context to ISDU context */
    ctx->isdu.event_ctx = &ctx->events;
}


static void handle_startup(iolink_dll_ctx_t *ctx, uint8_t byte)
{
    /* Transition to PREOPERATE on any activity */
    (void)byte;
    ctx->state = IOLINK_DLL_STATE_PREOPERATE;
    ctx->frame_index = 0;
    ctx->req_len = IOLINK_M_SEQ_TYPE0_LEN;
}

static uint8_t get_req_len(iolink_dll_ctx_t *ctx)
{
    switch (ctx->m_seq_type) {
        case IOLINK_M_SEQ_TYPE_0:
            return IOLINK_M_SEQ_TYPE0_LEN;
        case IOLINK_M_SEQ_TYPE_1_1:
        case IOLINK_M_SEQ_TYPE_1_2:
        case IOLINK_M_SEQ_TYPE_1_V:
            /* Type 1: MC + CKT + PD_OUT + OD(1) + CK */
            return 2 + ctx->pd_out_len + 1 + 1;
        case IOLINK_M_SEQ_TYPE_2_1:
        case IOLINK_M_SEQ_TYPE_2_2:
        case IOLINK_M_SEQ_TYPE_2_V:
            /* Type 2: MC + CKT + PD_OUT + OD(ctx->od_len) + CK */
            return 2 + ctx->pd_out_len + ctx->od_len + 1;
        default:
            return IOLINK_M_SEQ_TYPE0_LEN;
    }
}

static void handle_preoperate(iolink_dll_ctx_t *ctx, uint8_t byte)
{
    ctx->frame_buf[ctx->frame_index++] = byte;
    
    if (ctx->frame_index >= IOLINK_M_SEQ_TYPE0_LEN) {
        ctx->frame_index = 0;
        uint8_t mc = ctx->frame_buf[0];
        uint8_t ck = ctx->frame_buf[1];
        
        if (iolink_checksum_ck(mc, 0) == ck) {
            /* Handle Transition Command (0x0F) or standard ISDU MC */
            if (mc == 0x0F) {
                ctx->state = IOLINK_DLL_STATE_OPERATE;
                ctx->req_len = get_req_len(ctx);
            } else {
                iolink_isdu_collect_byte(&ctx->isdu, mc);
                iolink_isdu_process(&ctx->isdu);
                
                uint8_t resp[2];
                iolink_isdu_get_response_byte(&ctx->isdu, &resp[0]);
                uint8_t status = iolink_events_pending(&ctx->events) ? 0x04 : 0;
                resp[1] = iolink_checksum_ck(status, resp[0]);
                ctx->phy->send(resp, 2);
            }
        }
    }
}

static void handle_operate(iolink_dll_ctx_t *ctx, uint8_t byte)
{
    ctx->frame_buf[ctx->frame_index++] = byte;
    
    if (ctx->frame_index >= ctx->req_len) {
        ctx->frame_index = 0;
        
        if (ctx->m_seq_type == IOLINK_M_SEQ_TYPE_0) {
            uint8_t mc = ctx->frame_buf[0];
            uint8_t ck = ctx->frame_buf[1];
            if (iolink_checksum_ck(mc, 0) == ck) {
                ctx->retry_count = 0;  /* Reset retry counter on success */
                iolink_isdu_collect_byte(&ctx->isdu, mc);
                iolink_isdu_process(&ctx->isdu);
                uint8_t resp[2];
                iolink_isdu_get_response_byte(&ctx->isdu, &resp[0]);
                uint8_t status = iolink_events_pending(&ctx->events) ? 0x04 : 0;
                resp[1] = iolink_checksum_ck(status, resp[0]);
                ctx->phy->send(resp, 2);
            } else {
                /* CRC error in Type 0 */
                ctx->crc_errors++;
                ctx->retry_count++;
                if (ctx->retry_count >= ctx->max_retries) {
                    ctx->retry_count = 0;
                    /* Could trigger error event here */
                }
            }
        } else {
            /* Type 1/2 Logic */
            uint8_t received_ck = ctx->frame_buf[ctx->req_len-1];
            uint8_t calculated_ck = iolink_crc6(ctx->frame_buf, ctx->req_len-1);
            
            if (calculated_ck == received_ck) {
                ctx->retry_count = 0;  /* Reset retry counter on success */
                
                iolink_critical_enter();
                /* Extract PD (Starts after MC and CKT) */
                if (ctx->pd_out_len > 0) {
                    memcpy(ctx->pd_out, &ctx->frame_buf[2], ctx->pd_out_len);
                }
                
                /* Extract OD and feed ISDU (OD can be 1 or 2 bytes) */
                uint8_t od_idx = 2 + ctx->pd_out_len;
                
                /* For Type 1 (od_len=1): Feed single OD byte to ISDU */
                /* For Type 2 (od_len=2): Feed first OD byte only (second byte reserved for future use) */
                uint8_t od = ctx->frame_buf[od_idx];
                iolink_isdu_collect_byte(&ctx->isdu, od);
                
                /* Run ISDU engine */
                iolink_isdu_process(&ctx->isdu);
                
                /* Prepare Response: Status + PD_In + OD(od_len bytes) + CK */
                uint8_t resp[IOLINK_PD_IN_MAX_SIZE + 5];  /* Status + PD + OD(2) + CK */
                uint8_t resp_idx = 0;
                
                /* Status Byte: [Event(7)] [R(6)] [PDStatus(5)] [ODStatus(4-0)] */
                uint8_t status = 0;
                if (iolink_events_pending(&ctx->events)) status |= 0x80;
                if (ctx->pd_valid) status |= 0x20;
                
                resp[resp_idx++] = status;
                
                if (ctx->pd_in_len > 0) {
                    memcpy(&resp[resp_idx], ctx->pd_in, ctx->pd_in_len);
                    resp_idx += ctx->pd_in_len;
                }
                
                /* OD response (1 or 2 bytes based on od_len) */
                uint8_t od_in = 0;
                iolink_isdu_get_response_byte(&ctx->isdu, &od_in);
                resp[resp_idx++] = od_in;
                
                /* For Type 2, add second OD byte (reserved, set to 0) */
                if (ctx->od_len == 2) {
                    resp[resp_idx++] = 0x00;
                }
                
                /* CK */
                resp[resp_idx] = iolink_crc6(resp, resp_idx);
                resp_idx++;
                
                ctx->phy->send(resp, resp_idx);
                iolink_critical_exit();
            } else {
                /* CRC error in Type 1/2 */
                ctx->crc_errors++;
                ctx->retry_count++;
                if (ctx->retry_count >= ctx->max_retries) {
                    ctx->retry_count = 0;
                    /* Could trigger error event or state transition */
                }
            }
        }
    }
}

void iolink_dll_process(iolink_dll_ctx_t *ctx)
{
    if (ctx == NULL || ctx->phy == NULL) return;

    uint8_t byte;
    while (ctx->phy->recv_byte(&byte) > 0) {
        ctx->last_activity_ms = iolink_time_get_ms();
        
        switch (ctx->state) {
            case IOLINK_DLL_STATE_STARTUP:
                handle_startup(ctx, byte);
                break;
            case IOLINK_DLL_STATE_PREOPERATE:
                handle_preoperate(ctx, byte);
                break;
            case IOLINK_DLL_STATE_OPERATE:
                handle_operate(ctx, byte);
                break;
        }
    }

    /* Timeout check: Reset frame assembly if no activity */
    if (ctx->last_activity_ms != 0 && (iolink_time_get_ms() - ctx->last_activity_ms > 1000)) {
        ctx->state = IOLINK_DLL_STATE_STARTUP;
        ctx->last_activity_ms = 0;
        ctx->frame_index = 0;
    }
}
