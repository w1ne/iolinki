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
    uint8_t max_len;
} k_ds_params[] = {
    {IOLINK_IDX_APPLICATION_TAG, 0U, (uint8_t) IOLINK_PARAMS_TAG_MAX},
    {IOLINK_IDX_FUNCTION_TAG, 0U, (uint8_t) IOLINK_PARAMS_TAG_MAX},
    {IOLINK_IDX_LOCATION_TAG, 0U, (uint8_t) IOLINK_PARAMS_TAG_MAX},
};

#define DS_PARAM_COUNT (sizeof(k_ds_params) / sizeof(k_ds_params[0]))
#define DS_RECORD_HEADER 4U /* index(2) + subindex(1) + length(1) */
#define DS_PARAM_VALUE_MAX 64U
#define DS_IMAGE_REQUIRED (DS_PARAM_COUNT * (DS_RECORD_HEADER + IOLINK_PARAMS_TAG_MAX))

/* A full image of every DS parameter at its maximum length must fit, so a
   rebuild from valid parameters cannot fail for capacity reasons. */
typedef char ds_image_capacity_check[(DS_IMAGE_REQUIRED <= IOLINK_DS_IMAGE_MAX) ? 1 : -1];

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

/** @brief The bound params context, or the legacy singleton for unbound DS contexts. */
static iolink_params_ctx_t* ds_params(const iolink_ds_ctx_t* ctx)
{
    if (ctx->params_ctx != NULL) {
        return ctx->params_ctx;
    }
    return iolink_params_get_legacy_ctx();
}

/** @brief Discard a staged download image. */
static void ds_discard_staging(iolink_ds_ctx_t* ctx)
{
    ctx->staged_valid = false;
    ctx->staged_len = 0U;
}

/** @brief Position of (index, subindex) in the DS set, or -1 if not a DS parameter. */
static int ds_param_slot(uint16_t index, uint8_t subindex)
{
    for (size_t i = 0U; i < DS_PARAM_COUNT; ++i) {
        if ((k_ds_params[i].index == index) && (k_ds_params[i].subindex == subindex)) {
            return (int) i;
        }
    }
    return -1;
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
    uint8_t tmp[IOLINK_DS_IMAGE_MAX];

    if (ctx == NULL) {
        return IOLINK_DS_ERR_INVALID;
    }

    /* Serialize into a scratch buffer first so a failed rebuild never leaves a
       half-written image behind the last valid snapshot. */
    const iolink_params_ctx_t* params = ds_params(ctx);
    size_t pos = 0U;
    for (size_t i = 0U; i < DS_PARAM_COUNT; ++i) {
        uint8_t value[DS_PARAM_VALUE_MAX];
        int n = iolink_params_ctx_get(params, k_ds_params[i].index, k_ds_params[i].subindex, value,
                                      sizeof(value));
        if (n < 0) {
            n = 0; /* Parameter not present -> empty record */
        }
        if ((size_t) n > (size_t) k_ds_params[i].max_len) {
            return IOLINK_DS_ERR_INVALID;
        }
        if ((pos + DS_RECORD_HEADER + (size_t) n) > sizeof(tmp)) {
            return IOLINK_DS_ERR_INVALID; /* Image does not fit */
        }

        tmp[pos] = (uint8_t) (k_ds_params[i].index >> 8);
        tmp[pos + 1U] = (uint8_t) (k_ds_params[i].index & 0xFFU);
        tmp[pos + 2U] = k_ds_params[i].subindex;
        tmp[pos + 3U] = (uint8_t) n;
        if (n > 0) {
            (void) memcpy(&tmp[pos + DS_RECORD_HEADER], value, (size_t) n);
        }
        pos += DS_RECORD_HEADER + (size_t) n;
    }

    (void) memcpy(ctx->image, tmp, pos);
    ctx->image_len = pos;
    ctx->current_checksum = iolink_ds_calc_checksum(ctx->image, pos);
    return (int) pos;
}

/** @brief Check one record value against the parameter's value rules. */
static bool ds_value_valid(const uint8_t* value, size_t len)
{
    /* All DS parameters are StringT tags: the device stores them as C strings,
       so an embedded NUL would silently truncate the restored value. */
    return memchr(value, 0, len) == NULL;
}

