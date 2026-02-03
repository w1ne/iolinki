/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/isdu.h"
#include "iolinki/crc.h"
#include "iolinki/events.h"
#include "iolinki/device_info.h"
#include "iolinki/params.h"
#include <string.h>

/* 
 * IO-Link ISDU Segmentation Engine
 * 
 * ISDU Header (2 or 3 bytes):
 * [7:4] Service (Read/Write)
 * [3:0] Length (if < 15)
 * [ Index (16-bit) ]
 * [ Subindex (8-bit) ]
 */

/* ... includes ... */

/* Structs moved to header iolink_isdu_ctx_t */

void iolink_isdu_init(iolink_isdu_ctx_t *ctx)
{
    if (ctx) {
        memset(ctx, 0, sizeof(iolink_isdu_ctx_t));
        ctx->state = ISDU_STATE_IDLE;
        ctx->next_state = ISDU_STATE_IDLE;
    }
}

int iolink_isdu_collect_byte(iolink_isdu_ctx_t *ctx, uint8_t byte)
{
    if (!ctx) return 0;

    /* IO-Link V1.1 Control Byte parsing */
    bool start = (byte & 0x80) != 0;
    bool last = (byte & 0x40) != 0;
    uint8_t seq = (byte & 0x3F);

    switch (ctx->state) {
        case ISDU_STATE_IDLE:
            if (!start) return -1; /* Must start with 'Start' bit */
            ctx->is_segmented = !last;
            ctx->segment_seq = 0;
            ctx->error_code = 0;
            ctx->is_response_control_sent = false;
            ctx->buffer_idx = 0;
            ctx->response_len = 0;
            ctx->response_idx = 0;
            ctx->state = ISDU_STATE_HEADER_INITIAL;
            return 0;

        case ISDU_STATE_HEADER_INITIAL:
            {
                uint8_t service = (byte >> 4) & 0x0F;
                uint8_t length = byte & 0x0F;
                
                if (service == 0x09) { /* READ */
                    ctx->header.type = IOLINK_ISDU_SERVICE_READ;
                    ctx->header.length = 0;
                    ctx->next_state = ISDU_STATE_HEADER_INDEX_HIGH;
                } else if (service == 0x0A) { /* WRITE */
                    ctx->header.type = IOLINK_ISDU_SERVICE_WRITE;
                    if (length == 0) {
                        ctx->next_state = ISDU_STATE_HEADER_EXT_LEN;
                    } else {
                        ctx->header.length = length;
                        ctx->next_state = ISDU_STATE_HEADER_INDEX_HIGH;
                    }
                } else {
                    ctx->state = ISDU_STATE_IDLE;
                    return -1;
                }
                ctx->buffer_idx = 0;
                if (ctx->is_segmented) ctx->state = ISDU_STATE_SEGMENT_COLLECT;
                else ctx->state = ctx->next_state;
            }
            break;

        case ISDU_STATE_HEADER_EXT_LEN:
            ctx->header.length = byte;
            ctx->next_state = ISDU_STATE_HEADER_INDEX_HIGH;
            if (ctx->is_segmented) ctx->state = ISDU_STATE_SEGMENT_COLLECT;
            else ctx->state = ctx->next_state;
            break;

        case ISDU_STATE_HEADER_INDEX_HIGH:
            ctx->header.index = (uint16_t)(byte << 8);
            ctx->next_state = ISDU_STATE_HEADER_INDEX_LOW;
            if (ctx->is_segmented) ctx->state = ISDU_STATE_SEGMENT_COLLECT;
            else ctx->state = ctx->next_state;
            break;
            
        case ISDU_STATE_HEADER_INDEX_LOW:
            ctx->header.index |= byte;
            ctx->next_state = ISDU_STATE_HEADER_SUBINDEX;
            if (ctx->is_segmented) ctx->state = ISDU_STATE_SEGMENT_COLLECT;
            else ctx->state = ctx->next_state;
            break;

        case ISDU_STATE_HEADER_SUBINDEX:
            ctx->header.subindex = byte;
            if (ctx->header.type == IOLINK_ISDU_SERVICE_WRITE) {
                ctx->next_state = ISDU_STATE_DATA_COLLECT;
            } else {
                ctx->state = ISDU_STATE_SERVICE_EXECUTE;
                return 1;
            }
            if (ctx->is_segmented) ctx->state = ISDU_STATE_SEGMENT_COLLECT;
            else ctx->state = ctx->next_state;
            break;

        case ISDU_STATE_DATA_COLLECT:
            ctx->buffer[ctx->buffer_idx++] = byte;
            if (ctx->buffer_idx >= ctx->header.length) {
                ctx->state = ISDU_STATE_SERVICE_EXECUTE;
                return 1;
            }
            ctx->next_state = ISDU_STATE_DATA_COLLECT;
            if (ctx->is_segmented) ctx->state = ISDU_STATE_SEGMENT_COLLECT;
            break;
            
        case ISDU_STATE_SEGMENT_COLLECT:
            /* Expecting Control Byte */
            if (start) {
                ctx->state = ISDU_STATE_IDLE;
                return -1;
            }
            if (seq != (uint8_t)((ctx->segment_seq + 1) & 0x3F)) {
                ctx->error_code = 0x81;
                ctx->state = ISDU_STATE_IDLE;
                return -1;
            }
            ctx->segment_seq = seq;
            ctx->is_segmented = !last;
            ctx->state = ctx->next_state;
            break;

        case ISDU_STATE_RESPONSE_READY:
            /* Response is being sent. Collection of NEW requests can only happen after response is fully read. */
            if (start) {
                ctx->state = ISDU_STATE_IDLE;
                return iolink_isdu_collect_byte(ctx, byte);
            }
            return 0;

        default:
            ctx->state = ISDU_STATE_IDLE;
            break;
    }
    return 0;
}

