/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/data_storage.h"
#include "iolinki/utils.h"

void iolink_ds_init(iolink_ds_ctx_t* ctx, const iolink_ds_storage_api_t* storage)
{
    if (!iolink_ctx_zero(ctx, sizeof(iolink_ds_ctx_t))) {
        return;
    }
    ctx->storage = storage;
    ctx->state = IOLINK_DS_STATE_IDLE;
}

uint16_t iolink_ds_calc_checksum(const uint8_t* data, size_t len)
{
    /* Fletcher-16 or simple sum for demo. IO-Link usually uses a specific CRC. */
    uint16_t sum1 = 0U;
    uint16_t sum2 = 0U;
    if (!iolink_buf_is_valid(data, len)) {
        return 0U;
    }
    for (size_t i = 0U; i < len; ++i) {
        sum1 = (uint16_t) ((sum1 + (uint16_t) data[i]) % 255U);
        sum2 = (uint16_t) ((sum2 + sum1) % 255U);
    }
    return (uint16_t) ((sum2 << 8U) | sum1);
}

void iolink_ds_check(iolink_ds_ctx_t* ctx, uint16_t master_checksum)
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
    }
    else if (master_checksum != ctx->current_checksum) {
        /* Checksum mismatch -> Download request (Update device) */
        ctx->state = IOLINK_DS_STATE_DOWNLOAD_REQ;
    }
}

void iolink_ds_process(iolink_ds_ctx_t* ctx)
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

int iolink_ds_start_upload(iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return -1;
    }

    if (ctx->state != IOLINK_DS_STATE_IDLE) {
        return -1; /* Busy */
    }

    ctx->state = IOLINK_DS_STATE_UPLOAD_REQ;
    return 0;
}

int iolink_ds_start_download(iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return -1;
    }

    if (ctx->state != IOLINK_DS_STATE_IDLE) {
        return -1; /* Busy */
    }

    ctx->state = IOLINK_DS_STATE_DOWNLOAD_REQ;
    return 0;
}

int iolink_ds_abort(iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return -1;
    }

    /* Abort any active DS operation */
    ctx->state = IOLINK_DS_STATE_IDLE;
    return 0;
}

int iolink_ds_handle_command(iolink_ds_ctx_t* ctx, uint8_t cmd, uint16_t access_locks)
{
    if (ctx == NULL) {
        return -1;
    }

    /* Check Access Locks for Download Commands (Write to Device) */
    if ((cmd == IOLINK_CMD_PARAM_DOWNLOAD_START) || (cmd == IOLINK_CMD_PARAM_DOWNLOAD_END)) {
        if ((access_locks & IOLINK_LOCK_DS) != 0U) {
            /* DS Logic is locked */
            return -2; /* Signal Access Denied (user should map to ISDU error) */
        }
    }

    switch (cmd) {
        case IOLINK_CMD_PARAM_UPLOAD_START: /* 0x07 */
            /* Master wants to read parameters (Upload) */
            if (ctx->state != IOLINK_DS_STATE_IDLE) return -1; /* Busy */
            ctx->state = IOLINK_DS_STATE_UPLOAD_REQ;
            break;

        case IOLINK_CMD_PARAM_UPLOAD_END: /* 0x08 */
            /* Finish upload */
            if (ctx->state == IOLINK_DS_STATE_UPLOADING) {
                ctx->state = IOLINK_DS_STATE_IDLE;
            }
            break;

        case IOLINK_CMD_PARAM_DOWNLOAD_START: /* 0x05 */
            /* Master wants to write parameters (Download) */
            if (ctx->state != IOLINK_DS_STATE_IDLE) return -1; /* Busy */
            ctx->state = IOLINK_DS_STATE_DOWNLOAD_REQ;
            break;

        case IOLINK_CMD_PARAM_DOWNLOAD_END: /* 0x06 */
            /* Finish download */
            if (ctx->state == IOLINK_DS_STATE_DOWNLOADING) {
                ctx->current_checksum = ctx->master_checksum;
                ctx->state = IOLINK_DS_STATE_IDLE;
            }
            break;

        case IOLINK_CMD_PARAM_BREAK: /* 0x97 / Standard Break */
            return iolink_ds_abort(ctx);

        default:
            return -3; /* Unknown command */
    }

    return 0;
}
