/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/data_storage.h"
#include "iolinki/utils.h"

void iolink_ds_init(iolink_ds_ctx_t *ctx, const iolink_ds_storage_api_t *storage)
{
    if (!iolink_ctx_zero(ctx, sizeof(iolink_ds_ctx_t))) {
        return;
    }
    ctx->storage = storage;
    ctx->state = IOLINK_DS_STATE_IDLE;
}

uint16_t iolink_ds_calc_checksum(const uint8_t *data, size_t len)
{
    /* Fletcher-16 or simple sum for demo. IO-Link usually uses a specific CRC. */
    uint16_t sum1 = 0U;
    uint16_t sum2 = 0U;
    if (!iolink_buf_is_valid(data, len)) {
        return 0U;
    }
    for (size_t i = 0U; i < len; ++i) {
        sum1 = (uint16_t)((sum1 + (uint16_t)data[i]) % 255U);
        sum2 = (uint16_t)((sum2 + sum1) % 255U);
    }
    return (uint16_t)((sum2 << 8U) | sum1);
}

void iolink_ds_check(iolink_ds_ctx_t *ctx, uint16_t master_checksum)
{
    if (ctx == NULL) {
        return;
    }
    
    ctx->master_checksum = master_checksum;
    
    if (ctx->state != IOLINK_DS_STATE_IDLE) {
        return;
    }

    if (master_checksum == 0U) {
        /* Master has no data -> Upload request */
        ctx->state = IOLINK_DS_STATE_UPLOAD_REQ;
    } else if (master_checksum != ctx->current_checksum) {
        /* Checksum mismatch -> Download request (Update device) */
        ctx->state = IOLINK_DS_STATE_DOWNLOAD_REQ;
    }
}

void iolink_ds_process(iolink_ds_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    
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
            ctx->state = IOLINK_DS_STATE_IDLE;
            break;
    }
}