static void handle_mandatory_indices(iolink_isdu_ctx_t *ctx)
{
    const iolink_device_info_t *info = iolink_device_info_get();
    const char *str_data = NULL;
    
    
    switch (ctx->header.index) {
        case 0x0010: str_data = info->vendor_name; break;
        case 0x0011: str_data = info->vendor_text; break;
        case 0x0012: str_data = info->product_name; break;
        case 0x0013: str_data = info->product_id; break;
        case 0x0014: str_data = info->product_text; break;
        case 0x0015: str_data = info->serial_number; break;
        case 0x0016: str_data = info->hardware_revision; break;
        case 0x0017: str_data = info->firmware_revision; break;
        
        case 0x0018: /* Application Tag (optional) */
            if (ctx->header.type == IOLINK_ISDU_SERVICE_WRITE) {
                if (iolink_params_set(0x0018, 0, ctx->buffer, ctx->buffer_idx, true) == 0) {
                     ctx->response_len = 0;
                     ctx->response_idx = 0;
                     ctx->state = ISDU_STATE_RESPONSE_READY;
                     return;
                }
            } else {
                int res = iolink_params_get(0x0018, 0, ctx->response_buf, sizeof(ctx->response_buf));
                if (res >= 0) {
                     ctx->response_len = (uint8_t)res;
                     ctx->response_idx = 0;
                     ctx->state = ISDU_STATE_RESPONSE_READY;
                     return;
                }
            }
            break;
            
        case 0x0024:
            ctx->response_buf[0] = info->min_cycle_time;
            ctx->response_len = 1;
            ctx->response_idx = 0;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;
            
        case 0x001E:
            ctx->response_buf[0] = (uint8_t)(info->revision_id >> 8);
            ctx->response_buf[1] = (uint8_t)(info->revision_id & 0xFF);
            ctx->response_len = 2;
            ctx->response_idx = 0;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;
            
        default: return;
    }
    
    if (str_data) {
        size_t len_sz = strlen(str_data);
        if (len_sz > sizeof(ctx->response_buf)) len_sz = sizeof(ctx->response_buf);
        memcpy(ctx->response_buf, str_data, len_sz);
        ctx->response_len = (uint8_t)len_sz;
        ctx->response_idx = 0;
        ctx->state = ISDU_STATE_RESPONSE_READY;
    }
}

