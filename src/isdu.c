/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/protocol.h"
#include "iolinki/isdu.h"
#include "iolinki/dll.h"
#include "iolinki/iolink.h"
#include "iolinki/crc.h"
#include "iolinki/events.h"
#include "iolinki/device_info.h"
#include "iolinki/params.h"
#include "iolinki/data_storage.h"
#include "iolinki/platform.h"
#include "iolinki/utils.h"
#include <string.h>
#include <stdint.h>

/**
 * @file isdu.c
 * @brief ISDU service engine: acyclic parameter read/write handling.
 * @ingroup iolinki_isdu
 *
 * Implements the ISDU segmentation state machine (byte collection, header
 * parsing, sequence tracking and response segmentation) and dispatches
 * standard indices and system commands to the device parameter, device-info,
 * event, direct-parameter and data-storage handlers.
 *
 * ISDU Header (2 or 3 bytes):
 * [7:4] Service (Read/Write)
 * [3:0] Length (if < 15)
 * [ Index (16-bit) ]
 * [ Subindex (8-bit) ]
 */

/* ... includes ... */

/* Structs moved to header iolink_isdu_ctx_t */

/** @brief CHKPDU: XOR of all ISDU octets with the checksum octet read as 0 (A.5.6). */
static uint8_t chkpdu(const uint8_t* octets, size_t len)
{
    uint8_t c = 0U;
    for (size_t i = 0U; i < len; i++) {
        c ^= octets[i];
    }
    return c;
}

void iolink_isdu_init(iolink_isdu_ctx_t* ctx)
{
    if (!iolink_ctx_zero(ctx, sizeof(iolink_isdu_ctx_t))) {
        return;
    }
    ctx->state = ISDU_STATE_IDLE;
}

/** @brief Resolve device-local identity, retaining standalone legacy ISDU behavior. */
static const iolink_device_info_t* isdu_device_info(const iolink_isdu_ctx_t* ctx)
{
    if ((ctx != NULL) && (ctx->params_ctx != NULL) && (ctx->params_ctx->device_info != NULL)) {
        return iolink_device_info_ctx_get(ctx->params_ctx->device_info);
    }
    return iolink_device_info_get();
}

/** @brief Read a parameter via the bound params context, or the legacy global set. */
static int isdu_params_get(const iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex,
                           uint8_t* buffer, size_t max_len)
{
    if ((ctx != NULL) && (ctx->params_ctx != NULL)) {
        return iolink_params_ctx_get(ctx->params_ctx, index, subindex, buffer, max_len);
    }
    return iolink_params_get(index, subindex, buffer, max_len);
}

/** @brief Write a parameter via the bound params context, or the legacy global set. */
static int isdu_params_set(iolink_isdu_ctx_t* ctx, uint16_t index, uint8_t subindex,
                           const uint8_t* data, size_t len, bool persist)
{
    if ((ctx != NULL) && (ctx->params_ctx != NULL)) {
        return iolink_params_ctx_set(ctx->params_ctx, index, subindex, data, len, persist);
    }
    return iolink_params_set(index, subindex, data, len, persist);
}

/** @brief The bound parameter context, or the legacy singleton for standalone use. */
static iolink_params_ctx_t* isdu_params_ctx(iolink_isdu_ctx_t* ctx)
{
    if (ctx->params_ctx != NULL) {
        return ctx->params_ctx;
    }
    return iolink_params_get_legacy_ctx();
}

/** @brief DeviceAccessLocks (Index 0x000C) of this device. */
static uint16_t isdu_access_locks(const iolink_isdu_ctx_t* ctx)
{
    /* Device-local locks when a params context is bound. The standalone
       (legacy) engine keeps using the process-wide device-info value. */
    if (ctx->params_ctx != NULL) {
        return iolink_params_ctx_get_access_locks(ctx->params_ctx);
    }
    return iolink_device_info_get_access_locks();
}

/* handle_standard_commands() is defined below; the transport executes it once a
   full ISDU request has been received. */
static void handle_standard_commands(iolink_isdu_ctx_t* ctx);

/** @brief Reset the ISDU request transport to Idle. */
static void isdu_transport_reset(iolink_isdu_ctx_t* ctx)
{
    ctx->vendor_positive = false;
    ctx->state = ISDU_STATE_IDLE;
    ctx->req_idx = 0U;
    ctx->req_total = 0U;
    ctx->resp_idx = 0U;
    ctx->resp_total = 0U;
    ctx->buffer_idx = 0U;
    ctx->response_len = 0U;
    ctx->response_idx = 0U;
    ctx->flowctrl_seen = false;
    ctx->repeat_pending = false;
    ctx->read_cache_valid = false;
    ctx->last_read_od_len = 0U;
    ctx->error_code = IOLINK_ISDU_ERROR_NONE;
    ctx->header.type = IOLINK_ISDU_SERVICE_TYPE_READ;
    ctx->header.length = 0U;
    ctx->header.index = 0U;
    ctx->header.subindex = 0U;
}

/** @brief Total ISDU octet count declared by the first received octets (A.5.3/A.14).
 *
 * Returns 0 while the ExtLength octet is still missing. */
static size_t isdu_declared_total(const uint8_t* buf, size_t have)
{
    if (have == 0U) {
        return 0U;
    }
    const uint8_t len = (uint8_t) (buf[0] & 0x0FU);
    if (len == 0U) {
        return 1U; /* No Service / Busy: single octet */
    }
    if (len == 1U) {
        if (have < 2U) {
            return 0U; /* ExtLength octet not yet received */
        }
        const uint8_t ext = buf[1];
        if ((ext >= 17U) && (ext <= 238U)) {
            return ext;
        }
        return 0U; /* Reserved / invalid */
    }
    return len;
}

/**
 * @brief Decode a complete request buffer and execute the addressed service.
 *
 * Fills ctx->header and ctx->buffer from the raw octets, then dispatches via
 * handle_standard_commands(); the handler leaves a 2-octet negative marker
 * (response_buf[0]==0x80) or a positive payload in response_buf/response_len.
 */
static void isdu_execute_request(iolink_isdu_ctx_t* ctx, size_t total)
{
    ctx->vendor_positive = false;
    const uint8_t* buf = ctx->req_buf;
    const uint8_t service = (uint8_t) (buf[0] >> 4);
    size_t pos = 1U;
    const size_t payload_end = total - 1U; /* CHKPDU cannot supply an index/subindex. */

    ctx->buffer_idx = 0U;
    ctx->header.subindex = 0U;
    ctx->header.length = 0U;

    if ((service >= 1U) && (service <= 3U)) {
        ctx->header.type = IOLINK_ISDU_SERVICE_TYPE_WRITE;
    }
    else if ((service >= 9U) && (service <= 11U)) {
        ctx->header.type = IOLINK_ISDU_SERVICE_TYPE_READ;
    }
    else {
        ctx->response_buf[0] = 0x80U;
        ctx->response_buf[1] = IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
        ctx->response_len = 2U;
        ctx->state = ISDU_STATE_RESPONSE_READY;
        return;
    }

    if ((buf[0] & 0x0FU) == 1U) {
        pos++; /* Skip ExtLength octet */
    }

    /* Index format per Table A.12/A.15. Service 0x1/0x9 = 8-bit index,
       0x2/0xA = 8-bit index + subindex, 0x3/0xB = 16-bit index + subindex. */
    const bool two_index = (service == 3U) || (service == 11U);
    const bool has_sub = (service == 2U) || (service == 3U) || (service == 10U) || (service == 11U);

    if (two_index) {
        if ((pos + 1U) >= payload_end) {
            ctx->response_buf[0] = 0x80U;
            ctx->response_buf[1] = IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
            ctx->response_len = 2U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;
        }
        ctx->header.index = (uint16_t) ((uint16_t) buf[pos] << 8);
        pos++;
        ctx->header.index |= buf[pos++];
    }
    else {
        if (pos >= payload_end) {
            ctx->response_buf[0] = 0x80U;
            ctx->response_buf[1] = IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
            ctx->response_len = 2U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;
        }
        ctx->header.index = buf[pos++];
    }

    if (has_sub) {
        if (pos >= payload_end) {
            ctx->response_buf[0] = 0x80U;
            ctx->response_buf[1] = IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL;
            ctx->response_len = 2U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;
        }
        ctx->header.subindex = buf[pos++];
    }

    /* Data payload is everything up to the CHKPDU (last octet). */
    if (pos < total) {
        size_t data_end = total - 1U; /* exclude CHKPDU */
        if (data_end > pos) {
            size_t n = data_end - pos;
            if ((n > sizeof(ctx->buffer)) || (n > IOLINK_ISDU_DATA_MAX_SIZE)) {
                ctx->response_buf[0] = 0x80U;
                ctx->response_buf[1] = IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
                ctx->response_len = 2U;
                ctx->state = ISDU_STATE_RESPONSE_READY;
                return;
            }
            (void) memcpy(ctx->buffer, &buf[pos], n);
            ctx->buffer_idx = n;
        }
    }
    ctx->header.length = (uint8_t) ctx->buffer_idx;

    /* Parsed: execution is performed by iolink_isdu_process(), keeping the
       transport and the application dispatch separable (and testable). */
    ctx->state = ISDU_STATE_SERVICE_EXECUTE;
}

