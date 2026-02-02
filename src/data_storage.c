#include "iolinki/data_storage.h"
#include <string.h>

void iolink_ds_init(iolink_ds_ctx_t *ctx, const iolink_ds_storage_api_t *storage)
{
    if (!ctx) return;
    memset(ctx, 0, sizeof(iolink_ds_ctx_t));
    ctx->storage = storage;
    ctx->state = IOLINK_DS_STATE_IDLE;
}

uint16_t iolink_ds_calc_checksum(const uint8_t *data, size_t len)
{
    /* Fletcher-16 or simple sum for demo. IO-Link usually uses a specific CRC. */
    uint16_t sum1 = 0;
    uint16_t sum2 = 0;
    for (size_t i = 0; i < len; ++i) {
        sum1 = (uint16_t)((sum1 + data[i]) % 255);
        sum2 = (uint16_t)((sum2 + sum1) % 255);
    }
    return (uint16_t)((sum2 << 8) | sum1);
}

void iolink_ds_check(iolink_ds_ctx_t *ctx, uint16_t master_checksum)
{
    if (!ctx) return;
    
    ctx->master_checksum = master_checksum;
    
    if (ctx->state != IOLINK_DS_STATE_IDLE) return;

    if (master_checksum == 0) {
        /* Master has no data -> Upload request */
        ctx->state = IOLINK_DS_STATE_UPLOAD_REQ;
    } else if (master_checksum != ctx->current_checksum) {
        /* Checksum mismatch -> Download request (Update device) */
        ctx->state = IOLINK_DS_STATE_DOWNLOAD_REQ;
    }
}

void iolink_ds_process(iolink_ds_ctx_t *ctx)
{
    if (!ctx) return;
    
    switch (ctx->state) {
        case IOLINK_DS_STATE_UPLOAD_REQ:
            /* Master indicated it has no data -> Device sends parameters */
            /* Byte-by-byte transfer would happen here */
            ctx->state = IOLINK_DS_STATE_UPLOADING;
            break;

        case IOLINK_DS_STATE_UPLOADING:
            /* Complete upload simulation */
            ctx->state = IOLINK_DS_STATE_IDLE;
            break;
            
        case IOLINK_DS_STATE_DOWNLOAD_REQ:
            /* Master indicated a mismatch -> Device receives parameters */
            ctx->state = IOLINK_DS_STATE_DOWNLOADING;
            break;

        case IOLINK_DS_STATE_DOWNLOADING:
            /* Update local parameters and storage */
            ctx->current_checksum = ctx->master_checksum;
            ctx->state = IOLINK_DS_STATE_IDLE;
            break;

        default:
            break;
    }
}