static void handle_system_command(iolink_isdu_ctx_t *ctx, uint8_t cmd)
{
    switch (cmd) {
        case 0x80: iolink_isdu_init(ctx); break;
        case 0x81: break;
        case 0x82: break;
        case 0x83: break;
        default:
            ctx->response_buf[0] = 0x80;
            ctx->response_buf[1] = 0x11;
            ctx->response_len = 2;
            ctx->response_idx = 0;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;
    }
    ctx->response_len = 0;
    ctx->response_idx = 0;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

static uint16_t g_access_locks = 0x0000; /* TODO: Move to ctx or device info */

static void handle_access_locks(iolink_isdu_ctx_t *ctx)
{
    if (ctx->header.type == IOLINK_ISDU_SERVICE_READ) {
        ctx->response_buf[0] = (uint8_t)(g_access_locks >> 8);
        ctx->response_buf[1] = (uint8_t)(g_access_locks & 0xFF);
        ctx->response_len = 2;
    } else {
        ctx->response_len = 0;
    }
    ctx->response_idx = 0;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

static void handle_standard_commands(iolink_isdu_ctx_t *ctx)
{
    if (ctx->header.index == 0x0002) {
        if (ctx->header.type == IOLINK_ISDU_SERVICE_WRITE) {
             /* Mandatory System Commands */
             handle_system_command(ctx, ctx->buffer[0]); 
        } else {
            /* Read of Index 2 - usually blocked or returns last event depending on profile */
            ctx->response_buf[0] = 0x80; /* Error: Service not available */
            ctx->response_buf[1] = 0x11;
            ctx->response_len = 2;
            ctx->response_idx = 0;
            ctx->state = ISDU_STATE_RESPONSE_READY;
        }
    } else if (ctx->header.index == 0x000C) {
        handle_access_locks(ctx);
    } else {
        handle_mandatory_indices(ctx);
    }
}

void iolink_isdu_process(iolink_isdu_ctx_t *ctx)
{
    if (!ctx) return;
    
    if (ctx->state == ISDU_STATE_BUSY) {
        /* In real implementation, we would check if the background task is done.
           For now, we just move to RESPONSE_READY or IDLE. */
        return;
    }

    if (ctx->state == ISDU_STATE_SERVICE_EXECUTE) {
        handle_standard_commands(ctx);
        if (ctx->state != ISDU_STATE_RESPONSE_READY) {
            ctx->state = ISDU_STATE_IDLE;
        }
    }
}

int iolink_isdu_get_response_byte(iolink_isdu_ctx_t *ctx, uint8_t *byte)
{
    if (!ctx) return 0;
    if (ctx->state != ISDU_STATE_RESPONSE_READY) return 0;

    if (!ctx->is_response_control_sent) {
        /* Send Control Byte: [Done(1)] [Error(1)] [Seq(6)] */
        /* For now, assume Done=1 when response_idx reaches response_len */
        /* Actually, Start/Last bits are more standard for V1.1 requests/responses. */
        uint8_t ctrl = 0x00;
        if (ctx->response_idx == 0) {
             ctrl |= 0x80; /* Start */
        }
        if (ctx->error_code != 0) {
             ctrl |= 0x40; /* Error bit if using specific framing, or just Last */
        }
        
        /* Last bit if this is the final segment */
        if (ctx->response_idx + 1 >= ctx->response_len) {
            ctrl |= 0x40; /* Last */
        }
        /* Sequence number */
        ctrl |= (ctx->segment_seq & 0x3F);
        
        *byte = ctrl;
        ctx->is_response_control_sent = true;
        return 1;
    }

    if (ctx->response_idx < ctx->response_len) {
        *byte = ctx->response_buf[ctx->response_idx++];
        if (ctx->response_idx >= ctx->response_len) {
            ctx->state = ISDU_STATE_IDLE;
        } else {
            /* Mandatory for V1.1.5 on OD=1: Every byte is preceded by Control Byte. */
            ctx->is_response_control_sent = false;
            ctx->segment_seq = (ctx->segment_seq + 1) & 0x3F;
        }
        return 1;
    }

    ctx->state = ISDU_STATE_IDLE;
    return 0;
}
