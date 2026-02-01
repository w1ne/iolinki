#include "iolinki/dll.h"
#include "iolinki/crc.h"
#include "dll_internal.h"
#include <stddef.h>

void iolink_dll_init(iolink_dll_ctx_t *ctx, const iolink_phy_api_t *phy)
{
    if (ctx == NULL) return;
    ctx->state = IOLINK_DLL_STATE_STARTUP;
    ctx->phy = phy;
    ctx->last_activity_ms = 0;
}

static void handle_startup(iolink_dll_ctx_t *ctx)
{
    uint8_t byte;
    /* In a real scenario, we wait for a Wake-up or baudrate detection.
       For the reference stack, we assume the Master sends bytes. */
    if (ctx->phy->recv_byte(&byte) > 0) {
        /* Transition to PREOPERATE on first byte received (simplification) */
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
            /* Validate M-Sequence Type 0 */
            uint8_t mc = buf[0];
            uint8_t ck = buf[1];
            
            if (iolink_checksum_ck(mc, 0) == ck) { /* Simplistic check */
                /* Reply with Device Response */
                uint8_t response[2] = {0x00, 0x00}; /* Placeholder */
                ctx->phy->send(response, 2);
            }
        }
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
            /* TODO: Cyclic data exchange */
            break;
    }
}
