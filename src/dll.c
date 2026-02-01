#include "iolinki/dll.h"
#include "iolinki/crc.h"
#include "iolinki/isdu.h"
#include "iolinki/events.h"
#include "iolinki/time_utils.h"
#include "dll_internal.h"
#include <string.h>
#include <stdio.h>

void iolink_dll_init(iolink_dll_ctx_t *ctx, const iolink_phy_api_t *phy)
{
    if (ctx == NULL) return;
    memset(ctx, 0, sizeof(iolink_dll_ctx_t));
    ctx->state = IOLINK_DLL_STATE_STARTUP;
    ctx->phy = phy;
    iolink_isdu_init();
    iolink_events_init();
}

static void handle_startup(iolink_dll_ctx_t *ctx)
{
    uint8_t byte;
    if (ctx->phy->recv_byte(&byte) > 0) {
        ctx->state = IOLINK_DLL_STATE_PREOPERATE;
    }
}

static void handle_preoperate(iolink_dll_ctx_t *ctx)
{
    uint8_t buf[IOLINK_M_SEQ_TYPE0_LEN];
    static uint8_t index = 0;
    uint8_t byte;
    static uint64_t last_frame_us = 0;

    while (ctx->phy->recv_byte(&byte) > 0) {
        uint64_t now = iolink_time_get_us();
        
        /* Measure t_A (Master to Device delay) on first byte of frame */
        if (index == 0 && last_frame_us != 0) {
            uint64_t t_a = now - last_frame_us;
            /* In a real stack, check t_a vs requested cycle time */
            (void)t_a; 
        }

        buf[index++] = byte;
        if (index >= IOLINK_M_SEQ_TYPE0_LEN) {
            index = 0;
            last_frame_us = now;
            uint8_t mc = buf[0];
            uint8_t ck = buf[1];
            
            if (iolink_checksum_ck(mc, 0) == ck) {
                /* Hand over MC to ISDU engine (simplified) */
                iolink_isdu_collect_byte(mc);

                uint8_t response[2] = {0, 0};
                iolink_isdu_get_response_byte(&response[0]);
                
                /* Set Event bit (bit 2 of Status/CKT) if pending */
                uint8_t status = 0;
                if (iolink_events_pending()) {
                    status |= 0x04;
                }
                
                response[1] = iolink_checksum_ck(status, response[0]);

                ctx->phy->send(response, 2);
            } else {
                /* CRC Error: Signal via event or reset state machine */
                iolink_event_trigger(0x5000, IOLINK_EVENT_TYPE_ERROR); /* 0x5000: Device software error (placeholder for frame error) */
            }
        }
    }

    /* Timeout check: If no data received for too long, reset to STARTUP */
    if (ctx->last_activity_ms != 0 && (iolink_time_get_ms() - ctx->last_activity_ms > 1000)) {
        printf("[DLL] Communication timeout detected, resetting to STARTUP\n");
        ctx->state = IOLINK_DLL_STATE_STARTUP;
        ctx->last_activity_ms = 0;
    }
}

static void handle_operate(iolink_dll_ctx_t *ctx)
{
    uint8_t byte;
    
    /* 
     * In OPERATE state, we perform cyclic PD exchange.
     * Simplification: Master sends 1 byte PD, Device responds with 1 byte PD.
     */
    if (ctx->phy->recv_byte(&byte) > 0) {
        ctx->pd_out[0] = byte;
        ctx->pd_out_len = 1;
        
        uint8_t resp = ctx->pd_in[0];
        ctx->phy->send(&resp, 1);
    }
}

void iolink_dll_process(iolink_dll_ctx_t *ctx)
{
    if (ctx == NULL || ctx->phy == NULL) return;

    switch (ctx->state) {
        case IOLINK_DLL_STATE_STARTUP:
            handle_startup(ctx);
            break;
        case IOLINK_DLL_STATE_PREOPERATE:
            handle_preoperate(ctx);
            break;
        case IOLINK_DLL_STATE_OPERATE:
            handle_operate(ctx);
            break;
    }
}
