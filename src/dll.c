#include "iolinki/iolink.h"
#include "iolinki/dll.h"
#include "iolinki/crc.h"
#include "iolinki/isdu.h"
#include "iolinki/events.h"
#include "iolinki/time_utils.h"
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
                /* Initialize req_len for OPERATE based on config */
                if (ctx->m_seq_type == IOLINK_M_SEQ_TYPE_0) {
                    ctx->req_len = IOLINK_M_SEQ_TYPE0_LEN;
                } else {
                    ctx->req_len = 4 + ctx->pd_out_len;
                }
            } else {
                iolink_isdu_collect_byte(&ctx->isdu, mc);
                
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
            /* Same as Preoperate Type 0 but in OPERATE mode */
            uint8_t mc = ctx->frame_buf[0];
            uint8_t ck = ctx->frame_buf[1];
            if (iolink_checksum_ck(mc, 0) == ck) {
                iolink_isdu_collect_byte(&ctx->isdu, mc);
                uint8_t resp[2];
                iolink_isdu_get_response_byte(&ctx->isdu, &resp[0]);
                uint8_t status = iolink_events_pending(&ctx->events) ? 0x04 : 0;
                resp[1] = iolink_checksum_ck(status, resp[0]);
                ctx->phy->send(resp, 2);
            }
        } else {
            /* Type 1/2 Logic */
            uint8_t received_ck = ctx->frame_buf[ctx->req_len-1];
            uint8_t calculated_ck = iolink_crc6(ctx->frame_buf, ctx->req_len-1);
            
            if (calculated_ck == received_ck) {
                /* Extract PD */
                if (ctx->pd_out_len > 0) {
                    memcpy(ctx->pd_out, &ctx->frame_buf[2], ctx->pd_out_len);
                }
                
                /* Extract OD and feed ISDU */
                uint8_t od = ctx->frame_buf[2 + ctx->pd_out_len];
                iolink_isdu_collect_byte(&ctx->isdu, od);
                
                /* Run ISDU engine */
                iolink_isdu_process(&ctx->isdu);
                
                /* Prepare Response: Status + PD_In + OD + CK */
                uint8_t resp_len = 1 + ctx->pd_in_len + 1 + 1;
                uint8_t resp[48];
                
                uint8_t status = iolink_events_pending(&ctx->events) ? 0x20 : 0;
                resp[0] = status;
                if (ctx->pd_in_len > 0) {
                    memcpy(&resp[1], ctx->pd_in, ctx->pd_in_len);
                }
                
                uint8_t od_in = 0;
                iolink_isdu_get_response_byte(&ctx->isdu, &od_in);
                resp[1 + ctx->pd_in_len] = od_in;
                resp[resp_len-1] = iolink_crc6(resp, resp_len-1);
                
                ctx->phy->send(resp, resp_len);
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