/** @brief Frame the handler result into ctx->resp_buf (positive or negative, A.5/C.1). */
static void isdu_frame_response(iolink_isdu_ctx_t* ctx)
{
    /* The record bound is 232 application octets, even when a shorter header
       would leave room below the 238-octet total-PDU bound. */
    if ((ctx->response_len > IOLINK_ISDU_DATA_MAX_SIZE) ||
        ((ctx->response_len + 3U) > sizeof(ctx->resp_buf))) {
        ctx->vendor_positive = false;
        ctx->response_buf[0] = 0x80U;
        ctx->response_buf[1] = IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
        ctx->response_len = 2U;
    }
    const bool negative =
        !ctx->vendor_positive && ((ctx->response_len == 2U) && (ctx->response_buf[0] == 0x80U));
    const bool write = (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE);
    size_t pos = 0U;

    if (negative) {
        ctx->resp_buf[pos++] = write ? 0x44U : 0xC4U; /* Write/Read Response (-), Length 4 */
        ctx->resp_buf[pos++] = 0x80U;                 /* ErrorCode (APP_DEV) */
        ctx->resp_buf[pos++] = ctx->response_buf[1];  /* AdditionalCode */
    }
    else if (write) {
        ctx->resp_buf[pos++] = 0x52U; /* Write Response (+) */
    }
    else {
        const size_t data_len = ctx->response_len;
        const size_t direct_total = data_len + 2U; /* I-Service + data + CHKPDU */
        if (direct_total <= 15U) {
            ctx->resp_buf[pos++] = (uint8_t) (0xD0U | (uint8_t) direct_total);
            if (data_len > 0U) {
                (void) memcpy(&ctx->resp_buf[pos], ctx->response_buf, data_len);
                pos += data_len;
            }
        }
        else {
            /* A.5.3 / Figure A.19: I-Service+Length octet, ExtLength, data, CHKPDU = data + 3. */
            const size_t total = data_len + 3U;
            ctx->resp_buf[pos++] = 0xD1U;
            ctx->resp_buf[pos++] = (uint8_t) total;
            if (data_len > 0U) {
                (void) memcpy(&ctx->resp_buf[pos], ctx->response_buf, data_len);
                pos += data_len;
            }
        }
    }

    ctx->resp_buf[pos] = 0x00U;
    ctx->resp_buf[pos] = chkpdu(ctx->resp_buf, pos + 1U);
    pos++;
    ctx->resp_total = pos;
    ctx->resp_idx = 0U;
}

/** @brief Build a Busy response octet (0x01) for a read START not yet answerable. */
static void isdu_emit_busy(iolink_isdu_ctx_t* ctx)
{
    ctx->resp_buf[0] = 0x01U;
    ctx->resp_total = 1U;
    ctx->resp_idx = 0U;
}

