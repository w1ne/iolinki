/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file params.c
 * @brief Parameter manager: writable device tags with NVM persistence.
 * @ingroup iolinki_params
 *
 * Manages the application/function/location tag parameters, loading and
 * persisting them via the platform NVM interface, and mirrors the application
 * tag into the device-info context. A legacy singleton wrapper is provided for
 * context-less callers.
 */

#include "iolinki/params.h"
#include "iolinki/platform.h"
#include "iolinki/protocol.h"
#include "iolinki/utils.h"
#include <string.h>

/* Layout 1 ("IOL1"): native C struct image, written by earlier releases.
   Kept read-only so an existing device keeps its tags after an update. */
#define PARAMS_NVM_LEGACY_MAGIC 0x494F4C31U

typedef struct
{
    uint32_t magic;
    char application_tag[33];
    char function_tag[33];
    char location_tag[33];
} iolink_params_nvm_legacy_t;

#define REC_VERSION 4U
#define REC_FLAGS 5U
#define REC_LOCKS 6U
#define REC_APP 8U
#define REC_FUNC 41U
#define REC_LOC 74U
#define REC_TAG_FIELD (1U + IOLINK_PARAMS_TAG_MAX)
#define REC_CHECKSUM (IOLINK_PARAMS_NVM_RECORD_SIZE - 2U)

static iolink_device_info_ctx_t g_legacy_device_info_ctx;
static iolink_params_ctx_t g_legacy_params_ctx;
static bool g_legacy_params_ctx_initialized = false;

/** @brief Copy up to 32 bytes into a NUL-terminated tag buffer and mark it valid. */
static void copy_tag(char* dst, bool* valid, const uint8_t* data, size_t len)
{
    size_t copy_len = (len > IOLINK_PARAMS_TAG_MAX) ? IOLINK_PARAMS_TAG_MAX : len;

    if (copy_len > 0U) {
        (void) memcpy(dst, data, copy_len);
    }
    dst[copy_len] = '\0';
    *valid = true;
}

/** @brief Return the tag length, bounded to 32 octets even without a terminator. */
static size_t bounded_tag_len(const char* tag)
{
    const void* nul = memchr(tag, '\0', IOLINK_PARAMS_TAG_MAX);
    if (nul == NULL) {
        return IOLINK_PARAMS_TAG_MAX;
    }
    return (size_t) ((const char*) nul - tag);
}

/** @brief Copy a bounded tag string into an output buffer; returns bytes written. */
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

/** @brief Fletcher-16 integrity check for the NVM record. */
static uint16_t record_checksum(const uint8_t* data, size_t len)
{
    uint16_t sum1 = 0U;
    uint16_t sum2 = 0U;
    for (size_t i = 0U; i < len; ++i) {
        sum1 = (uint16_t) ((sum1 + (uint16_t) data[i]) % 255U);
        sum2 = (uint16_t) ((sum2 + sum1) % 255U);
    }
    return (uint16_t) ((sum2 << 8U) | sum1);
}

/** @brief True if [offset, offset + len) fits the 32-bit NVM address space. */
static bool nvm_range_ok(uint32_t offset, size_t len)
{
    return (len <= (size_t) UINT32_MAX) && (offset <= (UINT32_MAX - (uint32_t) len));
}

static void record_put_tag(uint8_t* rec, size_t pos, const char* tag, bool valid)
{
    size_t len = valid ? bounded_tag_len(tag) : 0U;
    rec[pos] = (uint8_t) len;
    if (len > 0U) {
        (void) memcpy(&rec[pos + 1U], tag, len);
    }
}

