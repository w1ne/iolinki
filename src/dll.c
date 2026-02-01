#include "iolinki/dll.h"
#include "iolinki/crc.h"
#include "iolinki/isdu.h"
#include "dll_internal.h"
#include <string.h>

void iolink_dll_init(iolink_dll_ctx_t *ctx, const iolink_phy_api_t *phy)
{
    if (ctx == NULL) return;
    memset(ctx, 0, sizeof(iolink_dll_ctx_t));
    ctx->state = IOLINK_DLL_STATE_STARTUP;
    ctx->phy = phy;
    iolink_isdu_init();
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

    while (ctx->phy->recv_byte(&byte) > 0) {
        buf[index++] = byte;
        if (index >= IOLINK_M_SEQ_TYPE0_LEN) {
            index = 0;
            uint8_t mc = buf[0];
            uint8_t ck = buf[1];
            
            if (iolink_checksum_ck(mc, 0) == ck) {
                /* Hand over MC to ISDU engine (simplified) */
                iolink_isdu_collect_byte(mc);

                uint8_t response[2] = {0, 0};
                iolink_isdu_get_response_byte(&response[0]);
                response[1] = iolink_checksum_ck(0, response[0]);

                ctx->phy->send(response, 2);
                
                /* Keep in PREOPERATE or move to OPERATE */
            }
        }
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