void iolink_isdu_od_write(iolink_isdu_ctx_t* ctx, uint8_t flowctrl, const uint8_t* od,
                          uint8_t od_len)
{
    if (ctx == NULL) {
        return;
    }

    /* Tables 52/54: IDLE/IDLE2/ABORT and reserved values 0x12..0x1F are not
       writes; they are handled by the read path. */
    if (flowctrl >= IOLINK_FLOWCTRL_IDLE) {
        return;
    }

    if (flowctrl == IOLINK_FLOWCTRL_START) {
        isdu_transport_reset(ctx);
        ctx->flowctrl_seen = true;
        ctx->last_flowctrl = flowctrl;
        ctx->state = ISDU_STATE_HEADER_INITIAL;
        ctx->req_total = 0U;
    }
    else {
        /* COUNT (0x00..0x0F): must follow a START. A repeated FlowCTRL repeats
           the previous message and its payload is ignored (7.3.6.2). */
        if (!ctx->flowctrl_seen || (ctx->state == ISDU_STATE_IDLE)) {
            ctx->error_code = IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
            isdu_transport_reset(ctx);
            return;
        }
        if (flowctrl == ctx->last_flowctrl) {
            ctx->repeat_pending = true;
            return;
        }
        const uint8_t expected = (uint8_t) ((ctx->last_flowctrl + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
        if (flowctrl != expected) {
            /* Structure violation: drop the request, go to Idle, next read
               answers No Service (0x00) (Table 54 T13). */
            ctx->error_code = IOLINK_ISDU_ERROR_SEGMENTATION;
            isdu_transport_reset(ctx);
            return;
        }
        ctx->last_flowctrl = flowctrl;
        ctx->repeat_pending = false;
    }

    if (ctx->repeat_pending) {
        ctx->repeat_pending = false;
        return;
    }

    for (uint8_t i = 0U; i < od_len; i++) {
        if (ctx->req_idx < sizeof(ctx->req_buf)) {
            ctx->req_buf[ctx->req_idx++] = od[i];
        }
        if (ctx->req_total == 0U) {
            ctx->req_total = isdu_declared_total(ctx->req_buf, ctx->req_idx);
            if ((ctx->req_idx >= 2U) && ((ctx->req_buf[0] & 0x0FU) == 1U) &&
                (ctx->req_total == 0U)) {
                /* Invalid ExtLength must not keep an unbounded partial service. */
                isdu_transport_reset(ctx);
                return;
            }
        }
        if ((ctx->req_total > 0U) && (ctx->req_idx >= ctx->req_total)) {
            break;
        }
    }

    if ((ctx->req_total > 0U) && (ctx->req_idx >= ctx->req_total)) {
        /* A.5.6: CHKPDU is valid when XOR of all ISDU octets is zero. */
        uint8_t x = 0U;
        for (size_t i = 0U; i < ctx->req_total; i++) {
            x ^= ctx->req_buf[i];
        }
        if (x != 0U) {
            ctx->error_code = IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
            ctx->header.type = IOLINK_ISDU_SERVICE_TYPE_READ;
            ctx->response_buf[0] = 0x80U;
            ctx->response_buf[1] = 0x00U; /* APP_DEV: CHKPDU mismatch (C.2.2) */
            ctx->response_len = 2U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            isdu_frame_response(ctx);
            return;
        }
        isdu_execute_request(ctx, ctx->req_total);
        if (ctx->state == ISDU_STATE_RESPONSE_READY) {
            isdu_frame_response(ctx);
        }
    }
}

void iolink_isdu_od_read(iolink_isdu_ctx_t* ctx, uint8_t flowctrl, uint8_t* od_out, uint8_t od_len)
{
    if (ctx == NULL) {
        return;
    }
    if ((flowctrl == IOLINK_FLOWCTRL_ABORT) || (flowctrl == IOLINK_FLOWCTRL_IDLE) ||
        (flowctrl == IOLINK_FLOWCTRL_IDLE2)) {
        /* Control-only calls must reset the transport even without an OD buffer. */
        isdu_transport_reset(ctx);
        if ((od_out != NULL) && (od_len <= IOLINK_OD_MAX_SIZE)) {
            (void) memset(od_out, 0, od_len);
        }
        return;
    }
    if ((od_out == NULL) || (od_len == 0U) || (od_len > IOLINK_OD_MAX_SIZE)) {
        return;
    }
    (void) memset(od_out, 0, od_len);

    if (flowctrl == IOLINK_FLOWCTRL_START) {
        /* Start of response read. If a service is being executed and the
           application has not answered, answer Busy (0x01) (Table A.14). */
        if (ctx->state == ISDU_STATE_SERVICE_EXECUTE) {
            isdu_emit_busy(ctx);
        }
        else if (ctx->state == ISDU_STATE_RESPONSE_READY) {
            /* restarted read: restart from the first response octet */
            ctx->resp_idx = 0U;
        }
        else {
            /* Idle / no pending service: No Service (0x00). */
            ctx->resp_buf[0] = 0x00U;
            ctx->resp_total = 1U;
            ctx->resp_idx = 0U;
        }
        ctx->last_flowctrl = flowctrl;
        ctx->flowctrl_seen = true;
    }
    else if (flowctrl < IOLINK_FLOWCTRL_START) {
        /* A retry repeats exactly the previous OD segment, including a final
           segment already delivered before the service returned to Idle. */
        if (!ctx->read_cache_valid) {
            return;
        }
        if (flowctrl == ctx->last_read_flowctrl) {
            if (od_len != ctx->last_read_od_len) {
                isdu_transport_reset(ctx);
                return;
            }
            (void) memcpy(od_out, ctx->last_read_od, od_len);
            return;
        }
        const uint8_t expected =
            (uint8_t) ((ctx->last_read_flowctrl + 1U) & IOLINK_FLOWCTRL_COUNT_MASK);
        if ((flowctrl != expected) || (ctx->state != ISDU_STATE_RESPONSE_READY) ||
            (ctx->resp_idx >= ctx->resp_total)) {
            isdu_transport_reset(ctx);
            return;
        }
        ctx->last_flowctrl = flowctrl;
    }
    else {
        /* Reserved values are not a valid read FlowCTRL. */
        return;
    }

    for (uint8_t i = 0U; i < od_len; i++) {
        if (ctx->resp_idx < ctx->resp_total) {
            od_out[i] = ctx->resp_buf[ctx->resp_idx++];
        }
        else {
            od_out[i] = 0x00U;
        }
    }

    const bool complete =
        (ctx->state == ISDU_STATE_RESPONSE_READY) && (ctx->resp_idx >= ctx->resp_total);
    if (complete) {
        isdu_transport_reset(ctx);
    }
    (void) memcpy(ctx->last_read_od, od_out, od_len);
    ctx->last_read_od_len = od_len;
    ctx->last_read_flowctrl = flowctrl;
    ctx->read_cache_valid = true;
}

/** @brief Terminate the service with a negative response {0x80, AdditionalCode} (Table C.1). */
static void isdu_reply_error(iolink_isdu_ctx_t* ctx, uint8_t additional_code)
{
    ctx->response_buf[0] = 0x80U;
    ctx->response_buf[1] = additional_code;
    ctx->response_len = 2U;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/** @brief Terminate the service with a positive response of len octets in response_buf. */
static void isdu_reply_data(iolink_isdu_ctx_t* ctx, size_t len)
{
    ctx->response_len = len;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/**
 * @brief Reject a nonzero Subindex or a Write on a read-only object (C.2.4, C.2.8).
 *
 * @return true when a negative response was produced and the service is done.
 */
static bool isdu_reject_read_only_access(iolink_isdu_ctx_t* ctx)
{
    if (ctx->header.subindex != 0U) {
        isdu_reply_error(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
        return true;
    }
    if (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) {
        isdu_reply_error(ctx, IOLINK_ISDU_ERROR_NOT_ACCESSIBLE);
        return true;
    }
    return false;
}

/** @brief Length of a NUL-terminated string, bounded by the StringT maximum. */
static size_t isdu_bounded_strlen(const char* str, size_t max_len)
{
    size_t n = 0U;
    while ((n < max_len) && (str[n] != '\0')) {
        n++;
    }
    return n;
}

/**
 * @brief Serve a read-only identification StringT object (B.2.8 to B.2.15, B.2.19).
 *
 * A mandatory object always exists and reads as an empty string when it is not
 * configured. An optional or conditional object without a configured value does
 * not exist: any access is IDX_NOTAVAIL.
 */
static void handle_identification_string(iolink_isdu_ctx_t* ctx, const char* str, size_t max_len,
                                         bool mandatory)
{
    if ((str == NULL) && !mandatory) {
        isdu_reply_error(ctx, IOLINK_ISDU_ERROR_INDEX_NOT_AVAIL);
        return;
    }
    if (isdu_reject_read_only_access(ctx)) {
        return;
    }
    size_t len = 0U;
    if (str != NULL) {
        len = isdu_bounded_strlen(str, max_len);
        if (len > IOLINK_ISDU_DATA_MAX_SIZE) {
            len = IOLINK_ISDU_DATA_MAX_SIZE;
        }
        (void) memcpy(ctx->response_buf, str, len);
    }
    isdu_reply_data(ctx, len);
}

/**
 * @brief Serve ProfileCharacteristic (Index 0x000D, B.2.5).
 *
 * ArrayT of UIntegerT16 ProfileIdentifiers. The object exists only when the
 * Device declares a profile; the configuration holds one identifier, so the
 * array has one element.
 */
static void handle_profile_characteristic(iolink_isdu_ctx_t* ctx, const iolink_device_info_t* info)
{
    if (info->profile_characteristic == 0U) {
        isdu_reply_error(ctx, IOLINK_ISDU_ERROR_INDEX_NOT_AVAIL);
        return;
    }
    if (isdu_reject_read_only_access(ctx)) {
        return;
    }
    ctx->response_buf[0] = (uint8_t) (info->profile_characteristic >> 8);
    ctx->response_buf[1] = (uint8_t) (info->profile_characteristic & 0xFFU);
    isdu_reply_data(ctx, 2U);
}

/**
 * @brief True if the Parameter (write) access lock refuses this write (B.2.4).
 *
 * The lock inhibits writes to every read/write parameter except
 * DeviceAccessLocks, but never blocks a Data Storage download
 * (DS_DownloadStart .. DS_DownloadEnd). Commands (0x0002) and the
 * DataStorageIndex (0x0003) are not parameters in this sense.
 */
static bool isdu_param_write_locked(const iolink_isdu_ctx_t* ctx)
{
    if ((isdu_access_locks(ctx) & IOLINK_LOCK_PARAM) == 0U) {
        return false;
    }
    const iolink_ds_ctx_t* ds = (const iolink_ds_ctx_t*) ctx->ds_ctx;
    return !((ds != NULL) && iolink_ds_download_open(ds) && ds->ds_command_session);
}

/** @brief Negative response for a rejected tag value (C.2.12, C.2.9). */
static void isdu_reply_tag_rejected(iolink_isdu_ctx_t* ctx)
{
    if (ctx->buffer_idx > IOLINK_PARAMS_TAG_MAX) {
        isdu_reply_error(ctx, IOLINK_ISDU_ERROR_LENGTH_OVERRUN);
    }
    else {
        isdu_reply_error(ctx, IOLINK_ISDU_ERROR_VALUE_OUT_OF_RANGE);
    }
}

/** @brief Read or write a user tag (ApplicationSpecificTag, FunctionTag, LocationTag). */
static void handle_tag(iolink_isdu_ctx_t* ctx, uint16_t index)
{
    if (ctx->header.subindex != 0U) {
        isdu_reply_error(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
        return;
    }
    if (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) {
        iolink_ds_ctx_t* ds = (iolink_ds_ctx_t*) ctx->ds_ctx;
        const bool in_download =
            (ds != NULL) && iolink_ds_download_open(ds) && iolink_ds_is_ds_param(index, 0U);

        if (isdu_param_write_locked(ctx)) {
            isdu_reply_error(ctx, IOLINK_ISDU_ERROR_NOT_ACCESSIBLE);
            return;
        }
        if (in_download) {
            /* Table 98 rules 1-3: check and stage only; End/Store commits. */
            if (iolink_ds_stage_param(ds, index, 0U, ctx->buffer, ctx->buffer_idx) == 0) {
                isdu_reply_data(ctx, 0U);
            }
            else {
                isdu_reply_tag_rejected(ctx);
            }
            return;
        }
        if (ctx->buffer_idx > IOLINK_PARAMS_TAG_MAX) {
            isdu_reply_tag_rejected(ctx);
            return;
        }
        if (isdu_params_set(ctx, index, 0U, ctx->buffer, ctx->buffer_idx, true) == 0) {
            isdu_reply_data(ctx, 0U);
        }
        else {
            /* The value was valid: the non-volatile write failed. Terminate
             * with a negative response instead of leaving the master polling
             * Busy; RAM and readback keep the previous value. */
            isdu_reply_error(ctx, IOLINK_ISDU_ERROR_APP_DEV);
        }
        return;
    }
    int res = isdu_params_get(ctx, index, 0U, ctx->response_buf, (size_t) IOLINK_ISDU_BUFFER_SIZE);
    if (res >= 0) {
        isdu_reply_data(ctx, (size_t) res);
    }
    else {
        isdu_reply_error(ctx, IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL);
    }
}

/** @brief Serve ErrorCount (Index 0x0020, B.2.20): read-only UIntegerT16, MSB first. */
static void handle_error_count(iolink_isdu_ctx_t* ctx)
{
    if (isdu_reject_read_only_access(ctx)) {
        return;
    }
    ctx->response_buf[0] = (uint8_t) (ctx->error_count >> 8);
    ctx->response_buf[1] = (uint8_t) (ctx->error_count & 0xFFU);
    isdu_reply_data(ctx, 2U);
}

/** @brief Serve DeviceStatus (Index 0x0024, B.2.21) through the public event API. */
static void handle_device_status(iolink_isdu_ctx_t* ctx)
{
    if (isdu_reject_read_only_access(ctx)) {
        return;
    }
    ctx->response_buf[0] = iolink_events_get_device_status((iolink_events_ctx_t*) ctx->event_ctx);
    isdu_reply_data(ctx, 1U);
}

/**
 * @brief Serve ProcessDataInput/ProcessDataOutput (Index 0x0028/0x0029, B.2.23/B.2.24).
 *
 * The response is the last valid Process Data in the same structure as on the
 * process communication channel. The object does not exist when the Device
 * has no Process Data in that direction (IDX_NOTAVAIL). Before the first valid
 * data the service is refused with SERV_NOTAVAIL (C.2.5). PDin is valid when
 * the application publishes it as valid; PDout is valid after the Master sent
 * ProcessDataOutputOperate (PDOUTVALID, Table 55). Later invalid data keeps
 * the last valid snapshot.
 */
static void handle_process_data(iolink_isdu_ctx_t* ctx, bool input)
{
    const iolink_dll_ctx_t* dll = (const iolink_dll_ctx_t*) ctx->dll_ctx;
    uint8_t pd_in_len = 0U;
    uint8_t pd_out_len = 0U;

    if (dll != NULL) {
        iolink_dll_get_pd_length(dll, &pd_in_len, &pd_out_len);
    }
    size_t len = input ? pd_in_len : pd_out_len;
    if ((dll == NULL) || (len == 0U) || (input && (len > sizeof(dll->pd_in))) ||
        (!input && (len > sizeof(dll->pd_out)))) {
        isdu_reply_error(ctx, IOLINK_ISDU_ERROR_INDEX_NOT_AVAIL);
        return;
    }
    if (isdu_reject_read_only_access(ctx)) {
        return;
    }

    iolink_critical_enter();
    const uint8_t n =
        input ? iolink_dll_get_last_valid_pd_in(dll, ctx->response_buf, sizeof(ctx->response_buf))
              : iolink_dll_get_last_valid_pd_out(dll, ctx->response_buf, sizeof(ctx->response_buf));
    iolink_critical_exit();

    if (n == 0U) {
        isdu_reply_error(ctx, IOLINK_ISDU_ERROR_SERV_TEMP_UNAVAIL);
        return;
    }
    isdu_reply_data(ctx, n);
}

/** @brief Serve the Table B.8 identification, tag, status and Process Data indices. */
static void handle_mandatory_indices(iolink_isdu_ctx_t* ctx)
{
    const iolink_device_info_t* info = isdu_device_info(ctx);

    if (info == NULL) {
        isdu_reply_error(ctx, IOLINK_ISDU_ERROR_INDEX_NOT_AVAIL);
        return;
    }

    /* Indices 0x0004-0x000B are reserved (Table B.8): VendorID and DeviceID
       are read through Direct Parameter page 1, so they reach the default. */
    switch (ctx->header.index) {
        case IOLINK_IDX_PROFILE_CHARACTERISTIC:
            handle_profile_characteristic(ctx, info);
            break;
        case IOLINK_IDX_VENDOR_NAME:
            handle_identification_string(ctx, info->vendor_name, IOLINK_STRING_MAX_LEN, true);
            break;
        case IOLINK_IDX_VENDOR_TEXT:
            handle_identification_string(ctx, info->vendor_text, IOLINK_STRING_MAX_LEN, false);
            break;
        case IOLINK_IDX_PRODUCT_NAME:
            handle_identification_string(ctx, info->product_name, IOLINK_STRING_MAX_LEN, true);
            break;
        case IOLINK_IDX_PRODUCT_ID:
            handle_identification_string(ctx, info->product_id, IOLINK_STRING_MAX_LEN, false);
            break;
        case IOLINK_IDX_PRODUCT_TEXT:
            handle_identification_string(ctx, info->product_text, IOLINK_STRING_MAX_LEN, false);
            break;
        case IOLINK_IDX_SERIAL_NUMBER:
            handle_identification_string(ctx, info->serial_number, IOLINK_SERIAL_NUMBER_MAX_LEN,
                                         false);
            break;
        case IOLINK_IDX_HARDWARE_REVISION:
            handle_identification_string(ctx, info->hardware_revision, IOLINK_STRING_MAX_LEN,
                                         false);
            break;
        case IOLINK_IDX_FIRMWARE_REVISION:
            handle_identification_string(ctx, info->firmware_revision, IOLINK_STRING_MAX_LEN,
                                         false);
            break;
        case IOLINK_IDX_PRODUCT_URI:
            handle_identification_string(ctx, info->product_uri, IOLINK_PRODUCT_URI_MAX_LEN, false);
            break;
        case IOLINK_IDX_APPLICATION_TAG:
        case IOLINK_IDX_FUNCTION_TAG:
        case IOLINK_IDX_LOCATION_TAG:
            handle_tag(ctx, ctx->header.index);
            break;
        case IOLINK_IDX_ERROR_COUNT:
            handle_error_count(ctx);
            break;
        case IOLINK_IDX_DEVICE_STATUS:
            handle_device_status(ctx);
            break;
        case IOLINK_IDX_PROCESS_DATA_INPUT:
            handle_process_data(ctx, true);
            break;
        case IOLINK_IDX_PROCESS_DATA_OUTPUT:
            handle_process_data(ctx, false);
            break;

            /* IOLINK_IDX_DETAILED_DEVICE_STATUS (0x0025) is handled earlier in
               handle_standard_commands() via handle_detailed_device_status(). */

        default:
            isdu_reply_error(ctx, IOLINK_ISDU_ERROR_INDEX_NOT_AVAIL);
            break;
    }
}

/** @brief AdditionalCode for a failed download commit (Table 98 rule 7, C.2.17). */
static uint8_t isdu_ds_commit_error(const iolink_ds_ctx_t* ds)
{
    /* A rejected parameter set is 0x8041. A non-volatile write failure is
       not a parameter check failure: report a Device application error. */
    if (ds->last_result == IOLINK_DS_ERR_STORAGE) {
        return IOLINK_ISDU_ERROR_APP_DEV;
    }
    return IOLINK_ISDU_ERROR_PARAM_INCONSISTENT;
}

/** @brief Map a Data Storage command result to the Table 99 ISDU response. */
static void isdu_reply_ds_result(iolink_isdu_ctx_t* ctx, const iolink_ds_ctx_t* ds, int ret)
{
    if (ret == IOLINK_DS_OK) {
        isdu_reply_data(ctx, 0U);
    }
    else if ((ret == IOLINK_DS_CMD_BUSY) || (ret == IOLINK_DS_CMD_DENIED)) {
        /* The command is supported but the current state does not permit it
           (Table 99, C.2.15). */
        isdu_reply_error(ctx, IOLINK_ISDU_ERROR_FUNC_TEMP_UNAVAIL);
    }
    else if (ret == IOLINK_DS_CMD_COMMIT_FAILED) {
        isdu_reply_error(ctx, isdu_ds_commit_error(ds));
    }
    else {
        isdu_reply_error(ctx, IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL);
    }
}

/**
 * @brief Raise the Data Storage upload request (10.4.2, Table 100 T2/T7).
 *
 * DS_UPLOAD_FLAG is set in every case. The DS_UPLOAD_REQ Event (single shot
 * Notification 0xFF91) is suppressed while the Data Storage lock is set.
 */
static void isdu_request_ds_upload(iolink_isdu_ctx_t* ctx, iolink_ds_ctx_t* ds, uint16_t locks)
{
    if (ds != NULL) {
        ds->upload_requested = true;
    }
    if (((locks & IOLINK_LOCK_DS) == 0U) && (ctx->event_ctx != NULL)) {
        (void) iolink_event_notify((iolink_events_ctx_t*) ctx->event_ctx,
                                   IOLINK_EVENTCODE_DS_UPLOAD_REQUEST);
    }
}

/**
 * @brief Execute a Block Parameterization SystemCommand 0x01-0x06 (10.3.5, Table B.9).
 *
 * The commands are routed to the Data Storage engine. Without a bound Data
 * Storage engine the commands are not implemented (FUNC_NOTAVAIL). An End
 * command without an open block in the same direction is refused with
 * FUNC_UNAVAILTEMP. A failed End/Store check is refused with 0x8041 and the
 * previous parameter set stays active (Table 98 rule 7). The Data Storage
 * access lock does not apply to Block Parameterization (B.2.4 limits it to
 * the DataStorageIndex); it only suppresses the DS_UPLOAD_REQ Event.
 */
static void handle_param_command(iolink_isdu_ctx_t* ctx, uint8_t cmd)
{
    iolink_ds_ctx_t* ds = (iolink_ds_ctx_t*) ctx->ds_ctx;
    if (ds == NULL) {
        isdu_reply_error(ctx, IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL);
        return;
    }
    const uint16_t locks = isdu_access_locks(ctx);
    const uint16_t block_locks = (uint16_t) (locks & (uint16_t) ~IOLINK_LOCK_DS);

    switch (cmd) {
        case IOLINK_CMD_PARAM_UPLOAD_END:
            if (!iolink_ds_upload_open(ds)) {
                isdu_reply_error(ctx, IOLINK_ISDU_ERROR_FUNC_TEMP_UNAVAIL);
                return;
            }
            break;

        case IOLINK_CMD_PARAM_DOWNLOAD_END:
            if (!iolink_ds_download_open(ds)) {
                isdu_reply_error(ctx, IOLINK_ISDU_ERROR_FUNC_TEMP_UNAVAIL);
                return;
            }
            break;

        case IOLINK_CMD_PARAM_DOWNLOAD_STORE: {
            /* ParamDownloadEnd plus a Data Storage upload request through the
               DS_UPLOAD_REQ Event (10.3.5, 10.4.2). The command is also valid
               without an open block, after single parameter writes. */
            if (ctx->event_ctx == NULL) {
                isdu_reply_error(ctx, IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL);
                return;
            }
            if (iolink_ds_upload_open(ds)) {
                isdu_reply_error(ctx, IOLINK_ISDU_ERROR_FUNC_TEMP_UNAVAIL);
                return;
            }
            const int ret =
                iolink_ds_handle_command(ds, IOLINK_CMD_PARAM_DOWNLOAD_STORE, block_locks);
            if (ret != IOLINK_DS_OK) {
                /* Table 98 rule 7: no Data Storage upload request. */
                isdu_reply_ds_result(ctx, ds, ret);
                return;
            }
            isdu_request_ds_upload(ctx, ds, locks);
            isdu_reply_data(ctx, 0U);
            return;
        }

        case IOLINK_CMD_PARAM_UPLOAD_START:
        case IOLINK_CMD_PARAM_DOWNLOAD_START:
        case IOLINK_CMD_PARAM_BREAK:
        default:
            break;
    }

    isdu_reply_ds_result(ctx, ds, iolink_ds_handle_command(ds, cmd, block_locks));
}

/** @brief True if the reset service supports @p cmd (query only, no action). */
static bool isdu_reset_supported(const iolink_isdu_ctx_t* ctx, uint8_t cmd)
{
    return (ctx->system_reset != NULL) &&
           (ctx->system_reset(ctx->system_reset_user, cmd, false) == 0);
}

/**
 * @brief Restore the stack-owned factory state (10.7.4, Table 101).
 *
 * Tags and DeviceAccessLocks return to their defaults with one checked NVM
 * write. Only after that write succeeds: ErrorCount is cleared, active Events
 * are sent with "disappears", DeviceStatus/DetailedDeviceStatus are cleared,
 * an open block parameter transfer is discarded and the DS upload request
 * (DS_UPLOAD_FLAG) is deleted.
 *
 * @return true on success; false if the NVM write failed (nothing changed).
 */
static bool isdu_restore_factory_state(iolink_isdu_ctx_t* ctx)
{
    if (iolink_params_ctx_factory_reset_checked(isdu_params_ctx(ctx)) != 0) {
        return false;
    }
    ctx->error_count = 0U;
    iolink_events_clear_status((iolink_events_ctx_t*) ctx->event_ctx);
    iolink_ds_ctx_t* ds = (iolink_ds_ctx_t*) ctx->ds_ctx;
    if (ds != NULL) {
        (void) iolink_ds_abort(ds);
        ds->upload_requested = false;
        (void) iolink_ds_build_image(ds);
    }
    return true;
}

/**
 * @brief Execute a SystemCommand (Index 0x0002, Table B.9).
 *
 * The response follows the execution of the requested action (10.7). A
 * command whose action this Device cannot execute is refused with
 * FUNC_NOTAVAIL and has no effect. A non-volatile write failure is refused
 * with APP_DEV (0x8000) and leaves the previous state. Reserved, profile and
 * vendor values are answered with FUNC_NOTAVAIL (Table 99, C.2.14).
 */
static void handle_system_command(iolink_isdu_ctx_t* ctx, uint8_t cmd)
{
    iolink_ds_ctx_t* ds = (iolink_ds_ctx_t*) ctx->ds_ctx;

    switch (cmd) {
        case IOLINK_CMD_PARAM_UPLOAD_START:
        case IOLINK_CMD_PARAM_UPLOAD_END:
        case IOLINK_CMD_PARAM_DOWNLOAD_START:
        case IOLINK_CMD_PARAM_DOWNLOAD_END:
        case IOLINK_CMD_PARAM_DOWNLOAD_STORE:
        case IOLINK_CMD_PARAM_BREAK:
            handle_param_command(ctx, cmd);
            return;

        case IOLINK_CMD_DEVICE_RESET: /* 0x80, 10.7.2 */
            /* The warm start interrupts communication, so it runs after the
               response was read and 3 MasterCycles passed (device layer). */
            if (!isdu_reset_supported(ctx, cmd)) {
                break;
            }
            (void) iolink_ds_abort(ds); /* Table 101: block transfer discarded */
            ctx->reset_pending = true;
            isdu_reply_data(ctx, 0U);
            return;

        case IOLINK_CMD_APPLICATION_RESET: /* 0x81, 10.7.3 */
            if (!isdu_reset_supported(ctx, cmd)) {
                break;
            }
            /* Table 101: Access locks "Default" (checked NVM write first). */
            if (iolink_params_ctx_reset_access_locks(isdu_params_ctx(ctx)) != 0) {
                isdu_reply_error(ctx, IOLINK_ISDU_ERROR_APP_DEV);
                return;
            }
            (void) iolink_ds_abort(ds);
            (void) ctx->system_reset(ctx->system_reset_user, cmd, true);
            ctx->app_reset_pending = true;
            /* Table 101: Data Storage "Upload required", DS_UPLOAD_REQ. */
            isdu_request_ds_upload(ctx, ds, isdu_access_locks(ctx));
            isdu_reply_data(ctx, 0U);
            return;

        case IOLINK_CMD_RESTORE_FACTORY_SETTINGS: /* 0x82, 10.7.4 */
            if (!isdu_restore_factory_state(ctx)) {
                isdu_reply_error(ctx, IOLINK_ISDU_ERROR_APP_DEV);
                return;
            }
            /* Application parameters, if the application registered a reset
               service. Direct Parameter page 1 is not changed by this reset,
               so no communication restart is required. */
            if (isdu_reset_supported(ctx, cmd)) {
                (void) ctx->system_reset(ctx->system_reset_user, cmd, true);
            }
            isdu_reply_data(ctx, 0U);
            return;

        case IOLINK_CMD_BACK_TO_BOX: /* 0x83, 10.7.5 */
            /* Requires the application to disable the output drivers. */
            if (!isdu_reset_supported(ctx, cmd)) {
                break;
            }
            if (!isdu_restore_factory_state(ctx)) {
                isdu_reply_error(ctx, IOLINK_ISDU_ERROR_APP_DEV);
                return;
            }
            ctx->back_to_box_pending = true;
            isdu_reply_data(ctx, 0U);
            return;

        default:
            break;
    }

    isdu_reply_error(ctx, IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL);
}

/**
 * @brief Read or write DeviceAccessLocks (Index 0x000C, B.2.4).
 *
 * Subindex 0 only; the value is 2 octets, MSB first. Reserved bits 4-15 are
 * refused with PAR_VALOUTOFRNG. A write is stored in non-volatile memory
 * first; a failed NVM write is refused with APP_DEV and keeps the old value.
 */
static void handle_access_locks(iolink_isdu_ctx_t* ctx)
{
    if (ctx->header.subindex != 0U) {
        isdu_reply_error(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
        return;
    }
    if (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_READ) {
        const uint16_t locks = isdu_access_locks(ctx);
        ctx->response_buf[0] = (uint8_t) (locks >> 8);
        ctx->response_buf[1] = (uint8_t) (locks & 0xFFU);
        isdu_reply_data(ctx, 2U);
        return;
    }
    if (ctx->buffer_idx < 2U) {
        isdu_reply_error(ctx, IOLINK_ISDU_ERROR_LENGTH_UNDERRUN);
        return;
    }
    if (ctx->buffer_idx > 2U) {
        isdu_reply_error(ctx, IOLINK_ISDU_ERROR_LENGTH_OVERRUN);
        return;
    }
    const uint16_t new_locks = (uint16_t) (((uint16_t) ctx->buffer[0] << 8) | ctx->buffer[1]);
    if ((new_locks & (uint16_t) ~IOLINK_PARAMS_ACCESS_LOCKS_DEFINED) != 0U) {
        isdu_reply_error(ctx, IOLINK_ISDU_ERROR_VALUE_OUT_OF_RANGE);
        return;
    }
    /* The standalone engine uses the legacy context, which mirrors the value
       into the process-wide device info. */
    if (iolink_params_ctx_set_access_locks(isdu_params_ctx(ctx), new_locks, true) != 0) {
        isdu_reply_error(ctx, IOLINK_ISDU_ERROR_APP_DEV);
        return;
    }
    isdu_reply_data(ctx, 0U);
}

/** @brief Serve DetailedDeviceStatus (Table B.14): one [EventQualifier]
 *  [EventCode MSB][EventCode LSB] record per pending "appears" Warning/Error. */
static void handle_detailed_device_status(iolink_isdu_ctx_t* ctx)
{
    if (isdu_reject_read_only_access(ctx)) {
        return;
    }

    /* The event manager produces bounded entries for active Error/Warning
       conditions, not a snapshot of acknowledged queue entries. */
    if (ctx->event_ctx == NULL) {
        isdu_reply_data(ctx, 0U);
        return;
    }

    const size_t capacity = sizeof(ctx->response_buf);
    const uint16_t limit = (capacity > UINT16_MAX) ? UINT16_MAX : (uint16_t) capacity;
    const size_t count = iolink_events_get_detailed_status((iolink_events_ctx_t*) ctx->event_ctx,
                                                           ctx->response_buf, limit);
    isdu_reply_data(ctx, count);
}

/** @brief Append a 32-bit value to a buffer in big-endian order, advancing the index. */
static void isdu_write_u32_be(uint8_t* buf, size_t* idx, uint32_t value)
{
    buf[(*idx)++] = (uint8_t) ((value >> 24) & 0xFFU);
    buf[(*idx)++] = (uint8_t) ((value >> 16) & 0xFFU);
    buf[(*idx)++] = (uint8_t) ((value >> 8) & 0xFFU);
    buf[(*idx)++] = (uint8_t) (value & 0xFFU);
}

/** @brief Serve the vendor error-statistics index as four big-endian DLL counters. */
static void handle_error_stats(iolink_isdu_ctx_t* ctx)
{
    if (ctx->header.type != IOLINK_ISDU_SERVICE_TYPE_READ) {
        ctx->response_buf[0] = 0x80U;
        ctx->response_buf[1] = IOLINK_ISDU_ERROR_WRITE_PROTECTED;
        ctx->response_len = 2U;
        return;
    }

    if (ctx->header.subindex != 0U) {
        ctx->response_buf[0] = 0x80U;
        ctx->response_buf[1] = IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL;
        ctx->response_len = 2U;
        return;
    }

    if (ctx->dll_ctx == NULL) {
        ctx->response_buf[0] = 0x80U;
        ctx->response_buf[1] = IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
        ctx->response_len = 2U;
        return;
    }

    iolink_dll_stats_t stats;
    iolink_dll_get_stats((const iolink_dll_ctx_t*) ctx->dll_ctx, &stats);

    size_t idx = 0U;
    isdu_write_u32_be(ctx->response_buf, &idx, stats.crc_errors);
    isdu_write_u32_be(ctx->response_buf, &idx, stats.timeout_errors);
    isdu_write_u32_be(ctx->response_buf, &idx, stats.framing_errors);
    isdu_write_u32_be(ctx->response_buf, &idx, stats.timing_errors);
    ctx->response_len = (uint8_t) idx;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/** @brief Return the Direct Parameter page 2 storage (device-specific, addresses 0x10-0x1F). */
static uint8_t* direct_param_page2(iolink_isdu_ctx_t* ctx)
{
    static uint8_t fallback_page2[16] = {0U};

    if ((ctx != NULL) && (ctx->direct_param_page2 != NULL)) {
        return (uint8_t*) ctx->direct_param_page2;
    }
    return fallback_page2;
}

/**
 * @brief Encode a Process Data length (in octets) per IO-Link V1.1.5 Figure B.5.
 *
 * <=2 octets are expressed as bit length (BYTE=0); larger as octets (BYTE=1).
 */
static uint8_t direct_param_encode_pd(uint8_t octets)
{
    if (octets == 0U) {
        return 0x00U;
    }
    if (octets <= 2U) {
        return (uint8_t) (octets * 8U); /* BYTE=0, Length in bits (8 or 16) */
    }
    return (uint8_t) (0x80U | (uint8_t) (octets - 1U)); /* BYTE=1, Length=octets-1 */
}

/**
 * @brief Build the M-sequenceCapability byte (Direct Parameter addr 0x03, Figure B.3).
 *
 * bit0 = ISDU supported, bits1-3 = OPERATE M-sequence code (Table A.10),
 * bits4-5 = PREOPERATE M-sequence code (Table A.8; TYPE_0 = 0 here).
 */
static uint8_t direct_param_mseq_capability(uint8_t m_seq_type, uint8_t od_len)
{
    uint8_t cap = 0x01U; /* ISDU supported */
    uint8_t operate_code;
    switch (m_seq_type) {
        case IOLINK_M_SEQ_TYPE_1_2:
            operate_code = 1U; /* 2 OD octets, no PD */
            break;
        case IOLINK_M_SEQ_TYPE_1_V:
            operate_code = 6U; /* 8 OD octets, no PD */
            break;
        case IOLINK_M_SEQ_TYPE_2_V:
            operate_code = (od_len == 1U) ? 4U : 5U; /* OD 1/2, variable PD */
            break;
        case IOLINK_M_SEQ_TYPE_1_1:
        case IOLINK_M_SEQ_TYPE_2_1:
        case IOLINK_M_SEQ_TYPE_2_2:
        case IOLINK_M_SEQ_TYPE_0:
        default:
            operate_code = 0U; /* Type 0 / Type 2_x with 1 OD octet */
            break;
    }
    cap |= (uint8_t) ((operate_code & 0x07U) << 1);
    /* PREOPERATE M-sequence in this stack is TYPE_0 (code 0) -> bits 4-5 = 0. */
    return cap;
}

/** @brief Populate the 16-octet Direct Parameter page 1 from device-info and DLL state. */
static void build_direct_param_page1(iolink_isdu_ctx_t* ctx, uint8_t* page)
{
    const iolink_device_info_t* info = isdu_device_info(ctx);
    uint8_t pd_in = 0U;
    uint8_t pd_out = 0U;
    uint8_t m_seq = 0U;
    uint8_t od_len = 1U;
    if (ctx->dll_ctx != NULL) {
        const iolink_dll_ctx_t* dll = (const iolink_dll_ctx_t*) ctx->dll_ctx;
        iolink_dll_get_pd_length(dll, &pd_in, &pd_out);
        m_seq = dll->m_seq_type;
        od_len = dll->od_len;
    }

    (void) memset(page, 0, 16);
    /* 0x00 MasterCommand (W) and 0x01 MasterCycleTime default to 0. */
    page[0x02] = (info != NULL) ? iolink_cycle_time_encode((uint16_t) info->min_cycle_time)
                                : 0U;                         /* MinCycleTime (Table B.3) */
    page[0x03] = direct_param_mseq_capability(m_seq, od_len); /* M-sequenceCapability */
    page[0x04] = 0x11U;                                       /* RevisionID = v1.1 */
    page[0x05] = direct_param_encode_pd(pd_in);               /* ProcessDataIn */
    page[0x06] = direct_param_encode_pd(pd_out);              /* ProcessDataOut */
    if (info != NULL) {
        page[0x07] = (uint8_t) (info->vendor_id >> 8);            /* VendorID MSB */
        page[0x08] = (uint8_t) (info->vendor_id & 0xFFU);         /* VendorID LSB */
        page[0x09] = (uint8_t) ((info->device_id >> 16) & 0xFFU); /* DeviceID octet2 */
        page[0x0A] = (uint8_t) ((info->device_id >> 8) & 0xFFU);  /* DeviceID octet1 */
        page[0x0B] = (uint8_t) (info->device_id & 0xFFU);         /* DeviceID octet0 */
    }
    /* 0x0C-0x0E reserved (0); 0x0F SystemCommand (W) reads 0. */
}

uint8_t iolink_isdu_direct_param_page1_octet(iolink_isdu_ctx_t* ctx, uint8_t addr)
{
    uint8_t page[16];

    if ((ctx == NULL) || (addr >= sizeof(page))) {
        return 0U;
    }

    build_direct_param_page1(ctx, page);
    return page[addr];
}

/** @brief Read/write Direct Parameter pages 1 (read-only) and 2 (device-specific). */
static void handle_direct_parameters(iolink_isdu_ctx_t* ctx)
{
    bool page2 = (ctx->header.index == IOLINK_IDX_DIRECT_PARAMETERS_2);
    uint8_t sub = ctx->header.subindex;
    uint8_t* page2_storage = direct_param_page2(ctx);

    if (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) {
        if (!page2) {
            /* Page 1 is read-only. */
            ctx->response_buf[0] = 0x80U;
            ctx->response_buf[1] = IOLINK_ISDU_ERROR_WRITE_PROTECTED;
            ctx->response_len = 2U;
        }
        else if (isdu_param_write_locked(ctx)) {
            ctx->response_buf[0] = 0x80U;
            ctx->response_buf[1] = IOLINK_ISDU_ERROR_NOT_ACCESSIBLE;
            ctx->response_len = 2U;
        }
        else if (sub == 0U) {
            size_t n = ctx->buffer_idx;
            if (n > 16U) {
                n = 16U;
            }
            (void) memcpy(page2_storage, ctx->buffer, n);
            ctx->response_len = 0U;
        }
        else if ((sub <= 16U) && (ctx->buffer_idx >= 1U)) {
            page2_storage[sub - 1U] = ctx->buffer[0];
            ctx->response_len = 0U;
        }
        else {
            ctx->response_buf[0] = 0x80U;
            ctx->response_buf[1] = IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL;
            ctx->response_len = 2U;
        }
    }
    else {
        uint8_t page[16];
        if (page2) {
            (void) memcpy(page, page2_storage, sizeof(page));
        }
        else {
            build_direct_param_page1(ctx, page);
        }

        if (sub == 0U) {
            (void) memcpy(ctx->response_buf, page, sizeof(page));
            ctx->response_len = sizeof(page);
        }
        else if (sub <= 16U) {
            ctx->response_buf[0] = page[sub - 1U]; /* Unimplemented bytes already read 0 */
            ctx->response_len = 1U;
        }
        else {
            ctx->response_buf[0] = 0x80U;
            ctx->response_buf[1] = IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL;
            ctx->response_len = 2U;
        }
    }
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/** @brief Write a 32-bit value MSB first into response_buf and reply. */
static void isdu_reply_u32(iolink_isdu_ctx_t* ctx, uint32_t value)
{
    size_t idx = 0U;
    isdu_write_u32_be(ctx->response_buf, &idx, value);
    isdu_reply_data(ctx, idx);
}

/** @brief Write to DataStorageIndex 0x0003 (Table B.10). */
static void handle_data_storage_write(iolink_isdu_ctx_t* ctx, iolink_ds_ctx_t* ds)
{
    /* B.2.4, Table 100 DSLocked_1: writes are refused with "access denied". */
    if ((isdu_access_locks(ctx) & IOLINK_LOCK_DS) != 0U) {
        isdu_reply_error(ctx, IOLINK_ISDU_ERROR_NOT_ACCESSIBLE);
        return;
    }

    switch (ctx->header.subindex) {
        case 0U:
            /* Device-specific extension: the serialized parameter image. In a
               download session it is checked and staged only; End/Store
               commits. Outside a session it is checked and committed with
               one NVM write. */
            if (iolink_ds_download_open(ds)) {
                if (iolink_ds_stage_image(ds, ctx->buffer, ctx->buffer_idx) == IOLINK_DS_OK) {
                    isdu_reply_data(ctx, 0U);
                }
                else {
                    isdu_reply_error(ctx, IOLINK_ISDU_ERROR_PARAM_INCONSISTENT);
                }
                return;
            }
            {
                const int ret = iolink_ds_apply_image(ds, ctx->buffer, ctx->buffer_idx);
                if (ret == IOLINK_DS_OK) {
                    isdu_reply_data(ctx, 0U);
                }
                else if (ret == IOLINK_DS_ERR_STORAGE) {
                    isdu_reply_error(ctx, IOLINK_ISDU_ERROR_APP_DEV);
                }
                else {
                    isdu_reply_error(ctx, IOLINK_ISDU_ERROR_PARAM_INCONSISTENT);
                }
            }
            return;

        case IOLINK_DS_SUBIDX_COMMAND: {
            if (ctx->buffer_idx < 1U) {
                isdu_reply_error(ctx, IOLINK_ISDU_ERROR_LENGTH_UNDERRUN);
                return;
            }
            if (ctx->buffer_idx > 1U) {
                isdu_reply_error(ctx, IOLINK_ISDU_ERROR_LENGTH_OVERRUN);
                return;
            }
            const int ret = iolink_ds_handle_ds_command(ds, ctx->buffer[0]);
            if (ret == IOLINK_DS_CMD_UNKNOWN) {
                isdu_reply_error(ctx, IOLINK_ISDU_ERROR_VALUE_OUT_OF_RANGE);
            }
            else {
                /* Table 100 DSIdle_2: an unhandled command is 0x8036. */
                isdu_reply_ds_result(ctx, ds, ret);
            }
            return;
        }

        case IOLINK_DS_SUBIDX_STATE_PROPERTY:
        case IOLINK_DS_SUBIDX_STORAGE_SIZE:
        case IOLINK_DS_SUBIDX_PARAM_CHECKSUM:
        case IOLINK_DS_SUBIDX_INDEX_LIST:
            isdu_reply_error(ctx, IOLINK_ISDU_ERROR_NOT_ACCESSIBLE);
            return;

        default:
            isdu_reply_error(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
            return;
    }
}

/** @brief Read DataStorageIndex 0x0003 (Table B.10). */
static void handle_data_storage_read(iolink_isdu_ctx_t* ctx, iolink_ds_ctx_t* ds)
{
    switch (ctx->header.subindex) {
        case 0U: {
            /* Device-specific extension: the serialized parameter image. */
            size_t len = 0U;
            const uint8_t* img = iolink_ds_get_image(ds, &len);
            if ((img == NULL) || (len > sizeof(ctx->response_buf))) {
                isdu_reply_error(ctx, IOLINK_ISDU_ERROR_APP_DEV);
                return;
            }
            if (len > 0U) {
                (void) memcpy(ctx->response_buf, img, len);
            }
            isdu_reply_data(ctx, len);
            return;
        }
        case IOLINK_DS_SUBIDX_COMMAND:
            /* Table B.10: a read returns an unspecified value. */
            ctx->response_buf[0] = 0x00U;
            isdu_reply_data(ctx, 1U);
            return;
        case IOLINK_DS_SUBIDX_STATE_PROPERTY:
            ctx->response_buf[0] = iolink_ds_state_property(ds, isdu_access_locks(ctx));
            isdu_reply_data(ctx, 1U);
            return;
        case IOLINK_DS_SUBIDX_STORAGE_SIZE:
            isdu_reply_u32(ctx, iolink_ds_storage_size());
            return;
        case IOLINK_DS_SUBIDX_PARAM_CHECKSUM:
            /* Rebuilt from the actual parameters, never from staged data. */
            if (iolink_ds_build_image(ds) < 0) {
                isdu_reply_error(ctx, IOLINK_ISDU_ERROR_APP_DEV);
                return;
            }
            isdu_reply_u32(ctx, (uint32_t) ds->current_checksum);
            return;
        case IOLINK_DS_SUBIDX_INDEX_LIST: {
            const size_t len = iolink_ds_index_list(ctx->response_buf, sizeof(ctx->response_buf));
            if (len == 0U) {
                isdu_reply_error(ctx, IOLINK_ISDU_ERROR_APP_DEV);
                return;
            }
            isdu_reply_data(ctx, len);
            return;
        }
        default:
            isdu_reply_error(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
            return;
    }
}

/**
 * @brief Serve DataStorageIndex 0x0003 (B.2.3, Table B.10).
 *
 * Subindices 1-5 follow Table B.10 (DS_Command, State_Property,
 * Data_Storage_Size, Parameter_Checksum, Index_List). Subindex 0 is a
 * device-specific extension that carries the serialized parameter image
 * (see data_storage.h); it is not the Table F.19 record layout.
 */
static void handle_data_storage(iolink_isdu_ctx_t* ctx)
{
    iolink_ds_ctx_t* ds = (iolink_ds_ctx_t*) ctx->ds_ctx;
    if (ds == NULL) {
        isdu_reply_error(ctx, IOLINK_ISDU_ERROR_INDEX_NOT_AVAIL);
        return;
    }
    if (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) {
        handle_data_storage_write(ctx, ds);
    }
    else {
        handle_data_storage_read(ctx, ds);
    }
}

/** @brief Dispatch an executed ISDU request to the handler for its index. */
static void handle_standard_commands(iolink_isdu_ctx_t* ctx)
{
    ctx->vendor_positive = false;
    /* B.2.27: Preferred Indices 0x0040-0x00FE are device-specific like the
       Extended Index range from 0x0100; both reach the application vendor
       service. 0x00FF itself is reserved. */
    const uint16_t index = ctx->header.index;
    const bool device_specific = ((index >= 0x0040U) && (index <= 0x00FEU)) || (index >= 0x0100U);
    if (device_specific && (ctx->vendor_service != NULL) &&
        (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) && isdu_param_write_locked(ctx)) {
        /* B.2.4: device-specific parameters are write-locked as well. */
        isdu_reply_error(ctx, IOLINK_ISDU_ERROR_NOT_ACCESSIBLE);
    }
    else if (device_specific && (ctx->vendor_service != NULL)) {
#if IOLINK_ISDU_BUFFER_SIZE > (IOLINK_ISDU_DATA_MAX_SIZE + 3U)
        size_t length = IOLINK_ISDU_DATA_MAX_SIZE;
#else
        size_t length = sizeof(ctx->resp_buf) - 3U;
#endif
        const size_t capacity = length;
        uint8_t error =
            ctx->vendor_service(ctx->vendor_user, ctx->header.index, ctx->header.subindex,
                                ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE, ctx->buffer,
                                ctx->buffer_idx, ctx->response_buf, &length);
        if (length > capacity) {
            error = IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
        }
        if (error != 0U) {
            ctx->response_buf[0] = 0x80U;
            ctx->response_buf[1] = error;
            length = 2U;
        }
        else {
            ctx->vendor_positive = true;
        }
        ctx->response_len = length;
        ctx->response_idx = 0U;
        ctx->state = ISDU_STATE_RESPONSE_READY;
    }
    else if (ctx->header.index == IOLINK_IDX_SYSTEM_COMMAND) {
        if (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) {
            /* SystemCommand is one UIntegerT octet without Subindex structure
               (Table B.8); wrong lengths are VAL_LENUNDRUN/VAL_LENOVRRUN. */
            if (ctx->header.subindex != 0U) {
                isdu_reply_error(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
            }
            else if (ctx->buffer_idx == 0U) {
                isdu_reply_error(ctx, IOLINK_ISDU_ERROR_LENGTH_UNDERRUN);
            }
            else if (ctx->buffer_idx > 1U) {
                isdu_reply_error(ctx, IOLINK_ISDU_ERROR_LENGTH_OVERRUN);
            }
            else {
                handle_system_command(ctx, ctx->buffer[0]);
            }
        }
        else {
            /* SystemCommand (Table B.8) is write-only; a read is answered with
               IDX_NOT_ACCESSIBLE (Table C.1). Events are read through the
               Diagnosis-channel event memory (Table 58). */
            ctx->response_buf[0] = 0x80U;
            ctx->response_buf[1] = IOLINK_ISDU_ERROR_NOT_ACCESSIBLE;
            ctx->response_len = 2U;
            ctx->response_idx = 0U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
        }
    }
    else if ((ctx->header.index == IOLINK_IDX_DIRECT_PARAMETERS_1) ||
             (ctx->header.index == IOLINK_IDX_DIRECT_PARAMETERS_2)) {
        handle_direct_parameters(ctx);
    }
    else if (ctx->header.index == IOLINK_IDX_DATA_STORAGE) {
        handle_data_storage(ctx);
    }
    else if (ctx->header.index == IOLINK_IDX_DEVICE_ACCESS_LOCKS) {
        handle_access_locks(ctx);
    }
    else if (ctx->header.index == IOLINK_IDX_DETAILED_DEVICE_STATUS) {
        handle_detailed_device_status(ctx);
    }
    else if (ctx->header.index == IOLINK_IDX_ERROR_STATS) {
        handle_error_stats(ctx);
        ctx->response_idx = 0U;
        ctx->state = ISDU_STATE_RESPONSE_READY;
    }
    else {
        handle_mandatory_indices(ctx);
    }
}

void iolink_isdu_process(iolink_isdu_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->state == ISDU_STATE_SERVICE_EXECUTE) {
        handle_standard_commands(ctx);
        if (ctx->state != ISDU_STATE_RESPONSE_READY) {
            isdu_transport_reset(ctx);
        }
        else {
            isdu_frame_response(ctx);
        }
    }
}

int iolink_isdu_get_response_byte(iolink_isdu_ctx_t* ctx, uint8_t* byte)
{
    if ((ctx == NULL) || (byte == NULL)) {
        return 0;
    }
    if (ctx->response_idx >= ctx->response_len) {
        return 0;
    }
    *byte = ctx->response_buf[ctx->response_idx++];
    return 1;
}
