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
    if ((ctx != NULL) && (ctx->device_info != NULL)) {
        return iolink_device_info_ctx_get(ctx->device_info);
    }
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

/** @brief Factory-reset the bound parameter context, or the legacy global set. */
static void isdu_params_factory_reset(iolink_isdu_ctx_t* ctx)
{
    /* Reset the device's own parameter context when one is bound (as device.c
       does via ctx->params_ctx); otherwise fall back to the legacy global set.
       Using the bound context is required, else a per-device app tag survives a
       Restore Factory Settings command. */
    if ((ctx != NULL) && (ctx->params_ctx != NULL)) {
        iolink_params_ctx_factory_reset(ctx->params_ctx);
    }
    else {
        iolink_params_factory_reset();
    }
}

/** @brief Effective DeviceAccessLocks value (index 0x000C). */
static uint16_t isdu_access_locks(const iolink_isdu_ctx_t* ctx)
{
    if ((ctx != NULL) && (ctx->device_info != NULL)) {
        return iolink_device_info_ctx_get_access_locks(ctx->device_info);
    }
    return iolink_device_info_get_access_locks();
}

/** @brief Commit the DeviceAccessLocks value, persisting it through the parameter manager. */
static int isdu_store_access_locks(iolink_isdu_ctx_t* ctx, uint16_t locks)
{
    if ((ctx != NULL) && (ctx->params_ctx != NULL)) {
        if (iolink_params_ctx_store_access_locks(ctx->params_ctx, locks) != 0) {
            return -1;
        }
        /* The parameter manager applies the value to its own device-info
           binding; keep the object this engine reads in sync with it. */
        if (ctx->device_info != NULL) {
            iolink_device_info_ctx_set_access_locks(ctx->device_info, locks);
        }
        return 0;
    }
    if ((ctx != NULL) && (ctx->device_info != NULL)) {
        iolink_device_info_ctx_set_access_locks(ctx->device_info, locks);
        return 0;
    }
    if (iolink_params_store_access_locks(locks) == 0) {
        iolink_device_info_set_access_locks(locks);
        return 0;
    }
    return -1;
}

/** @brief Table 101: restore the state-of-delivery DeviceAccessLocks value.
 *
 * @return 0 on success, negative when the non-volatile commit failed. The
 *         runtime value is only changed after a successful commit, so RAM and
 *         NVM cannot diverge.
 */
static int isdu_reset_access_locks(iolink_isdu_ctx_t* ctx)
{
    uint16_t defaults;
    uint16_t previous;

    if ((ctx != NULL) && (ctx->device_info != NULL)) {
        defaults = iolink_device_info_ctx_get_default_access_locks(ctx->device_info);
        previous = iolink_device_info_ctx_get_access_locks(ctx->device_info);
    }
    else {
        defaults = iolink_device_info_get_default_access_locks();
        previous = iolink_device_info_get_access_locks();
    }

    if (isdu_store_access_locks(ctx, defaults) != 0) {
        /* Leave the runtime value untouched on a failed commit. */
        if ((ctx != NULL) && (ctx->device_info != NULL)) {
            iolink_device_info_ctx_set_access_locks(ctx->device_info, previous);
        }
        else {
            iolink_device_info_set_access_locks(previous);
        }
        return -1;
    }
    return 0;
}

/** @brief Discard an active Data Storage transfer (Table 101 "Discard"). */
static void isdu_discard_ds(iolink_isdu_ctx_t* ctx)
{
    if (ctx->ds_ctx != NULL) {
        (void) iolink_ds_discard((iolink_ds_ctx_t*) ctx->ds_ctx);
    }
}