/** @brief Serialize tags and access locks into a layout-2 NVM record. */
static void record_build(const iolink_params_ctx_t* ctx, bool locks_valid, uint16_t locks,
                         uint8_t rec[IOLINK_PARAMS_NVM_RECORD_SIZE])
{
    uint8_t flags = 0U;

    (void) memset(rec, 0, IOLINK_PARAMS_NVM_RECORD_SIZE);
    rec[0] = (uint8_t) (IOLINK_PARAMS_NVM_MAGIC >> 24);
    rec[1] = (uint8_t) (IOLINK_PARAMS_NVM_MAGIC >> 16);
    rec[2] = (uint8_t) (IOLINK_PARAMS_NVM_MAGIC >> 8);
    rec[3] = (uint8_t) (IOLINK_PARAMS_NVM_MAGIC & 0xFFU);
    rec[REC_VERSION] = (uint8_t) IOLINK_PARAMS_NVM_LAYOUT_VERSION;
    if (ctx->application_tag_valid) {
        flags |= IOLINK_PARAMS_NVM_FLAG_APPLICATION_TAG;
    }
    if (ctx->function_tag_valid) {
        flags |= IOLINK_PARAMS_NVM_FLAG_FUNCTION_TAG;
    }
    if (ctx->location_tag_valid) {
        flags |= IOLINK_PARAMS_NVM_FLAG_LOCATION_TAG;
    }
    if (locks_valid) {
        flags |= IOLINK_PARAMS_NVM_FLAG_ACCESS_LOCKS;
        rec[REC_LOCKS] = (uint8_t) (locks >> 8);
        rec[REC_LOCKS + 1U] = (uint8_t) (locks & 0xFFU);
    }
    rec[REC_FLAGS] = flags;
    record_put_tag(rec, REC_APP, ctx->application_tag, ctx->application_tag_valid);
    record_put_tag(rec, REC_FUNC, ctx->function_tag, ctx->function_tag_valid);
    record_put_tag(rec, REC_LOC, ctx->location_tag, ctx->location_tag_valid);

    uint16_t cs = record_checksum(rec, REC_CHECKSUM);
    rec[REC_CHECKSUM] = (uint8_t) (cs >> 8);
    rec[REC_CHECKSUM + 1U] = (uint8_t) (cs & 0xFFU);
}

/** @brief Write the record for @p ctx with explicit access-lock content. */
static int params_write_nvm(const iolink_params_ctx_t* ctx, bool locks_valid, uint16_t locks)
{
    uint8_t rec[IOLINK_PARAMS_NVM_RECORD_SIZE];

    if (!nvm_range_ok(ctx->nvm_offset, sizeof(rec))) {
        return -1;
    }
    record_build(ctx, locks_valid, locks, rec);
    return (iolink_nvm_write(ctx->nvm_offset, rec, sizeof(rec)) == 0) ? 0 : -1;
}

/** @brief Write the record for @p ctx, preserving the bound device's current locks. */
static int params_ctx_write_nvm(const iolink_params_ctx_t* ctx)
{
    bool locks_valid = (ctx->device_info != NULL);
    uint16_t locks = iolink_device_info_ctx_get_access_locks(ctx->device_info);
    return params_write_nvm(ctx, locks_valid, locks);
}

/** @brief Decode a layout-2 record. Returns true only if every field is in range. */
static bool record_decode(const uint8_t rec[IOLINK_PARAMS_NVM_RECORD_SIZE],
                          iolink_params_ctx_t* ctx, bool* locks_valid, uint16_t* locks)
{
    uint32_t magic = ((uint32_t) rec[0] << 24) | ((uint32_t) rec[1] << 16) |
                     ((uint32_t) rec[2] << 8) | (uint32_t) rec[3];
    uint16_t stored_cs = (uint16_t) (((uint16_t) rec[REC_CHECKSUM] << 8) | rec[REC_CHECKSUM + 1U]);
    static const size_t tag_pos[3] = {REC_APP, REC_FUNC, REC_LOC};
    static const uint8_t tag_flag[3] = {IOLINK_PARAMS_NVM_FLAG_APPLICATION_TAG,
                                        IOLINK_PARAMS_NVM_FLAG_FUNCTION_TAG,
                                        IOLINK_PARAMS_NVM_FLAG_LOCATION_TAG};
    char* tag_dst[3] = {ctx->application_tag, ctx->function_tag, ctx->location_tag};
    bool* tag_valid[3] = {&ctx->application_tag_valid, &ctx->function_tag_valid,
                          &ctx->location_tag_valid};

    if ((magic != IOLINK_PARAMS_NVM_MAGIC) ||
        (rec[REC_VERSION] != (uint8_t) IOLINK_PARAMS_NVM_LAYOUT_VERSION) ||
        (record_checksum(rec, REC_CHECKSUM) != stored_cs)) {
        return false;
    }
    for (size_t i = 0U; i < 3U; ++i) {
        if (rec[tag_pos[i]] > IOLINK_PARAMS_TAG_MAX) {
            return false;
        }
    }
    for (size_t i = 0U; i < 3U; ++i) {
        if ((rec[REC_FLAGS] & tag_flag[i]) != 0U) {
            copy_tag(tag_dst[i], tag_valid[i], &rec[tag_pos[i] + 1U], rec[tag_pos[i]]);
        }
    }
    *locks_valid = (rec[REC_FLAGS] & IOLINK_PARAMS_NVM_FLAG_ACCESS_LOCKS) != 0U;
    *locks = (uint16_t) (((uint16_t) rec[REC_LOCKS] << 8) | rec[REC_LOCKS + 1U]);
    return true;
}

