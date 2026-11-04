/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file data_storage.c
 * @brief IO-Link Data Storage (DS) parameter server.
 * @ingroup iolinki_data_storage
 *
 * The Master keeps an opaque backup of a device's parameters so a replacement
 * unit can be restored automatically. The device serializes its DS-backed
 * parameters into an image (DS_Data, index 0x0003) for upload, and applies an
 * image received from the Master on download. Each record is laid out as:
 *   [Index(2, big-endian)][Subindex(1)][Length(1)][Data(Length)]
 */

#include "iolinki/data_storage.h"
#include "iolinki/params.h"
#include "iolinki/utils.h"
#include <string.h>

/* Parameters included in the Data Storage set (V1.1.5 device identification
   parameters that are writable and therefore worth backing up). */
static const struct
{
    uint16_t index;
    uint8_t subindex;
    uint8_t max_len; /* Table B.8 StringT limit (B.2.16-B.2.18: max 32 octets) */
} k_ds_params[] = {
    {IOLINK_IDX_APPLICATION_TAG, 0U, 32U},
    {IOLINK_IDX_FUNCTION_TAG, 0U, 32U},
    {IOLINK_IDX_LOCATION_TAG, 0U, 32U},
};

#define DS_PARAM_COUNT (sizeof(k_ds_params) / sizeof(k_ds_params[0]))
#define DS_RECORD_HEADER 4U /* index(2) + subindex(1) + length(1) */
#define DS_PARAM_VALUE_MAX 64U

/** @brief Match a DS-set parameter address; -1 when it is outside the set. */
static int ds_param_find(uint16_t index, uint8_t subindex)
{
    for (size_t i = 0U; i < DS_PARAM_COUNT; i++) {
        if ((k_ds_params[i].index == index) && (k_ds_params[i].subindex == subindex)) {
            return (int) i;
        }
    }
    return -1;
}

/** @brief True when @p index belongs to the DS set with a different subindex. */
static bool ds_param_misaddressed(uint16_t index, uint8_t subindex)
{
    for (size_t i = 0U; i < DS_PARAM_COUNT; i++) {
        if ((k_ds_params[i].index == index) && (k_ds_params[i].subindex != subindex)) {
            return true;
        }
    }
    return false;
}

void iolink_ds_init(iolink_ds_ctx_t* ctx, const iolink_ds_storage_api_t* storage)
{
    if (!iolink_ctx_zero(ctx, sizeof(iolink_ds_ctx_t))) {
        return;
    }
    ctx->storage = storage;
    ctx->state = IOLINK_DS_STATE_IDLE;
}

void iolink_ds_bind_params(iolink_ds_ctx_t* ctx, iolink_params_ctx_t* params_ctx)
{
    if (ctx != NULL) {
        ctx->params_ctx = params_ctx;
    }
}

/** @brief Read a parameter via the bound params context, or the legacy global set. */
static int ds_params_get(const iolink_ds_ctx_t* ctx, uint16_t index, uint8_t subindex,
                         uint8_t* buffer, size_t max_len)
{
    if ((ctx != NULL) && (ctx->params_ctx != NULL)) {
        return iolink_params_ctx_get(ctx->params_ctx, index, subindex, buffer, max_len);
    }
    return iolink_params_get(index, subindex, buffer, max_len);
}

/** @brief Write a parameter via the bound params context, or the legacy global set. */
static int ds_params_set(iolink_ds_ctx_t* ctx, uint16_t index, uint8_t subindex,
                         const uint8_t* data, size_t len, bool persist)
{
    if ((ctx != NULL) && (ctx->params_ctx != NULL)) {
        return iolink_params_ctx_set(ctx->params_ctx, index, subindex, data, len, persist);
    }
    return iolink_params_set(index, subindex, data, len, persist);
}

uint16_t iolink_ds_calc_checksum(const uint8_t* data, size_t len)
{
    /* Fletcher-16 over the serialized image. The Master treats the DS blob and
       its checksum opaquely (DS is device-specific), so a stable device-local
       checksum is all that is required for consistency comparison. */
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

int iolink_ds_build_image(iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return -1;
    }

    size_t pos = 0U;
    for (size_t i = 0U; i < DS_PARAM_COUNT; ++i) {
        uint8_t value[DS_PARAM_VALUE_MAX];
        int n =
            ds_params_get(ctx, k_ds_params[i].index, k_ds_params[i].subindex, value, sizeof(value));
        if (n < 0) {
            n = 0; /* Parameter not present -> empty record */
        }

        if ((pos + DS_RECORD_HEADER + (size_t) n) > IOLINK_DS_IMAGE_MAX) {
            return -1; /* Image does not fit */
        }

        ctx->image[pos] = (uint8_t) (k_ds_params[i].index >> 8);
        ctx->image[pos + 1U] = (uint8_t) (k_ds_params[i].index & 0xFFU);
        ctx->image[pos + 2U] = k_ds_params[i].subindex;
        ctx->image[pos + 3U] = (uint8_t) n;
        if (n > 0) {
            (void) memcpy(&ctx->image[pos + DS_RECORD_HEADER], value, (size_t) n);
        }
        pos += DS_RECORD_HEADER + (size_t) n;
    }

    ctx->image_len = pos;
    ctx->current_checksum = iolink_ds_calc_checksum(ctx->image, pos);
    return (int) pos;
}

