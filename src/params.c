/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/params.h"
#include "iolinki/platform.h"
#include "iolinki/protocol.h"
#include "iolinki/utils.h"
#include <string.h>

#define PARAMS_NVM_MAGIC 0x494F4C31U /* "IOL1" */

typedef struct
{
    uint32_t magic;
    char application_tag[33];
    char function_tag[33];
    char location_tag[33];
    /* Future: user parameters, etc. */
} iolink_params_nvm_t;

static iolink_device_info_ctx_t g_legacy_device_info_ctx;
static iolink_params_ctx_t g_legacy_params_ctx;
static bool g_legacy_params_ctx_initialized = false;

static void copy_tag(char* dst, bool* valid, const uint8_t* data, size_t len)
{
    size_t copy_len = (len > 32U) ? 32U : len;

    if (copy_len > 0U) {
        (void) memcpy(dst, data, copy_len);
    }
    dst[copy_len] = '\0';
    *valid = true;
}

static size_t bounded_tag_len(const char* tag)
{
    size_t len = strlen(tag);
    if (len > 32U) {
        len = 32U;
    }
    return len;
}

static int read_tag(const char* tag, uint8_t* buffer, size_t max_len)
{
    size_t len = bounded_tag_len(tag);
    if (len > max_len) {
        len = max_len;
    }
    if (len > 0U) {
        (void) memcpy(buffer, tag, len);
    }
    return (int) len;
}

static void params_ctx_write_nvm(const iolink_params_ctx_t* ctx)
{
    iolink_params_nvm_t nvm;

    (void) memset(&nvm, 0, sizeof(nvm));
    nvm.magic = PARAMS_NVM_MAGIC;
    if (ctx->application_tag_valid) {
        (void) memcpy(nvm.application_tag, ctx->application_tag, sizeof(nvm.application_tag));
    }
    if (ctx->function_tag_valid) {
        (void) memcpy(nvm.function_tag, ctx->function_tag, sizeof(nvm.function_tag));
    }
    if (ctx->location_tag_valid) {
        (void) memcpy(nvm.location_tag, ctx->location_tag, sizeof(nvm.location_tag));
    }
    (void) iolink_nvm_write(0U, (uint8_t*) &nvm, sizeof(nvm));
}

void iolink_params_ctx_init(iolink_params_ctx_t* ctx, iolink_device_info_ctx_t* device_info)
{
    iolink_params_nvm_t nvm;

    if (ctx == NULL) {
        return;
    }

    (void) memset(ctx, 0, sizeof(*ctx));
    ctx->device_info = device_info;

    if ((iolink_nvm_read(0U, (uint8_t*) &nvm, sizeof(nvm)) == 0) &&
        (nvm.magic == PARAMS_NVM_MAGIC)) {
        copy_tag(ctx->application_tag, &ctx->application_tag_valid,
                 (const uint8_t*) nvm.application_tag, bounded_tag_len(nvm.application_tag));
        copy_tag(ctx->function_tag, &ctx->function_tag_valid, (const uint8_t*) nvm.function_tag,
                 bounded_tag_len(nvm.function_tag));
        copy_tag(ctx->location_tag, &ctx->location_tag_valid, (const uint8_t*) nvm.location_tag,
                 bounded_tag_len(nvm.location_tag));
        if (ctx->device_info != NULL) {
            (void) iolink_device_info_ctx_set_application_tag(
                ctx->device_info, ctx->application_tag, (uint8_t) strlen(ctx->application_tag));
        }
        return;
    }

    if (ctx->device_info != NULL) {
        const iolink_device_info_t* info = iolink_device_info_ctx_get(ctx->device_info);
        if ((info != NULL) && (info->application_tag != NULL)) {
            copy_tag(ctx->application_tag, &ctx->application_tag_valid,
                     (const uint8_t*) info->application_tag, bounded_tag_len(info->application_tag));
        }
    }
}