int iolink_ds_validate_image(const uint8_t* data, size_t len)
{
    bool seen[DS_PARAM_COUNT] = {false};

    if ((data == NULL) || (len == 0U) || (len > IOLINK_DS_IMAGE_MAX)) {
        return IOLINK_DS_ERR_INVALID;
    }

    size_t pos = 0U;
    while (pos < len) {
        if ((len - pos) < DS_RECORD_HEADER) {
            return IOLINK_DS_ERR_INVALID; /* Truncated header */
        }
        uint16_t index = (uint16_t) (((uint16_t) data[pos] << 8) | (uint16_t) data[pos + 1U]);
        uint8_t subindex = data[pos + 2U];
        size_t rlen = (size_t) data[pos + 3U];
        if ((len - pos - DS_RECORD_HEADER) < rlen) {
            return IOLINK_DS_ERR_INVALID; /* Truncated data */
        }
        int slot = ds_param_slot(index, subindex);
        if (slot < 0) {
            return IOLINK_DS_ERR_INVALID; /* Not a DS parameter of this device */
        }
        if (seen[slot]) {
            return IOLINK_DS_ERR_INVALID; /* Duplicate record */
        }
        seen[slot] = true;
        if (rlen > (size_t) k_ds_params[slot].max_len) {
            return IOLINK_DS_ERR_INVALID; /* Length overrun */
        }
        if (!ds_value_valid(&data[pos + DS_RECORD_HEADER], rlen)) {
            return IOLINK_DS_ERR_INVALID;
        }
        pos += DS_RECORD_HEADER + rlen;
    }
    return IOLINK_DS_OK;
}

int iolink_ds_apply_image(iolink_ds_ctx_t* ctx, const uint8_t* data, size_t len)
{
    if ((ctx == NULL) || (iolink_ds_validate_image(data, len) != IOLINK_DS_OK)) {
        return IOLINK_DS_ERR_INVALID;
    }

    /* Stage every record into a copy of the live parameters. Nothing live is
       touched until the whole image has been accepted. */
    iolink_params_ctx_t* params = ds_params(ctx);
    iolink_params_ctx_t candidate = *params;
    size_t pos = 0U;
    while (pos < len) {
        uint16_t index = (uint16_t) (((uint16_t) data[pos] << 8) | (uint16_t) data[pos + 1U]);
        uint8_t subindex = data[pos + 2U];
        size_t rlen = (size_t) data[pos + 3U];
        if (iolink_params_ctx_stage(&candidate, index, subindex, &data[pos + DS_RECORD_HEADER],
                                    rlen) != 0) {
            return IOLINK_DS_ERR_INVALID;
        }
        pos += DS_RECORD_HEADER + rlen;
    }

    /* One NVM write for the whole parameter set; RAM changes only on success. */
    if (iolink_params_ctx_commit(params, &candidate, true) != 0) {
        return IOLINK_DS_ERR_STORAGE;
    }

    /* Re-serialize from the ACTUAL parameters so image and checksum reflect
       true device state rather than the raw download. */
    if (iolink_ds_build_image(ctx) < 0) {
        return IOLINK_DS_ERR_INVALID;
    }
    return IOLINK_DS_OK;
}

int iolink_ds_stage_image(iolink_ds_ctx_t* ctx, const uint8_t* data, size_t len)
{
    if (ctx == NULL) {
        return IOLINK_DS_ERR_INVALID;
    }
    if ((ctx->state != IOLINK_DS_STATE_DOWNLOAD_REQ) &&
        (ctx->state != IOLINK_DS_STATE_DOWNLOADING)) {
        return IOLINK_DS_ERR_INVALID;
    }
    if (iolink_ds_validate_image(data, len) != IOLINK_DS_OK) {
        ds_discard_staging(ctx);
        return IOLINK_DS_ERR_INVALID;
    }
    (void) memcpy(ctx->staged, data, len);
    ctx->staged_len = len;
    ctx->staged_valid = true;
    ctx->state = IOLINK_DS_STATE_DOWNLOADING;
    return IOLINK_DS_OK;
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
    if ((iolink_ds_build_image(ctx) < 0) && (ctx->image_len == 0U)) {
        return NULL;
    }
    if (out_len != NULL) {
        *out_len = ctx->image_len;
    }
    return ctx->image;
}