/** @brief Decode a legacy layout-1 record (tags only). */
static bool legacy_record_decode(uint32_t offset, iolink_params_ctx_t* ctx)
{
    iolink_params_nvm_legacy_t nvm;

    if (!nvm_range_ok(offset, sizeof(nvm)) ||
        (iolink_nvm_read(offset, (uint8_t*) &nvm, sizeof(nvm)) != 0) ||
        (nvm.magic != PARAMS_NVM_LEGACY_MAGIC)) {
        return false;
    }
    copy_tag(ctx->application_tag, &ctx->application_tag_valid,
             (const uint8_t*) nvm.application_tag, bounded_tag_len(nvm.application_tag));
    copy_tag(ctx->function_tag, &ctx->function_tag_valid, (const uint8_t*) nvm.function_tag,
             bounded_tag_len(nvm.function_tag));
    copy_tag(ctx->location_tag, &ctx->location_tag_valid, (const uint8_t*) nvm.location_tag,
             bounded_tag_len(nvm.location_tag));
    return true;
}

/** @brief Mirror committed legacy values into the process-wide device-info singleton. */
static void legacy_mirror(const iolink_params_ctx_t* ctx, bool tag_changed, bool locks_changed,
                          uint16_t locks)
{
    if (ctx != &g_legacy_params_ctx) {
        return;
    }
    if (tag_changed) {
        (void) iolink_device_info_set_application_tag(
            ctx->application_tag, (uint8_t) bounded_tag_len(ctx->application_tag));
    }
    if (locks_changed) {
        iolink_device_info_set_access_locks(locks);
    }
}

int iolink_params_ctx_init_at(iolink_params_ctx_t* ctx, iolink_device_info_ctx_t* device_info,
                              uint32_t nvm_offset)
{
    uint8_t rec[IOLINK_PARAMS_NVM_RECORD_SIZE];
    bool locks_valid = false;
    uint16_t locks = 0U;
    bool restored = false;

    if (ctx == NULL) {
        return -1;
    }

    (void) memset(ctx, 0, sizeof(*ctx));
    ctx->device_info = device_info;
    ctx->nvm_offset = nvm_offset;

    bool range_ok = nvm_range_ok(nvm_offset, sizeof(rec));

    if (range_ok && (iolink_nvm_read(nvm_offset, rec, sizeof(rec)) == 0)) {
        iolink_params_ctx_t decoded = *ctx;
        if (record_decode(rec, &decoded, &locks_valid, &locks)) {
            *ctx = decoded;
            restored = true;
        }
    }
    if (range_ok && !restored) {
        iolink_params_ctx_t decoded = *ctx;
        if (legacy_record_decode(nvm_offset, &decoded)) {
            *ctx = decoded;
            restored = true;
        }
    }

    if (restored) {
        if (ctx->device_info != NULL) {
            (void) iolink_device_info_ctx_set_application_tag(
                ctx->device_info, ctx->application_tag,
                (uint8_t) bounded_tag_len(ctx->application_tag));
            if (locks_valid && ((locks & (uint16_t) ~IOLINK_PARAMS_ACCESS_LOCKS_DEFINED) == 0U)) {
                iolink_device_info_ctx_set_access_locks(ctx->device_info, locks);
            }
        }
        return 0;
    }

    if (ctx->device_info != NULL) {
        const iolink_device_info_t* info = iolink_device_info_ctx_get(ctx->device_info);
        if ((info != NULL) && (info->application_tag != NULL)) {
            copy_tag(ctx->application_tag, &ctx->application_tag_valid,
                     (const uint8_t*) info->application_tag,
                     bounded_tag_len(info->application_tag));
        }
    }
    /* An out-of-range offset keeps RAM defaults; persistent writes then fail. */
    return range_ok ? 1 : -1;
}