int iolink_ds_apply_image(iolink_ds_ctx_t* ctx, const uint8_t* data, size_t len)
{
    if ((ctx == NULL) || !iolink_buf_is_valid(data, len) || (len > IOLINK_DS_IMAGE_MAX)) {
        return -1;
    }

    /* Validate the full record structure and every address this Device owns
       before applying anything, so a rejected image never partially overwrites
       parameters (Table 98 rule 7). */
    size_t pos = 0U;
    while (pos < len) {
        if ((pos + DS_RECORD_HEADER) > len) {
            return -1; /* Truncated header */
        }
        const uint16_t index = (uint16_t) (((uint16_t) data[pos] << 8) | data[pos + 1U]);
        const uint8_t subindex = data[pos + 2U];
        const size_t rlen = (size_t) data[pos + 3U];
        if ((pos + DS_RECORD_HEADER + rlen) > len) {
            return -1; /* Truncated data */
        }
        const int param = ds_param_find(index, subindex);
        if (param < 0) {
            if (ds_param_misaddressed(index, subindex)) {
                return -1; /* Known parameter with a wrong subindex */
            }
            /* A replacement Device may omit a backed-up parameter. */
        }
        else if (rlen > (size_t) k_ds_params[param].max_len) {
            return -1; /* Value exceeds the StringT fixedLength (Table B.8) */
        }
        pos += DS_RECORD_HEADER + rlen;
    }

    /* Apply each record. Unknown indices are skipped (a replacement device may
       not implement every backed-up parameter) rather than failing the restore. */
    pos = 0U;
    while (pos < len) {
        uint16_t index = (uint16_t) (((uint16_t) data[pos] << 8) | (uint16_t) data[pos + 1U]);
        uint8_t subindex = data[pos + 2U];
        size_t rlen = (size_t) data[pos + 3U];
        /* A replacement Device may omit a backed-up parameter; unknown records
           are intentionally skipped. Known DS parameters must apply cleanly. */
        if ((ds_param_find(index, subindex) >= 0) &&
            (ds_params_set(ctx, index, subindex, &data[pos + DS_RECORD_HEADER], rlen, true) != 0)) {
            /* Persistence failure after the validation pass: the remaining
               records were not applied, but earlier records may already be in
               NVM. The parameter manager has no transactional commit. */
            return -1;
        }
        pos += DS_RECORD_HEADER + rlen;
    }

    /* Recovery: re-serialize from the device's ACTUAL parameters so the stored
       image and checksum reflect true device state rather than the raw download.
       If the image could not be applied faithfully (e.g. unknown indices were
       skipped), the resulting checksum differs from the Master's, which makes the
       next iolink_ds_check() re-request a download until the two sides agree. */
    if (iolink_ds_build_image(ctx) < 0) {
        return -1;
    }
    return 0;
}

bool iolink_ds_verify(const iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return false;
    }
    return iolink_ds_calc_checksum(ctx->image, ctx->image_len) == ctx->current_checksum;
}

const uint8_t* iolink_ds_get_image(iolink_ds_ctx_t* ctx, size_t* out_len)
{
    if (ctx == NULL) {
        return NULL;
    }
    if (ctx->image_len == 0U) {
        if (iolink_ds_build_image(ctx) < 0) {
            return NULL;
        }
    }
    if (out_len != NULL) {
        *out_len = ctx->image_len;
    }
    return ctx->image;
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
            /* Master has no data -> snapshot the current parameters for upload. */
            (void) iolink_ds_build_image(ctx);
            ctx->state = IOLINK_DS_STATE_UPLOADING;
            break;

        case IOLINK_DS_STATE_UPLOADING:
            ctx->state = IOLINK_DS_STATE_IDLE;
            break;

        case IOLINK_DS_STATE_DOWNLOAD_REQ:
            /* Master indicated a mismatch -> Device will receive parameters. */
            ctx->state = IOLINK_DS_STATE_DOWNLOADING;
            break;

        case IOLINK_DS_STATE_DOWNLOADING:
            /* Commit a staged image if one was written to DS_Data (0x0003). A
               rejected image is discarded without touching the active
               parameters. The checksum is always recomputed from the device's
               ACTUAL parameters, never copied from the Master: adopting the
               Master value would mark a rejected/never-staged download as
               synchronised (Table 98 rule 7). */
            if (ctx->image_len > 0U) {
                if (iolink_ds_apply_image(ctx, ctx->image, ctx->image_len) != 0) {
                    ctx->image_len = 0U;
                    (void) iolink_ds_build_image(ctx);
                    ctx->state = IOLINK_DS_STATE_IDLE;
                    break;
                }
            }
            else {
                (void) iolink_ds_build_image(ctx);
            }
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

    (void) iolink_ds_build_image(ctx);
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

    ctx->image_len = 0U; /* Clear any prior staged image */
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

int iolink_ds_discard(iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return -1;
    }

    ctx->state = IOLINK_DS_STATE_IDLE;
    /* A staged Master image must never be applied after a reset command. */
    ctx->image_len = 0U;
    return 0;
}