int iolink_params_ctx_get(iolink_params_ctx_t* ctx, uint16_t index, uint8_t subindex,
                          uint8_t* buffer, size_t max_len)
{
    if ((ctx == NULL) || (buffer == NULL)) {
        return -1;
    }
    if ((index == IOLINK_IDX_APPLICATION_TAG) && (subindex == 0U)) {
        const iolink_device_info_t* info = iolink_device_info_ctx_get(ctx->device_info);
        if ((info != NULL) && (info->application_tag != NULL)) {
            return read_tag(info->application_tag, buffer, max_len);
        }
        return 0;
    }
    if ((index == IOLINK_IDX_FUNCTION_TAG) && (subindex == 0U)) {
        if (!ctx->function_tag_valid) {
            return 0;
        }
        return read_tag(ctx->function_tag, buffer, max_len);
    }
    if ((index == IOLINK_IDX_LOCATION_TAG) && (subindex == 0U)) {
        if (!ctx->location_tag_valid) {
            return 0;
        }
        return read_tag(ctx->location_tag, buffer, max_len);
    }
    return -1;
}

int iolink_params_ctx_set(iolink_params_ctx_t* ctx, uint16_t index, uint8_t subindex,
                          const uint8_t* data, size_t len, bool persist)
{
    if ((ctx == NULL) || !iolink_buf_is_valid(data, len)) {
        return -1;
    }
    if ((index == IOLINK_IDX_APPLICATION_TAG) && (subindex == 0U)) {
        if (len >= sizeof(ctx->application_tag)) {
            return -1;
        }
        copy_tag(ctx->application_tag, &ctx->application_tag_valid, data, len);
        if (ctx->device_info != NULL) {
            if (iolink_device_info_ctx_set_application_tag(
                    ctx->device_info, ctx->application_tag,
                    (uint8_t) strlen(ctx->application_tag)) != 0) {
                return -1;
            }
        }
        if (persist) {
            params_ctx_write_nvm(ctx);
        }
        return 0;
    }
    if ((index == IOLINK_IDX_FUNCTION_TAG) && (subindex == 0U)) {
        copy_tag(ctx->function_tag, &ctx->function_tag_valid, data, len);
        if (persist) {
            params_ctx_write_nvm(ctx);
        }
        return 0;
    }
    if ((index == IOLINK_IDX_LOCATION_TAG) && (subindex == 0U)) {
        copy_tag(ctx->location_tag, &ctx->location_tag_valid, data, len);
        if (persist) {
            params_ctx_write_nvm(ctx);
        }
        return 0;
    }
    return -1;
}

void iolink_params_ctx_factory_reset(iolink_params_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }

    ctx->application_tag[0] = '\0';
    ctx->function_tag[0] = '\0';
    ctx->location_tag[0] = '\0';
    ctx->application_tag_valid = true;
    ctx->function_tag_valid = true;
    ctx->location_tag_valid = true;

    if (ctx->device_info != NULL) {
        (void) iolink_device_info_ctx_set_application_tag(ctx->device_info, "", 0U);
    }
    params_ctx_write_nvm(ctx);
}

static iolink_params_ctx_t* legacy_params_ctx(void)
{
    if (!g_legacy_params_ctx_initialized) {
        iolink_device_info_ctx_init(&g_legacy_device_info_ctx, NULL);
        iolink_params_ctx_init(&g_legacy_params_ctx, &g_legacy_device_info_ctx);
        g_legacy_params_ctx_initialized = true;
    }
    return &g_legacy_params_ctx;
}

void iolink_params_init(void)
{
    iolink_device_info_ctx_init(&g_legacy_device_info_ctx, iolink_device_info_get());
    iolink_params_ctx_init(&g_legacy_params_ctx, &g_legacy_device_info_ctx);
    g_legacy_params_ctx_initialized = true;
}

int iolink_params_get(uint16_t index, uint8_t subindex, uint8_t* buffer, size_t max_len)
{
    return iolink_params_ctx_get(legacy_params_ctx(), index, subindex, buffer, max_len);
}

int iolink_params_set(uint16_t index, uint8_t subindex, const uint8_t* data, size_t len,
                      bool persist)
{
    int res = iolink_params_ctx_set(legacy_params_ctx(), index, subindex, data, len, persist);
    if ((res == 0) && (index == IOLINK_IDX_APPLICATION_TAG) && (subindex == 0U)) {
        (void) iolink_device_info_set_application_tag((const char*) data, (uint8_t) len);
    }
    return res;
}

void iolink_params_factory_reset(void)
{
    iolink_params_ctx_factory_reset(legacy_params_ctx());
    (void) iolink_device_info_set_application_tag("", 0U);
}