void iolink_params_ctx_init(iolink_params_ctx_t* ctx, iolink_device_info_ctx_t* device_info)
{
    (void) iolink_params_ctx_init_at(ctx, device_info, IOLINK_PARAMS_NVM_DEFAULT_OFFSET);
}

int iolink_params_ctx_get(const iolink_params_ctx_t* ctx, uint16_t index, uint8_t subindex,
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
    if ((index == IOLINK_IDX_DEVICE_ACCESS_LOCKS) && (subindex == 0U)) {
        uint16_t locks = iolink_params_ctx_get_access_locks(ctx);
        if ((ctx->device_info == NULL) || (max_len < 2U)) {
            return -1;
        }
        buffer[0] = (uint8_t) (locks >> 8);
        buffer[1] = (uint8_t) (locks & 0xFFU);
        return 2;
    }
    return -1;
}

int iolink_params_ctx_stage(iolink_params_ctx_t* staged, uint16_t index, uint8_t subindex,
                            const uint8_t* data, size_t len)
{
    if ((staged == NULL) || !iolink_buf_is_valid(data, len) || (subindex != 0U)) {
        return -1;
    }
    if (len > IOLINK_PARAMS_TAG_MAX) {
        return -1;
    }
    if (index == IOLINK_IDX_APPLICATION_TAG) {
        copy_tag(staged->application_tag, &staged->application_tag_valid, data, len);
    }
    else if (index == IOLINK_IDX_FUNCTION_TAG) {
        copy_tag(staged->function_tag, &staged->function_tag_valid, data, len);
    }
    else if (index == IOLINK_IDX_LOCATION_TAG) {
        copy_tag(staged->location_tag, &staged->location_tag_valid, data, len);
    }
    else {
        return -1;
    }
    return 0;
}

int iolink_params_ctx_commit(iolink_params_ctx_t* ctx, const iolink_params_ctx_t* staged,
                             bool persist)
{
    if ((ctx == NULL) || (staged == NULL) || (staged->device_info != ctx->device_info) ||
        (staged->nvm_offset != ctx->nvm_offset)) {
        return -1;
    }
    bool tag_changed = (staged->application_tag_valid != ctx->application_tag_valid) ||
                       (strcmp(staged->application_tag, ctx->application_tag) != 0);

    /* A rejected persistent write must not appear accepted in RAM/readback. */
    if (persist && (params_ctx_write_nvm(staged) != 0)) {
        return -1;
    }
    if (tag_changed && (staged->device_info != NULL)) {
        if (iolink_device_info_ctx_set_application_tag(
                staged->device_info, staged->application_tag,
                (uint8_t) bounded_tag_len(staged->application_tag)) != 0) {
            return -1;
        }
    }
    if (staged != ctx) {
        *ctx = *staged;
    }
    legacy_mirror(ctx, tag_changed, false, 0U);
    return 0;
}

int iolink_params_ctx_set(iolink_params_ctx_t* ctx, uint16_t index, uint8_t subindex,
                          const uint8_t* data, size_t len, bool persist)
{
    if ((ctx == NULL) || !iolink_buf_is_valid(data, len)) {
        return -1;
    }
    if ((index == IOLINK_IDX_DEVICE_ACCESS_LOCKS) && (subindex == 0U)) {
        if (len != 2U) {
            return -1;
        }
        return iolink_params_ctx_set_access_locks(
            ctx, (uint16_t) (((uint16_t) data[0] << 8) | data[1]), persist);
    }
    iolink_params_ctx_t candidate = *ctx;
    if (iolink_params_ctx_stage(&candidate, index, subindex, data, len) != 0) {
        return -1;
    }
    return iolink_params_ctx_commit(ctx, &candidate, persist);
}

