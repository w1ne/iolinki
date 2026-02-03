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
        ctx->m_seq_type == IOLINK_M_SEQ_TYPE_2_2 ||
        ctx->m_seq_type == IOLINK_M_SEQ_TYPE_2_V) {
        ctx->od_len = 2;  /* Type 2 uses 2-byte OD */
    } else {
        ctx->od_len = 1;  /* Type 0, 1_x use 1-byte OD */
    }
    
    /* Initialize variable PD fields for Type 1_V and 2_V */
    if (ctx->m_seq_type == IOLINK_M_SEQ_TYPE_1_V ||
        ctx->m_seq_type == IOLINK_M_SEQ_TYPE_2_V) {
        ctx->pd_in_len_current = ctx->pd_in_len;
        ctx->pd_out_len_current = ctx->pd_out_len;
        ctx->pd_in_len_max = ctx->pd_in_len;
        ctx->pd_out_len_max = ctx->pd_out_len;
    } else {
        /* For fixed types, current = max = configured */
        ctx->pd_in_len_current = ctx->pd_in_len;
        ctx->pd_out_len_current = ctx->pd_out_len;
        ctx->pd_in_len_max = ctx->pd_in_len;
        ctx->pd_out_len_max = ctx->pd_out_len;
    }
    
    /* Initialize PHY mode to SDCI (default) */
    ctx->phy_mode = IOLINK_PHY_MODE_SDCI;
    if (ctx->phy->set_mode) {
        ctx->phy->set_mode(IOLINK_PHY_MODE_SDCI);
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
            /* Type 1 (fixed): MC + CKT + PD_OUT + OD(1) + CK */
            return 2 + ctx->pd_out_len + 1 + 1;
        case IOLINK_M_SEQ_TYPE_1_V:
            /* Type 1_V (variable): MC + CKT + PD_OUT(current) + OD(1) + CK */
            return 2 + ctx->pd_out_len_current + 1 + 1;
        case IOLINK_M_SEQ_TYPE_2_1:
        case IOLINK_M_SEQ_TYPE_2_2:
            /* Type 2 (fixed): MC + CKT + PD_OUT + OD(2) + CK */
            return 2 + ctx->pd_out_len + ctx->od_len + 1;
        case IOLINK_M_SEQ_TYPE_2_V:
            /* Type 2_V (variable): MC + CKT + PD_OUT(current) + OD(2) + CK */
            return 2 + ctx->pd_out_len_current + ctx->od_len + 1;
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

/* Variable PD API Functions */

int iolink_dll_set_pd_length(iolink_dll_ctx_t *ctx, uint8_t pd_in_len, uint8_t pd_out_len)
{
    /* Validate M-sequence type */
    if (ctx->m_seq_type != IOLINK_M_SEQ_TYPE_1_V &&
        ctx->m_seq_type != IOLINK_M_SEQ_TYPE_2_V) {
        return -1;  /* Only variable types support PD length changes */
    }
    
    /* Validate PD lengths (2-32 bytes per IO-Link spec) */
    if (pd_in_len < 2 || pd_in_len > 32 || pd_out_len < 2 || pd_out_len > 32) {
        return -1;
    }
    
    /* Validate against maximum lengths */
    if (pd_in_len > ctx->pd_in_len_max || pd_out_len > ctx->pd_out_len_max) {
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
    if (pd_in_len) {
        *pd_in_len = ctx->pd_in_len_current;
    }
    if (pd_out_len) {
        *pd_out_len = ctx->pd_out_len_current;
    }
}

/* SIO Mode API Functions */

int iolink_dll_set_sio_mode(iolink_dll_ctx_t *ctx)
{
    if (!ctx || !ctx->phy) {
        return -1;
    }
    
    /* Can only switch modes in OPERATE state */
    if (ctx->state != IOLINK_DLL_STATE_OPERATE) {
        return -1;
    }
    
    /* Check if PHY supports mode switching */
    if (!ctx->phy->set_mode) {
        return -1;  /* PHY doesn't support mode switching */
    }
    
    /* Switch to SIO mode */
    ctx->phy->set_mode(IOLINK_PHY_MODE_SIO);
    ctx->phy_mode = IOLINK_PHY_MODE_SIO;
    
    return 0;
}

int iolink_dll_set_sdci_mode(iolink_dll_ctx_t *ctx)
{
    if (!ctx || !ctx->phy) {
        return -1;
    }
    
    /* Check if PHY supports mode switching */
    if (!ctx->phy->set_mode) {
        return -1;
    }
    
    /* Switch to SDCI mode */
    ctx->phy->set_mode(IOLINK_PHY_MODE_SDCI);
    ctx->phy_mode = IOLINK_PHY_MODE_SDCI;
    
    return 0;
}

iolink_phy_mode_t iolink_dll_get_phy_mode(const iolink_dll_ctx_t *ctx)
{
    if (!ctx) {
        return IOLINK_PHY_MODE_INACTIVE;
    }
    
    return ctx->phy_mode;
}