/** @brief 10.4.2/Table 101: discard any transfer, then request a DS upload. */
static void isdu_request_ds_upload(iolink_isdu_ctx_t* ctx)
{
    if (ctx->ds_ctx == NULL) {
        return;
    }
    isdu_discard_ds(ctx);
    if (iolink_ds_start_upload((iolink_ds_ctx_t*) ctx->ds_ctx) != 0) {
        return; /* Busy: the Master must not be told to upload. */
    }
    if (ctx->event_ctx != NULL) {
        iolink_event_trigger((iolink_events_ctx_t*) ctx->event_ctx,
                             IOLINK_EVENTCODE_DS_UPLOAD_REQUEST, IOLINK_EVENT_TYPE_NOTIFICATION);
    }
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

/** @brief Answer a negative ISDU response (Table C.1). */
static void isdu_negative_response(iolink_isdu_ctx_t* ctx, uint8_t error)
{
    ctx->response_buf[0] = 0x80U;
    ctx->response_buf[1] = error;
    ctx->response_len = 2U;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/**
 * @brief Object model of one Table B.8 index implemented by this stack.
 *
 * Object access is always possible through subindex 0 (Table B.8 note 1);
 * max_subindex covers the element access this stack also serves.
 */
typedef struct
{
    uint8_t max_subindex; /**< Highest valid element subindex */
    bool dynamic_array;   /**< Subindex range depends on the object content */
} isdu_object_desc_t;

/** @brief Look up an index implemented by this stack (Table B.8). */
static bool isdu_object_lookup(uint16_t index, isdu_object_desc_t* desc)
{
    switch (index) {
        case IOLINK_IDX_DIRECT_PARAMETERS_1:
        case IOLINK_IDX_DIRECT_PARAMETERS_2:
            /* Page channel: subindex 0 is the whole 16-octet page. */
            desc->max_subindex = 16U;
            desc->dynamic_array = false;
            return true;

        case IOLINK_IDX_SYSTEM_COMMAND:
        case IOLINK_IDX_DATA_STORAGE:
        case IOLINK_IDX_DEVICE_ACCESS_LOCKS:
        case IOLINK_IDX_VENDOR_NAME:
        case IOLINK_IDX_VENDOR_TEXT:
        case IOLINK_IDX_PRODUCT_NAME:
        case IOLINK_IDX_PRODUCT_ID:
        case IOLINK_IDX_PRODUCT_TEXT:
        case IOLINK_IDX_SERIAL_NUMBER:
        case IOLINK_IDX_HARDWARE_REVISION:
        case IOLINK_IDX_FIRMWARE_REVISION:
        case IOLINK_IDX_APPLICATION_TAG:
        case IOLINK_IDX_FUNCTION_TAG:
        case IOLINK_IDX_LOCATION_TAG:
        case IOLINK_IDX_PRODUCT_URI:
        case IOLINK_IDX_ERROR_COUNT:
        case IOLINK_IDX_DEVICE_STATUS:
        case IOLINK_IDX_PROCESS_DATA_INPUT:
        case IOLINK_IDX_PROCESS_DATA_OUTPUT:
            desc->max_subindex = 0U;
            desc->dynamic_array = false;
            return true;

        case IOLINK_IDX_PROFILE_CHARACTERISTIC:
            /* B.2.5: ArrayT; this stack implements a single array element. */
            desc->max_subindex = 1U;
            desc->dynamic_array = false;
            return true;

        case IOLINK_IDX_DETAILED_DEVICE_STATUS:
            /* B.2.22/Table B.14: variable-length array of 3-octet entries;
               Table B.14 explicitly does not support subindex access. */
            desc->max_subindex = 0U;
            desc->dynamic_array = false;
            return true;

        default:
            return false;
    }
}

/** @brief Request validation result (Table C.1). */
typedef enum
{
    ISDU_REQUEST_VALID = 0U,
    ISDU_REQUEST_INDEX_NOT_AVAIL,
    ISDU_REQUEST_SUBINDEX_NOT_AVAIL
} isdu_request_check_t;

/**
 * @brief Validate the requested index/subindex before any handler runs.
 *
 * C.2.3: an index that does not exist answers IDX_NOTAVAIL even when the
 * request also carries a non-zero subindex; C.2.4 only applies to an existing
 * object that has no such subindex.
 */
static isdu_request_check_t isdu_validate_request(const iolink_isdu_ctx_t* ctx)
{
    const uint16_t index = ctx->header.index;
    const uint8_t subindex = ctx->header.subindex;

    /* B.2.27/B.2.28: Preferred (0x0040-0x00FE) and Extended (0x0100-0x3FFF)
       indices are device-specific and owned by the application service when
       one is registered. */
    if (((index >= 0x0040U) && (index <= 0x00FEU)) || (index >= 0x0100U)) {
        if (ctx->vendor_service != NULL) {
            return ISDU_REQUEST_VALID;
        }
        if (index == IOLINK_IDX_ERROR_STATS) {
            /* Vendor error statistics are a single-element object. */
            return (subindex == 0U) ? ISDU_REQUEST_VALID : ISDU_REQUEST_SUBINDEX_NOT_AVAIL;
        }
        return ISDU_REQUEST_INDEX_NOT_AVAIL;
    }

    isdu_object_desc_t desc;
    if (!isdu_object_lookup(index, &desc)) {
        /* Reserved or unimplemented Table B.8 index. */
        return ISDU_REQUEST_INDEX_NOT_AVAIL;
    }
    if (!desc.dynamic_array && (subindex > desc.max_subindex)) {
        return ISDU_REQUEST_SUBINDEX_NOT_AVAIL;
    }
    return ISDU_REQUEST_VALID;
}

/** @brief Whether an SDCI write to @p index is a parameter write (Table B.2.4). */
static bool isdu_write_is_parameter(uint16_t index)
{
    /* SystemCommand is a command object, not a parameter; Data Storage
       downloads are explicitly excluded from the Parameter access lock, and
       DeviceAccessLocks must stay writable to release the lock (B.2.4). */
    if ((index == IOLINK_IDX_SYSTEM_COMMAND) || (index == IOLINK_IDX_DATA_STORAGE) ||
        (index == IOLINK_IDX_DEVICE_ACCESS_LOCKS)) {
        return false;
    }
    /* Direct Parameter page 1 is read-only; its handler rejects the write. */
    if (index == IOLINK_IDX_DIRECT_PARAMETERS_1) {
        return false;
    }
    return true;
}

/** @brief Serve the mandatory identification/status indices (vendor, product, tags, etc.). */
static void handle_mandatory_indices(iolink_isdu_ctx_t* ctx)
{
    const iolink_device_info_t* info = isdu_device_info(ctx);
    const char* str_data = NULL;

    if (info == NULL) {
        ctx->response_buf[0] = 0x80U;
        ctx->response_buf[1] = IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
        ctx->response_len = 2U;
        ctx->response_idx = 0U;
        ctx->state = ISDU_STATE_RESPONSE_READY;
        return;
    }

    /* Table C.1: the identification tags are writable here. A known read-only
       object rejects a WRITE with access denied; an unknown/reserved index
       falls through to the default IDX_NOTAVAIL response. */
    if (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) {
        const uint16_t index = ctx->header.index;
        const bool writable_tag = (index == IOLINK_IDX_APPLICATION_TAG) ||
                                  (index == IOLINK_IDX_FUNCTION_TAG) ||
                                  (index == IOLINK_IDX_LOCATION_TAG);
        if (!writable_tag) {
            switch (index) {
                case IOLINK_IDX_PROFILE_CHARACTERISTIC:
                case IOLINK_IDX_VENDOR_NAME:
                case IOLINK_IDX_VENDOR_TEXT:
                case IOLINK_IDX_PRODUCT_NAME:
                case IOLINK_IDX_PRODUCT_ID:
                case IOLINK_IDX_PRODUCT_TEXT:
                case IOLINK_IDX_PRODUCT_URI:
                case IOLINK_IDX_SERIAL_NUMBER:
                case IOLINK_IDX_HARDWARE_REVISION:
                case IOLINK_IDX_FIRMWARE_REVISION:
                case IOLINK_IDX_ERROR_COUNT:
                case IOLINK_IDX_DEVICE_STATUS:
                    ctx->response_buf[0] = 0x80U;
                    ctx->response_buf[1] = IOLINK_ISDU_ERROR_WRITE_PROTECTED;
                    ctx->response_len = 2U;
                    ctx->response_idx = 0U;
                    ctx->state = ISDU_STATE_RESPONSE_READY;
                    return;
                default:
                    break; /* unknown index: IDX_NOTAVAIL below */
            }
        }
    }

    /* The index/subindex model was validated before dispatch (Table B.8). */
    switch (ctx->header.index) {
            /* Table B.8: 0x0004-0x000B are reserved. VendorID/DeviceID live in
               Direct Parameter Page 1 (0x07-0x0B), not in an ISDU index. */

        case IOLINK_IDX_PROFILE_CHARACTERISTIC:
            /* B.2.5: a read-only ArrayT of UIntegerT16 holding the list of
             * ProfileIdentifiers, conditional on the associated Profile. This
             * stack exposes a single element (subindex 0 or 1). */
            if (info->profile_characteristic == 0U) {
                ctx->response_buf[0] = 0x80U;
                ctx->response_buf[1] = IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
                ctx->response_len = 2U;
                ctx->response_idx = 0U;
                ctx->state = ISDU_STATE_RESPONSE_READY;
                return;
            }
            /* A WRITE is already rejected by the read-only guard above (0x80
               0x23); only the subindex range is specific to the ArrayT. */
            if (ctx->header.subindex > 1U) {
                ctx->response_buf[0] = 0x80U;
                ctx->response_buf[1] = IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL;
                ctx->response_len = 2U;
                ctx->response_idx = 0U;
                ctx->state = ISDU_STATE_RESPONSE_READY;
                return;
            }
            ctx->response_buf[0] = (uint8_t) (info->profile_characteristic >> 8);
            ctx->response_buf[1] = (uint8_t) (info->profile_characteristic & 0xFF);
            ctx->response_len = 2U;
            ctx->response_idx = 0U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;

        case IOLINK_IDX_ERROR_COUNT: {
            /* Table B.8/B.2.20: 2-octet error count since reset (read-only,
               optional). Sum the DLL error counters; 0 without a bound DLL. */
            uint32_t total = 0U;
            if (ctx->dll_ctx != NULL) {
                iolink_dll_stats_t stats;
                iolink_dll_get_stats((const iolink_dll_ctx_t*) ctx->dll_ctx, &stats);
                uint64_t sum = (uint64_t) stats.crc_errors + (uint64_t) stats.framing_errors +
                               (uint64_t) stats.timeout_errors + (uint64_t) stats.timing_errors;
                total = (sum > 0xFFFFU) ? 0xFFFFU : (uint32_t) sum;
            }
            ctx->response_buf[0] = (uint8_t) (total >> 8);
            ctx->response_buf[1] = (uint8_t) (total & 0xFFU);
            ctx->response_len = 2U;
            ctx->response_idx = 0U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;
        }

        case IOLINK_IDX_VENDOR_NAME:
            str_data = info->vendor_name;
            break;
        case IOLINK_IDX_VENDOR_TEXT:
            str_data = info->vendor_text;
            break;
        case IOLINK_IDX_PRODUCT_NAME:
            str_data = info->product_name;
            break;
        case IOLINK_IDX_PRODUCT_ID:
            str_data = info->product_id;
            break;
        case IOLINK_IDX_PRODUCT_TEXT:
            str_data = info->product_text;
            break;
        case IOLINK_IDX_SERIAL_NUMBER:
            str_data = info->serial_number;
            break;
        case IOLINK_IDX_HARDWARE_REVISION:
            str_data = info->hardware_revision;
            break;
        case IOLINK_IDX_FIRMWARE_REVISION:
            str_data = info->firmware_revision;
            break;

        case IOLINK_IDX_PRODUCT_URI:
            str_data = info->product_uri;
            break;

        case IOLINK_IDX_APPLICATION_TAG:
            if (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) {
                if (isdu_params_set(ctx, IOLINK_IDX_APPLICATION_TAG, 0U, ctx->buffer,
                                    ctx->buffer_idx, true) == 0) {
                    ctx->response_len = 0U;
                    ctx->response_idx = 0U;
                    ctx->state = ISDU_STATE_RESPONSE_READY;
                    return;
                }
                /* Persistent storage rejected the service: terminate with a
                 * negative response instead of leaving the master polling Busy. */
                ctx->response_buf[0] = 0x80U;
                ctx->response_buf[1] = IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
                ctx->response_len = 2U;
                ctx->response_idx = 0U;
                ctx->state = ISDU_STATE_RESPONSE_READY;
                return;
            }
            else {
                int res = isdu_params_get(ctx, IOLINK_IDX_APPLICATION_TAG, 0U, ctx->response_buf,
                                          (size_t) IOLINK_ISDU_BUFFER_SIZE);
                if (res >= 0) {
                    ctx->response_len = (uint8_t) res;
                    ctx->response_idx = 0U;
                    ctx->state = ISDU_STATE_RESPONSE_READY;
                    return;
                }
            }
            break;

        case IOLINK_IDX_FUNCTION_TAG:
            if (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) {
                if (isdu_params_set(ctx, IOLINK_IDX_FUNCTION_TAG, 0U, ctx->buffer, ctx->buffer_idx,
                                    true) == 0) {
                    ctx->response_len = 0U;
                    ctx->response_idx = 0U;
                    ctx->state = ISDU_STATE_RESPONSE_READY;
                    return;
                }
                /* Persistent storage rejected the service: terminate with a
                 * negative response instead of leaving the master polling Busy. */
                ctx->response_buf[0] = 0x80U;
                ctx->response_buf[1] = IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
                ctx->response_len = 2U;
                ctx->response_idx = 0U;
                ctx->state = ISDU_STATE_RESPONSE_READY;
                return;
            }
            else {
                int res = isdu_params_get(ctx, IOLINK_IDX_FUNCTION_TAG, 0U, ctx->response_buf,
                                          (size_t) IOLINK_ISDU_BUFFER_SIZE);
                if (res >= 0) {
                    ctx->response_len = (uint8_t) res;
                    ctx->response_idx = 0U;
                    ctx->state = ISDU_STATE_RESPONSE_READY;
                    return;
                }
            }
            break;

        case IOLINK_IDX_LOCATION_TAG:
            if (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) {
                if (isdu_params_set(ctx, IOLINK_IDX_LOCATION_TAG, 0U, ctx->buffer, ctx->buffer_idx,
                                    true) == 0) {
                    ctx->response_len = 0U;
                    ctx->response_idx = 0U;
                    ctx->state = ISDU_STATE_RESPONSE_READY;
                    return;
                }
                /* Persistent storage rejected the service: terminate with a
                 * negative response instead of leaving the master polling Busy. */
                ctx->response_buf[0] = 0x80U;
                ctx->response_buf[1] = IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
                ctx->response_len = 2U;
                ctx->response_idx = 0U;
                ctx->state = ISDU_STATE_RESPONSE_READY;
                return;
            }
            else {
                int res = isdu_params_get(ctx, IOLINK_IDX_LOCATION_TAG, 0U, ctx->response_buf,
                                          (size_t) IOLINK_ISDU_BUFFER_SIZE);
                if (res >= 0) {
                    ctx->response_len = (uint8_t) res;
                    ctx->response_idx = 0U;
                    ctx->state = ISDU_STATE_RESPONSE_READY;
                    return;
                }
            }
            break;

        case IOLINK_IDX_PROCESS_DATA_INPUT: {
            /* Table B.8 / B.2.23: ProcessDataInput provides the last *valid*
               process input data handed to the Device application. The DLL
               exposes the current PDin validity; while it is flagged invalid
               the buffer is not "last valid" data, so the object answers
               IDX_NOTAVAIL instead of returning stale octets. */
            uint8_t len = 0U;
            if (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) {
                ctx->response_buf[0] = 0x80U;
                ctx->response_buf[1] = IOLINK_ISDU_ERROR_WRITE_PROTECTED;
                ctx->response_len = 2U;
                ctx->response_idx = 0U;
                ctx->state = ISDU_STATE_RESPONSE_READY;
                return;
            }
            if (ctx->header.subindex != 0U) {
                ctx->response_buf[0] = 0x80U;
                ctx->response_buf[1] = IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL;
                ctx->response_len = 2U;
                ctx->response_idx = 0U;
                ctx->state = ISDU_STATE_RESPONSE_READY;
                return;
            }
            if (ctx->dll_ctx != NULL) {
                const iolink_dll_ctx_t* dll = (const iolink_dll_ctx_t*) ctx->dll_ctx;
                len = dll->pd_in_len_current;
                if (len > sizeof(dll->pd_in)) {
                    len = (uint8_t) sizeof(dll->pd_in);
                }
                if (len > 0U) {
                    if (!dll->pd_valid) {
                        /* B.2.23: the current PDin is flagged invalid, so the
                           buffer is not "last valid" data. */
                        ctx->response_buf[0] = 0x80U;
                        ctx->response_buf[1] = IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
                        ctx->response_len = 2U;
                        ctx->response_idx = 0U;
                        ctx->state = ISDU_STATE_RESPONSE_READY;
                        return;
                    }
                    (void) memcpy(ctx->response_buf, dll->pd_in, len);
                }
            }
            ctx->response_len = len;
            ctx->response_idx = 0U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;
        }

        case IOLINK_IDX_PROCESS_DATA_OUTPUT: {
            /* Table B.8 / B.2.24: ProcessDataOutput provides the last process
               output data written to the Device application. Until a PDout
               frame has been received there is no such value: a configured
               PDout answers IDX_NOTAVAIL, a zero-length PDout answers empty. */
            uint8_t len = 0U;
            if (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) {
                ctx->response_buf[0] = 0x80U;
                ctx->response_buf[1] = IOLINK_ISDU_ERROR_WRITE_PROTECTED;
                ctx->response_len = 2U;
                ctx->response_idx = 0U;
                ctx->state = ISDU_STATE_RESPONSE_READY;
                return;
            }
            if (ctx->header.subindex != 0U) {
                ctx->response_buf[0] = 0x80U;
                ctx->response_buf[1] = IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL;
                ctx->response_len = 2U;
                ctx->response_idx = 0U;
                ctx->state = ISDU_STATE_RESPONSE_READY;
                return;
            }
            if (ctx->dll_ctx != NULL) {
                const iolink_dll_ctx_t* dll = (const iolink_dll_ctx_t*) ctx->dll_ctx;
                len = dll->pd_out_len_current;
                if (len > sizeof(dll->pd_out)) {
                    len = (uint8_t) sizeof(dll->pd_out);
                }
                if (dll->pd_out_seen) {
                    if (len > 0U) {
                        (void) memcpy(ctx->response_buf, dll->pd_out, len);
                    }
                }
                else if (len > 0U) {
                    ctx->response_buf[0] = 0x80U;
                    ctx->response_buf[1] = IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
                    ctx->response_len = 2U;
                    ctx->response_idx = 0U;
                    ctx->state = ISDU_STATE_RESPONSE_READY;
                    return;
                }
            }
            ctx->response_len = len;
            ctx->response_idx = 0U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;
        }

        case IOLINK_IDX_DEVICE_STATUS:
            ctx->response_buf[0] =
                iolink_events_get_highest_severity((iolink_events_ctx_t*) ctx->event_ctx);
            ctx->response_len = 1U;
            ctx->response_idx = 0U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;

            /* IOLINK_IDX_DETAILED_DEVICE_STATUS (0x0025) is handled earlier in
               handle_standard_commands() via handle_detailed_device_status(). */

        default:
            ctx->response_buf[0] = 0x80U; /* Error: Service not available */
            ctx->response_buf[1] = IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
            ctx->response_len = 2U;
            ctx->response_idx = 0U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;
    }

    if (str_data == NULL) {
        /* Optional object not implemented by this Device (Table C.1). */
        ctx->response_buf[0] = 0x80U;
        ctx->response_buf[1] = IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
        ctx->response_len = 2U;
        ctx->response_idx = 0U;
        ctx->state = ISDU_STATE_RESPONSE_READY;
        return;
    }

    /* Table B.8/B.2.8-B.2.19: per-object StringT fixedLength caps the
       response. ProductURI allows up to 100 octets; SerialNumber 16. */
    size_t limit = 64U;
    if (ctx->header.index == IOLINK_IDX_SERIAL_NUMBER) {
        limit = 16U;
    }
    else if (ctx->header.index == IOLINK_IDX_PRODUCT_URI) {
        limit = 100U;
    }
    size_t len_sz = strlen(str_data);
    if (len_sz > limit) {
        len_sz = limit;
    }
    if (len_sz > sizeof(ctx->response_buf)) {
        len_sz = sizeof(ctx->response_buf);
    }
    (void) memcpy(ctx->response_buf, str_data, len_sz);
    ctx->response_len = (uint8_t) len_sz;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/** @brief Execute a SystemCommand (reset, factory restore, data-storage commands). */
/** @brief Table 101: reset the Device diagnosis/status for the "0" reset. */
static void isdu_reset_diagnostics(iolink_isdu_ctx_t* ctx)
{
    if (ctx->dll_ctx != NULL) {
        iolink_dll_reset_diagnostics((iolink_dll_ctx_t*) ctx->dll_ctx);
    }
}

/** @brief Table 101: reset ErrorCount for the reset commands that keep Events. */
static void isdu_reset_error_counters(iolink_isdu_ctx_t* ctx)
{
    if (ctx->dll_ctx != NULL) {
        iolink_dll_reset_error_counters((iolink_dll_ctx_t*) ctx->dll_ctx);
    }
}

static void handle_system_command(iolink_isdu_ctx_t* ctx, uint8_t cmd)
{
    switch (cmd) {
        case IOLINK_CMD_DEVICE_RESET: /* 0x80 */
            /* Table 101: Device reset sets Diagnosis and status to "0" and
               discards a block parameter transfer. */
            isdu_discard_ds(ctx);
            isdu_reset_diagnostics(ctx);
            /* Set reset flag for application to handle */
            ctx->reset_pending = true;
            break;

        case IOLINK_CMD_APPLICATION_RESET: /* 0x81 */
            /* Table 101: Access locks return to the state of delivery, the
               block transfer is discarded and the Data Storage behaviour
               becomes "upload required" (DS_UPLOAD_REQ). */
            if (isdu_reset_access_locks(ctx) != 0) {
                isdu_negative_response(ctx, IOLINK_ISDU_ERROR_APP_DEVICE);
                return;
            }
            isdu_request_ds_upload(ctx);
            ctx->app_reset_pending = true;
            break;

        case IOLINK_CMD_RESTORE_FACTORY_SETTINGS: /* 0x82 */
            /* Table 101 "Clear": active Events are withdrawn with MODE
               "disappears" while ErrorCount is reset. Access locks fall back to
               the state of delivery and the block transfer is discarded. */
            if (isdu_reset_access_locks(ctx) != 0) {
                isdu_negative_response(ctx, IOLINK_ISDU_ERROR_APP_DEVICE);
                return;
            }
            isdu_params_factory_reset(ctx);
            /* Discard after the parameter reset so the rebuilt Data Storage
               image reflects the new (default) parameters. */
            isdu_discard_ds(ctx);
            isdu_reset_diagnostics(ctx);
            break;

        case IOLINK_CMD_BACK_TO_BOX: /* 0x83 */
            /* Back-to-box sets diagnosis/status to "0" and stops communication
               after the positive response and three Master cycles. */
            if (isdu_reset_access_locks(ctx) != 0) {
                isdu_negative_response(ctx, IOLINK_ISDU_ERROR_APP_DEVICE);
                return;
            }
            isdu_params_factory_reset(ctx);
            /* Discard after the parameter reset so the rebuilt Data Storage
               image reflects the new (default) parameters. */
            isdu_discard_ds(ctx);
            isdu_reset_error_counters(ctx);
            if (ctx->event_ctx != NULL) {
                iolink_events_init((iolink_events_ctx_t*) ctx->event_ctx);
            }
            ctx->back_to_box_pending = true;
            break;

        /* Table B.9: ParamUploadStart/End (0x01/0x02), ParamDownloadStart/End
           (0x03/0x04) and ParamDownloadStore (0x05). */
        case IOLINK_CMD_PARAM_UPLOAD_START:
        case IOLINK_CMD_PARAM_UPLOAD_END:
        case IOLINK_CMD_PARAM_DOWNLOAD_START:
        case IOLINK_CMD_PARAM_DOWNLOAD_END:
        case IOLINK_CMD_PARAM_DOWNLOAD_STORE:
            if (ctx->ds_ctx != NULL) {
                uint16_t locks = isdu_access_locks(ctx);
                int ret = iolink_ds_handle_command((iolink_ds_ctx_t*) ctx->ds_ctx, cmd, locks);

                if (ret != 0) {
                    ctx->response_buf[0] = 0x80U;
                    if (ret == -1) {
                        ctx->response_buf[1] = IOLINK_ISDU_ERROR_FUNCTION_TEMP_UNAVAIL;
                    }
                    else if (ret == -2) {
                        ctx->response_buf[1] =
                            IOLINK_ISDU_ERROR_WRITE_PROTECTED; /* 0x23, Table C.1 */
                    }
                    else if (ret == -4) {
                        ctx->response_buf[1] = IOLINK_ISDU_ERROR_PARAM_INCONSISTENT;
                    }
                    else if (ret == -3) {
                        ctx->response_buf[1] = IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL;
                    }
                    else {
                        ctx->response_buf[1] = IOLINK_ISDU_ERROR_IDX_NOT_AVAIL;
                    }
                    ctx->response_len = 2U;
                    ctx->response_idx = 0U;
                    ctx->state = ISDU_STATE_RESPONSE_READY;
                    return;
                }

                /* 10.3.5/10.4.2: ParamDownloadStore starts the Data Storage
                   upload; the Master is notified through the DS_UPLOAD_REQ
                   Event (0xFF91, Table D.1). */
                if ((cmd == IOLINK_CMD_PARAM_DOWNLOAD_STORE) && (ctx->event_ctx != NULL)) {
                    iolink_event_trigger((iolink_events_ctx_t*) ctx->event_ctx,
                                         IOLINK_EVENTCODE_DS_UPLOAD_REQUEST,
                                         IOLINK_EVENT_TYPE_NOTIFICATION);
                }
            }
            break;

        case IOLINK_CMD_PARAM_BREAK: /* 0x06 */
            /* Abort current Data Storage operation */
            if (ctx->ds_ctx != NULL) {
                (void) iolink_ds_abort((iolink_ds_ctx_t*) ctx->ds_ctx);
            }
            break;

        default:
            /* Table B.9: 0x84-0x9F are reserved; an unsupported command value is
               FUNC_NOTAVAIL (Table C.1 / C.2.14). */
            ctx->response_buf[0] = 0x80U;
            ctx->response_buf[1] = IOLINK_ISDU_ERROR_FUNCTION_NOT_AVAIL;
            ctx->response_len = 2U;
            ctx->response_idx = 0U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;
    }

    /* Success: empty response */
    ctx->response_len = 0U;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/** @brief Read or write the DeviceAccessLocks parameter (index 0x000C). */
static void handle_access_locks(iolink_isdu_ctx_t* ctx)
{
    if (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_READ) {
        uint16_t locks = isdu_access_locks(ctx);
        ctx->response_buf[0] = (uint8_t) (locks >> 8);
        ctx->response_buf[1] = (uint8_t) (locks & 0xFF);
        ctx->response_len = 2U;
    }
    else {
        /* B.2.4/Table B.12: the object is exactly two octets; bits 4-15 are
           reserved. A wrong length is a length error (C.2.12/C.2.13) and the
           reserved bits are not stored. */
        if (ctx->buffer_idx < 2U) {
            ctx->response_buf[0] = 0x80U;
            ctx->response_buf[1] = IOLINK_ISDU_ERROR_PARAM_LEN_UNDERRUN;
            ctx->response_len = 2U;
        }
        else if (ctx->buffer_idx > 2U) {
            ctx->response_buf[0] = 0x80U;
            ctx->response_buf[1] = IOLINK_ISDU_ERROR_PARAM_LEN_OVERRUN;
            ctx->response_len = 2U;
        }
        else {
            const uint16_t new_locks =
                (uint16_t) ((((uint16_t) ctx->buffer[0] << 8) | ctx->buffer[1]) & 0x000FU);
            if (isdu_store_access_locks(ctx, new_locks) != 0) {
                ctx->response_buf[0] = 0x80U;
                ctx->response_buf[1] = IOLINK_ISDU_ERROR_APP_DEVICE;
                ctx->response_len = 2U;
            }
            else {
                ctx->response_len = 0U;
            }
        }
    }
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/** @brief Serve DetailedDeviceStatus: encode up to 8 queued events as qualifier+code triplets. */
static void handle_detailed_device_status(iolink_isdu_ctx_t* ctx)
{
    if (ctx->header.type != IOLINK_ISDU_SERVICE_TYPE_READ) {
        /* Table B.14/B.2.22: read-only object; answer a WRITE with a full
           negative confirmation. */
        isdu_negative_response(ctx, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
        return;
    }

    if (ctx->event_ctx == NULL) {
        /* No event host: answer an empty array instead of dropping the
           response (which would time out at the Master). */
        ctx->response_len = 0U;
        ctx->response_idx = 0U;
        ctx->state = ISDU_STATE_RESPONSE_READY;
        return;
    }

    iolink_critical_enter();
    iolink_events_ctx_t* event_ctx = (iolink_events_ctx_t*) ctx->event_ctx;

    /* Collect the Error/Warning events whose "Event appears" has been reported.
       B.2.22: only MODE "Event appears" entries are listed; a disappeared event
       drops out, and an appearance that is still deferred for the 1 s spacing
       has not reached the Master yet, so it is not listed either. */
    uint16_t codes[IOLINK_EVENT_PENDING_MAX];
    iolink_event_type_t types[IOLINK_EVENT_PENDING_MAX];
    uint8_t n = 0U;
    for (uint8_t i = 0U; i < IOLINK_EVENT_PENDING_MAX; i++) {
        const iolink_event_pending_t* p = &event_ctx->pending[i];
        if (p->active && p->present && p->reported && (p->type != IOLINK_EVENT_TYPE_NOTIFICATION)) {
            codes[n] = p->code;
            types[n] = p->type;
            n++;
        }
    }

    /* B.2.22: the conformant read is subindex 0, which returns the complete
       list of 3-octet entries ("Subindex access is not supported"). Element
       access through subindex 1..n is kept as a device extension. */
    const uint8_t sub = ctx->header.subindex;
    if ((sub == 0U) || (sub <= n)) {
        const uint8_t start = (sub == 0U) ? 0U : (uint8_t) (sub - 1U);
        const uint8_t end = (sub == 0U) ? n : (uint8_t) (start + 1U);
        uint8_t pos = 0U;
        for (uint8_t i = start; i < end; i++) {
            /* Figure A.24: MODE 11 = appears, TYPE 10 = Warning / 11 = Error,
               INSTANCE 4 = Application. */
            uint8_t qualifier = 0xC0U;
            qualifier |= (types[i] == IOLINK_EVENT_TYPE_ERROR) ? (0x03U << 4) : (0x02U << 4);
            qualifier |= 0x04U;
            ctx->response_buf[pos++] = qualifier;
            ctx->response_buf[pos++] = (uint8_t) (codes[i] >> 8);
            ctx->response_buf[pos++] = (uint8_t) (codes[i] & 0xFFU);
        }
        ctx->response_len = pos;
    }
    else {
        ctx->response_buf[0] = 0x80U;
        ctx->response_buf[1] = IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL;
        ctx->response_len = 2U;
    }
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
    iolink_critical_exit();
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
        isdu_negative_response(ctx, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
        return;
    }

    if (ctx->header.subindex != 0U) {
        isdu_negative_response(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
        return;
    }

    if (ctx->dll_ctx == NULL) {
        isdu_negative_response(ctx, IOLINK_ISDU_ERROR_SERVICE_UNAVAIL);
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

/** @brief Serve the Data Storage index: backup (read image) or restore (apply image). */
static void handle_data_storage(iolink_isdu_ctx_t* ctx)
{
    iolink_ds_ctx_t* ds = (iolink_ds_ctx_t*) ctx->ds_ctx;
    if (ds == NULL) {
        ctx->response_buf[0] = 0x80U;
        ctx->response_buf[1] = IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
        ctx->response_len = 2U;
        ctx->response_idx = 0U;
        ctx->state = ISDU_STATE_RESPONSE_READY;
        return;
    }

    if (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) {
        /* Restore: apply the parameter image provided by the Master. */
        if (iolink_ds_apply_image(ds, ctx->buffer, ctx->buffer_idx) != 0) {
            ctx->response_buf[0] = 0x80U;
            ctx->response_buf[1] = IOLINK_ISDU_ERROR_PARAM_INCONSISTENT;
            ctx->response_len = 2U;
        }
        else {
            ctx->response_len = 0U; /* Write acknowledged */
        }
    }
    else {
        /* Backup: return the serialized parameter image. */
        size_t len = 0U;
        const uint8_t* img = iolink_ds_get_image(ds, &len);
        if ((img == NULL) || (len > IOLINK_ISDU_BUFFER_SIZE)) {
            ctx->response_len = 0U;
        }
        else {
            if (len > 0U) {
                (void) memcpy(ctx->response_buf, img, len);
            }
            ctx->response_len = len;
        }
    }
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/** @brief Dispatch an executed ISDU request to the handler for its index. */
static void handle_standard_commands(iolink_isdu_ctx_t* ctx)
{
    ctx->vendor_positive = false;
    const uint16_t index = ctx->header.index;

    /* Table B.8/C.2.3/C.2.4: validate index and subindex before dispatch so a
       reserved index is IDX_NOTAVAIL even with a non-zero subindex, and every
       handler starts from a valid object address. */
    switch (isdu_validate_request(ctx)) {
        case ISDU_REQUEST_INDEX_NOT_AVAIL:
            isdu_negative_response(ctx, IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL);
            return;
        case ISDU_REQUEST_SUBINDEX_NOT_AVAIL:
            isdu_negative_response(ctx, IOLINK_ISDU_ERROR_SUBINDEX_NOT_AVAIL);
            return;
        default:
            break;
    }

    /* Table B.2.4/10.6.5: with the Parameter (write) access lock set, every
       read/write parameter accessible over SDCI rejects a write with access
       denied. DeviceAccessLocks, SystemCommand and Data Storage downloads are
       exempt. */
    if ((ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) && isdu_write_is_parameter(index) &&
        ((isdu_access_locks(ctx) & IOLINK_LOCK_PARAM) != 0U)) {
        isdu_negative_response(ctx, IOLINK_ISDU_ERROR_WRITE_PROTECTED);
        return;
    }

    /* B.2.27: Preferred Indices 0x0040-0x00FE are device-specific like the
       Extended Index range from 0x0100; both reach the application vendor
       service. 0x00FF itself is reserved. */
    const bool device_specific = ((index >= 0x0040U) && (index <= 0x00FEU)) || (index >= 0x0100U);
    if (device_specific && (ctx->vendor_service != NULL)) {
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
            /* Mandatory System Commands */
            if (ctx->buffer_idx > 0U) {
                handle_system_command(ctx, ctx->buffer[0]);
            }
            else {
                ctx->response_buf[0] = 0x80U;
                ctx->response_buf[1] = IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
                ctx->response_len = 2U;
                ctx->response_idx = 0U;
                ctx->state = ISDU_STATE_RESPONSE_READY;
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