int iolink_ds_handle_command(iolink_ds_ctx_t* ctx, uint8_t cmd, uint16_t access_locks)
{
    if (ctx == NULL) {
        return -1;
    }

    /* Check Access Locks for Download Commands (Write to Device) */
    if ((cmd == IOLINK_CMD_PARAM_DOWNLOAD_START) || (cmd == IOLINK_CMD_PARAM_DOWNLOAD_END) ||
        (cmd == IOLINK_CMD_PARAM_DOWNLOAD_STORE)) {
        if ((access_locks & IOLINK_LOCK_DS) != 0U) {
            /* DS Logic is locked */
            return -2; /* Signal Access Denied (user should map to ISDU error) */
        }
    }

    switch (cmd) {
        case IOLINK_CMD_PARAM_UPLOAD_START: /* 0x01 */
            /* Master wants to read parameters (Upload) */
            if (ctx->state != IOLINK_DS_STATE_IDLE) {
                return -1; /* Busy */
            }
            (void) iolink_ds_build_image(ctx);
            ctx->state = IOLINK_DS_STATE_UPLOAD_REQ;
            break;

        case IOLINK_CMD_PARAM_UPLOAD_END: /* 0x02 */
            /* Finish upload */
            if (ctx->state == IOLINK_DS_STATE_UPLOADING) {
                ctx->state = IOLINK_DS_STATE_IDLE;
            }
            break;

        case IOLINK_CMD_PARAM_DOWNLOAD_START: /* 0x03 */
            /* Master wants to write parameters (Download) */
            if (ctx->state != IOLINK_DS_STATE_IDLE) {
                return -1; /* Busy */
            }
            ctx->image_len = 0U; /* Clear any prior staged image */
            ctx->state = IOLINK_DS_STATE_DOWNLOAD_REQ;
            break;

        case IOLINK_CMD_PARAM_DOWNLOAD_END: /* 0x04 */
            /* Commit the parameter image staged via DS_Data (0x0003). */
            if (ctx->state == IOLINK_DS_STATE_DOWNLOADING) {
                if (ctx->image_len > 0U) {
                    if (iolink_ds_apply_image(ctx, ctx->image, ctx->image_len) != 0) {
                        /* Table 98 rule 7: the previous parameter set stays
                           active and the rejected image must not be readable
                           through DS_Data (0x0003). */
                        ctx->image_len = 0U;
                        (void) iolink_ds_build_image(ctx);
                        return -4;
                    }
                }
                ctx->state = IOLINK_DS_STATE_IDLE;
            }
            break;

        case IOLINK_CMD_PARAM_DOWNLOAD_STORE: /* 0x05 */
            /* 10.3.5: terminates the Block Parameter transfer like
               ParamDownloadEnd and additionally starts the Data Storage upload
               (DS_UPLOAD_REQ, 10.4.2). It also applies after single-parameter
               writes, so it is accepted from Idle as well. */
            if ((ctx->state != IOLINK_DS_STATE_IDLE) &&
                (ctx->state != IOLINK_DS_STATE_DOWNLOADING)) {
                return -1; /* Busy */
            }
            if (ctx->state == IOLINK_DS_STATE_DOWNLOADING) {
                if (ctx->image_len > 0U) {
                    if (iolink_ds_apply_image(ctx, ctx->image, ctx->image_len) != 0) {
                        /* Same rule as ParamDownloadEnd: discard the rejected
                           image so 0x0003 cannot serve it. */
                        ctx->image_len = 0U;
                        (void) iolink_ds_build_image(ctx);
                        return -4;
                    }
                }
            }
            if (iolink_ds_build_image(ctx) < 0) {
                return -4;
            }
            ctx->state = IOLINK_DS_STATE_UPLOAD_REQ;
            break;

        case IOLINK_CMD_PARAM_BREAK: /* 0x06 */
            return iolink_ds_abort(ctx);

        default:
            return -3; /* Unknown command */
    }

    return 0;
}