int iolink_ds_save(iolink_ds_ctx_t* ctx)
{
    uint8_t header[IOLINK_DS_STORE_HEADER_SIZE];

    if (ctx == NULL) {
        return IOLINK_DS_ERR_INVALID;
    }
    if ((ctx->storage == NULL) || (ctx->storage->write == NULL)) {
        return IOLINK_DS_ERR_STORAGE;
    }
    if (iolink_ds_build_image(ctx) < 0) {
        return IOLINK_DS_ERR_INVALID;
    }

    header[0] = (uint8_t) 'D';
    header[1] = (uint8_t) 'S';
    header[2] = (uint8_t) IOLINK_DS_IMAGE_LAYOUT_VERSION;
    header[3] = 0U;
    header[4] = (uint8_t) (ctx->image_len >> 8);
    header[5] = (uint8_t) (ctx->image_len & 0xFFU);
    header[6] = (uint8_t) (ctx->current_checksum >> 8);
    header[7] = (uint8_t) (ctx->current_checksum & 0xFFU);

    size_t total = sizeof(header) + ctx->image_len;
    if ((ctx->storage->erase != NULL) && (ctx->storage->erase(0U, total) != 0)) {
        return IOLINK_DS_ERR_STORAGE;
    }
    /* Write the image first and the header last. If the backend keeps write
       order, an interrupted save leaves an erased or old header. This is not a
       transaction: iolink_ds_load() verifies length and checksum. */
    if ((ctx->image_len > 0U) &&
        (ctx->storage->write((uint32_t) sizeof(header), ctx->image, ctx->image_len) != 0)) {
        return IOLINK_DS_ERR_STORAGE;
    }
    if (ctx->storage->write(0U, header, sizeof(header)) != 0) {
        return IOLINK_DS_ERR_STORAGE;
    }
    return IOLINK_DS_OK;
}

int iolink_ds_load(const iolink_ds_ctx_t* ctx, uint8_t* out, size_t max_len, size_t* out_len)
{
    uint8_t header[IOLINK_DS_STORE_HEADER_SIZE];

    if ((ctx == NULL) || (out == NULL) || (out_len == NULL)) {
        return IOLINK_DS_ERR_INVALID;
    }
    if ((ctx->storage == NULL) || (ctx->storage->read == NULL)) {
        return IOLINK_DS_ERR_STORAGE;
    }
    if (ctx->storage->read(0U, header, sizeof(header)) != 0) {
        return IOLINK_DS_ERR_STORAGE;
    }
    size_t len = ((size_t) header[4] << 8) | (size_t) header[5];
    uint16_t cs = (uint16_t) (((uint16_t) header[6] << 8) | header[7]);
    if ((header[0] != (uint8_t) 'D') || (header[1] != (uint8_t) 'S') ||
        (header[2] != (uint8_t) IOLINK_DS_IMAGE_LAYOUT_VERSION) || (len == 0U) ||
        (len > IOLINK_DS_IMAGE_MAX) || (len > max_len)) {
        return IOLINK_DS_ERR_INVALID;
    }
    if (ctx->storage->read((uint32_t) sizeof(header), out, len) != 0) {
        return IOLINK_DS_ERR_STORAGE;
    }
    if ((iolink_ds_calc_checksum(out, len) != cs) ||
        (iolink_ds_validate_image(out, len) != IOLINK_DS_OK)) {
        return IOLINK_DS_ERR_INVALID;
    }
    *out_len = len;
    return IOLINK_DS_OK;
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

/**
 * @brief Finish a download session: commit staged data, rebuild, optionally store.
 *
 * Always leaves the engine IDLE with staging discarded.
 */
static int ds_finish_download(iolink_ds_ctx_t* ctx, bool store)
{
    int res = IOLINK_DS_OK;

    if (ctx->staged_valid) {
        res = iolink_ds_apply_image(ctx, ctx->staged, ctx->staged_len);
    }
    else if (iolink_ds_build_image(ctx) < 0) {
        /* No staged image: parameters may have been written individually. */
        res = IOLINK_DS_ERR_INVALID;
    }
    ds_discard_staging(ctx);

    if ((res == IOLINK_DS_OK) && store) {
        ctx->upload_requested = true;
        if (ctx->storage != NULL) {
            res = iolink_ds_save(ctx);
        }
    }
    ctx->last_result = res;
    ctx->state = IOLINK_DS_STATE_IDLE;
    return res;
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
            /* Commit a staged image if present; otherwise only refresh the
               checksum from actual parameters. Never adopt the Master checksum
               without data, so a failed download is visible at the next check. */
            (void) ds_finish_download(ctx, false);
            break;

        default:
            ctx->state = IOLINK_DS_STATE_IDLE;
            break;
    }
}