uint16_t iolink_params_ctx_get_access_locks(const iolink_params_ctx_t* ctx)
{
    if (ctx == NULL) {
        return 0U;
    }
    return iolink_device_info_ctx_get_access_locks(ctx->device_info);
}

int iolink_params_ctx_set_access_locks(iolink_params_ctx_t* ctx, uint16_t locks, bool persist)
{
    if ((ctx == NULL) || (ctx->device_info == NULL) ||
        ((locks & (uint16_t) ~IOLINK_PARAMS_ACCESS_LOCKS_DEFINED) != 0U)) {
        return -1;
    }
    if (persist && (params_write_nvm(ctx, true, locks) != 0)) {
        return -1;
    }
    iolink_device_info_ctx_set_access_locks(ctx->device_info, locks);
    legacy_mirror(ctx, false, true, locks);
    return 0;
}

/** @brief Factory-default access locks for @p ctx (legacy ctx uses the singleton's config). */
static uint16_t factory_locks(const iolink_params_ctx_t* ctx)
{
    if (ctx == &g_legacy_params_ctx) {
        return iolink_device_info_get_factory_access_locks();
    }
    return iolink_device_info_ctx_get_factory_access_locks(ctx->device_info);
}

/** @brief Build the factory-default candidate for @p ctx. */
static void factory_candidate(const iolink_params_ctx_t* ctx, iolink_params_ctx_t* candidate)
{
    *candidate = *ctx;
    candidate->application_tag[0] = '\0';
    candidate->function_tag[0] = '\0';
    candidate->location_tag[0] = '\0';
    candidate->application_tag_valid = true;
    candidate->function_tag_valid = true;
    candidate->location_tag_valid = true;
}

/** @brief Apply a factory-default candidate to RAM, including default access locks. */
static void factory_apply(iolink_params_ctx_t* ctx, const iolink_params_ctx_t* candidate)
{
    uint16_t locks = factory_locks(ctx);

    *ctx = *candidate;
    if (ctx->device_info != NULL) {
        (void) iolink_device_info_ctx_set_application_tag(ctx->device_info, "", 0U);
        iolink_device_info_ctx_set_access_locks(ctx->device_info, locks);
    }
    legacy_mirror(ctx, true, ctx->device_info != NULL, locks);
}

int iolink_params_ctx_factory_reset_checked(iolink_params_ctx_t* ctx)
{
    iolink_params_ctx_t candidate;

    if (ctx == NULL) {
        return -1;
    }
    factory_candidate(ctx, &candidate);
    if (params_write_nvm(&candidate, ctx->device_info != NULL,
                         factory_locks(ctx)) != 0) {
        return -1;
    }
    factory_apply(ctx, &candidate);
    return 0;
}

void iolink_params_ctx_factory_reset(iolink_params_ctx_t* ctx)
{
    iolink_params_ctx_t candidate;

    if (ctx == NULL) {
        return;
    }
    /* Legacy void API: reset RAM even if unsupported/failed storage cannot
     * retain the reset. Integrators cannot infer persistence from this call;
     * use iolink_params_ctx_factory_reset_checked() to get the result. */
    factory_candidate(ctx, &candidate);
    (void) params_write_nvm(&candidate, ctx->device_info != NULL,
                            factory_locks(ctx));
    factory_apply(ctx, &candidate);
}

/** @brief Lazily initialize and return the process-wide legacy parameter context. */
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
    /* The commit mirrors the application tag and access locks into the
       device-info singleton (legacy_mirror). */
    return iolink_params_ctx_set(legacy_params_ctx(), index, subindex, data, len, persist);
}

void iolink_params_factory_reset(void)
{
    iolink_params_ctx_factory_reset(legacy_params_ctx());
}

iolink_params_ctx_t* iolink_params_get_legacy_ctx(void)
{
    return legacy_params_ctx();
}