int iolink_ds_start_upload(iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return IOLINK_DS_ERR_INVALID;
    }

    if (ctx->state != IOLINK_DS_STATE_IDLE) {
        return IOLINK_DS_CMD_BUSY;
    }

    if (iolink_ds_build_image(ctx) < 0) {
        return IOLINK_DS_ERR_INVALID;
    }
    ctx->state = IOLINK_DS_STATE_UPLOAD_REQ;
    return IOLINK_DS_OK;
}

int iolink_ds_start_download(iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return IOLINK_DS_ERR_INVALID;
    }

    if (ctx->state != IOLINK_DS_STATE_IDLE) {
        return IOLINK_DS_CMD_BUSY;
    }

    /* Discard stale staging; keep the last valid image of actual parameters. */
    ds_discard_staging(ctx);
    ctx->state = IOLINK_DS_STATE_DOWNLOAD_REQ;
    return IOLINK_DS_OK;
}

int iolink_ds_abort(iolink_ds_ctx_t* ctx)
{
    if (ctx == NULL) {
        return IOLINK_DS_ERR_INVALID;
    }

    ds_discard_staging(ctx);
    ctx->state = IOLINK_DS_STATE_IDLE;
    return IOLINK_DS_OK;
}

int iolink_ds_handle_command(iolink_ds_ctx_t* ctx, uint8_t cmd, uint16_t access_locks)
{
    if (ctx == NULL) {
        return IOLINK_DS_CMD_BUSY;
    }

    /* Check Access Locks for Download Commands (Write to Device) */
    if ((cmd == IOLINK_CMD_PARAM_DOWNLOAD_START) || (cmd == IOLINK_CMD_PARAM_DOWNLOAD_END) ||
        (cmd == IOLINK_CMD_PARAM_DOWNLOAD_STORE)) {
        if ((access_locks & IOLINK_LOCK_DS) != 0U) {
            return IOLINK_DS_CMD_DENIED; /* user maps to ISDU error */
        }
    }

    switch (cmd) {
        case IOLINK_CMD_PARAM_UPLOAD_START: /* 0x07 */
            if (ctx->state != IOLINK_DS_STATE_IDLE) {
                return IOLINK_DS_CMD_BUSY;
            }
            if (iolink_ds_build_image(ctx) < 0) {
                return IOLINK_DS_CMD_COMMIT_FAILED;
            }
            ctx->state = IOLINK_DS_STATE_UPLOAD_REQ;
            break;

        case IOLINK_CMD_PARAM_UPLOAD_END: /* 0x08 */
            if ((ctx->state == IOLINK_DS_STATE_UPLOADING) ||
                (ctx->state == IOLINK_DS_STATE_UPLOAD_REQ)) {
                ctx->state = IOLINK_DS_STATE_IDLE;
                ctx->upload_requested = false;
            }
            break;

        case IOLINK_CMD_PARAM_DOWNLOAD_START: /* 0x05 */
            return iolink_ds_start_download(ctx);

        case IOLINK_CMD_PARAM_DOWNLOAD_END: /* 0x06 */
            if ((ctx->state == IOLINK_DS_STATE_DOWNLOAD_REQ) ||
                (ctx->state == IOLINK_DS_STATE_DOWNLOADING)) {
                if (ds_finish_download(ctx, false) != IOLINK_DS_OK) {
                    return IOLINK_DS_CMD_COMMIT_FAILED;
                }
            }
            break;

        case IOLINK_CMD_PARAM_DOWNLOAD_STORE: /* 0x09 */
            if ((ctx->state == IOLINK_DS_STATE_UPLOAD_REQ) ||
                (ctx->state == IOLINK_DS_STATE_UPLOADING)) {
                return IOLINK_DS_CMD_BUSY;
            }
            if (ds_finish_download(ctx, true) != IOLINK_DS_OK) {
                return IOLINK_DS_CMD_COMMIT_FAILED;
            }
            break;

        case IOLINK_CMD_PARAM_BREAK: /* 0x97 */
            return iolink_ds_abort(ctx);

        default:
            return IOLINK_DS_CMD_UNKNOWN;
    }

    return IOLINK_DS_OK;
}
