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

/* handle_standard_commands() is defined below; the transport executes it once a
   full ISDU request has been received. */
static void handle_standard_commands(iolink_isdu_ctx_t* ctx);

/** @brief Reset the ISDU request transport to Idle. */
static void isdu_transport_reset(iolink_isdu_ctx_t* ctx)
{
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
    const uint8_t* buf = ctx->req_buf;
    const uint8_t service = (uint8_t) (buf[0] >> 4);
    size_t pos = 1U;

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
        if ((pos + 1U) >= total) {
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
        if (pos >= total) {
            ctx->response_buf[0] = 0x80U;
            ctx->response_buf[1] = IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
            ctx->response_len = 2U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;
        }
        ctx->header.index = buf[pos++];
    }

    if (has_sub) {
        if (pos >= total) {
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
            if (n > sizeof(ctx->buffer)) {
                n = sizeof(ctx->buffer);
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
    const bool negative = ((ctx->response_len == 2U) && (ctx->response_buf[0] == 0x80U));
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
    for (uint8_t i = 0U; i < od_len; i++) {
        od_out[i] = 0U;
    }

    if (flowctrl == IOLINK_FLOWCTRL_ABORT) {
        isdu_transport_reset(ctx);
        return;
    }
    if ((flowctrl == IOLINK_FLOWCTRL_IDLE) || (flowctrl == IOLINK_FLOWCTRL_IDLE2)) {
        isdu_transport_reset(ctx);
        return;
    }

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
        /* COUNT on a read continues the response sequentially. */
        if (!ctx->flowctrl_seen) {
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

    if ((ctx->state == ISDU_STATE_RESPONSE_READY) && (ctx->resp_idx >= ctx->resp_total)) {
        isdu_transport_reset(ctx);
    }
}

/** @brief Serve the mandatory identification/status indices (vendor, product, tags, etc.). */
static void handle_mandatory_indices(iolink_isdu_ctx_t* ctx)
{
    const iolink_device_info_t* info = iolink_device_info_get();
    const char* str_data = NULL;

    if (info == NULL) {
        ctx->response_buf[0] = 0x80U;
        ctx->response_buf[1] = 0x11U;
        ctx->response_len = 2U;
        ctx->response_idx = 0U;
        ctx->state = ISDU_STATE_RESPONSE_READY;
        return;
    }

    switch (ctx->header.index) {
        case IOLINK_IDX_VENDOR_ID:
            ctx->response_buf[0] = (uint8_t) (info->vendor_id >> 8);
            ctx->response_buf[1] = (uint8_t) (info->vendor_id & 0xFF);
            ctx->response_len = 2U;
            ctx->response_idx = 0U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;

        case IOLINK_IDX_DEVICE_ID:
            /* Table B.8/7.3.5: DeviceID is 3 octets (MSO first). */
            ctx->response_buf[0] = (uint8_t) ((info->device_id >> 16) & 0xFFU);
            ctx->response_buf[1] = (uint8_t) ((info->device_id >> 8) & 0xFFU);
            ctx->response_buf[2] = (uint8_t) (info->device_id & 0xFFU);
            ctx->response_len = 3U;
            ctx->response_idx = 0U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;

        case IOLINK_IDX_PROFILE_CHARACTERISTIC:
            ctx->response_buf[0] = (uint8_t) (info->profile_characteristic >> 8);
            ctx->response_buf[1] = (uint8_t) (info->profile_characteristic & 0xFF);
            ctx->response_len = 2U;
            ctx->response_idx = 0U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;

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

        case IOLINK_IDX_APPLICATION_TAG:
            if (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) {
                if (isdu_params_set(ctx, IOLINK_IDX_APPLICATION_TAG, 0U, ctx->buffer,
                                    ctx->buffer_idx, true) == 0) {
                    ctx->response_len = 0U;
                    ctx->response_idx = 0U;
                    ctx->state = ISDU_STATE_RESPONSE_READY;
                    return;
                }
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

        case IOLINK_IDX_PROCESS_DATA_INPUT:
            /* Read-only: Returns PD Input descriptor (1 byte: PD length) */
            if (ctx->header.type == IOLINK_ISDU_SERVICE_TYPE_WRITE) {
                ctx->response_buf[0] = 0x80U;
                ctx->response_buf[1] = IOLINK_ISDU_ERROR_WRITE_PROTECTED;
                ctx->response_len = 2U;
                ctx->response_idx = 0U;
                ctx->state = ISDU_STATE_RESPONSE_READY;
                return;
            }
            /* Return the actual configured PD Input length (1 byte). */
            {
                uint8_t pd_in = 0U;
                uint8_t pd_out = 0U;
                if (ctx->dll_ctx != NULL) {
                    iolink_dll_get_pd_length((const iolink_dll_ctx_t*) ctx->dll_ctx, &pd_in,
                                             &pd_out);
                }
                ctx->response_buf[0] = pd_in;
            }
            ctx->response_len = 1U;
            ctx->response_idx = 0U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;

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

    if (str_data) {
        size_t len_sz = strlen(str_data);
        if (len_sz > sizeof(ctx->response_buf)) {
            len_sz = sizeof(ctx->response_buf);
        }
        (void) memcpy(ctx->response_buf, str_data, len_sz);
        ctx->response_len = (uint8_t) len_sz;
        ctx->response_idx = 0U;
        ctx->state = ISDU_STATE_RESPONSE_READY;
    }
}

/** @brief Execute a SystemCommand (reset, factory restore, data-storage commands). */
static void handle_system_command(iolink_isdu_ctx_t* ctx, uint8_t cmd)
{
    switch (cmd) {
        case IOLINK_CMD_DEVICE_RESET: /* 0x80 */
            /* Set reset flag for application to handle */
            ctx->reset_pending = true;
            break;

        case IOLINK_CMD_APPLICATION_RESET: /* 0x81 */
            /* Set application reset flag */
            ctx->app_reset_pending = true;
            break;

        case IOLINK_CMD_RESTORE_FACTORY_SETTINGS: /* 0x82 */
            /* Reset all parameters to factory defaults */
            isdu_params_factory_reset(ctx);
            break;

        case IOLINK_CMD_RESTORE_APP_DEFAULTS: /* 0x83 */
            /* Reset application-specific parameters (currently same as factory) */
            isdu_params_factory_reset(ctx);
            break;

        case IOLINK_CMD_SET_COMM_MODE: /* 0x84 */
            /* Communication mode switching handled by DLL - this is a no-op */
            break;

        /* Standard Data Storage Commands (0x05-0x08) */
        case IOLINK_CMD_PARAM_DOWNLOAD_START:
        case IOLINK_CMD_PARAM_DOWNLOAD_END:
        case IOLINK_CMD_PARAM_UPLOAD_START:
        case IOLINK_CMD_PARAM_UPLOAD_END:
            if (ctx->ds_ctx != NULL) {
                uint16_t locks = iolink_device_info_get_access_locks();
                int ret = iolink_ds_handle_command((iolink_ds_ctx_t*) ctx->ds_ctx, cmd, locks);

                if (ret != 0) {
                    ctx->response_buf[0] = 0x80U;
                    if (ret == -1) {
                        ctx->response_buf[1] = IOLINK_ISDU_ERROR_BUSY; /* 0x30 */
                    }
                    else if (ret == -2) {
                        ctx->response_buf[1] =
                            IOLINK_ISDU_ERROR_WRITE_PROTECTED; /* 0x23, Table C.1 */
                    }
                    else {
                        ctx->response_buf[1] = IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
                    }
                    ctx->response_len = 2U;
                    ctx->response_idx = 0U;
                    ctx->state = ISDU_STATE_RESPONSE_READY;
                    return;
                }
            }
            break;

        /* Legacy/Custom DS Commands (0x95-0x97) - Mapped to standard flows if possible */
        case IOLINK_CMD_PARAM_UPLOAD: /* 0x95 -> 0x07 Start Upload */
            if (ctx->ds_ctx != NULL) {
                /* Legacy mapped to Upload Start */
                if (iolink_ds_start_upload((iolink_ds_ctx_t*) ctx->ds_ctx) != 0) {
                    ctx->response_buf[0] = 0x80U;
                    ctx->response_buf[1] = IOLINK_ISDU_ERROR_BUSY;
                    ctx->response_len = 2U;
                    ctx->response_idx = 0U;
                    ctx->state = ISDU_STATE_RESPONSE_READY;
                    return;
                }
            }
            break;

        case IOLINK_CMD_PARAM_DOWNLOAD: /* 0x96 -> 0x05 Start Download */
            if (ctx->ds_ctx != NULL) {
                if (iolink_ds_start_download((iolink_ds_ctx_t*) ctx->ds_ctx) != 0) {
                    ctx->response_buf[0] = 0x80U;
                    ctx->response_buf[1] = IOLINK_ISDU_ERROR_BUSY;
                    ctx->response_len = 2U;
                    ctx->response_idx = 0U;
                    ctx->state = ISDU_STATE_RESPONSE_READY;
                    return;
                }
            }
            break;

        case IOLINK_CMD_PARAM_BREAK: /* 0x97 */
            /* Abort current Data Storage operation */
            if (ctx->ds_ctx != NULL) {
                (void) iolink_ds_abort((iolink_ds_ctx_t*) ctx->ds_ctx);
            }
            break;

        default:
            /* Unknown command */
            ctx->response_buf[0] = 0x80U;
            ctx->response_buf[1] = IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
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
        uint16_t locks = iolink_device_info_get_access_locks();
        ctx->response_buf[0] = (uint8_t) (locks >> 8);
        ctx->response_buf[1] = (uint8_t) (locks & 0xFF);
        ctx->response_len = 2U;
    }
    else {
        /* Write: Update access locks */
        if (ctx->buffer_idx >= 2U) {
            uint16_t new_locks = ((uint16_t) ctx->buffer[0] << 8) | ctx->buffer[1];
            iolink_device_info_set_access_locks(new_locks);
        }
        ctx->response_len = 0U;
    }
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
}

/** @brief Serve DetailedDeviceStatus: encode up to 8 queued events as qualifier+code triplets. */
static void handle_detailed_device_status(iolink_isdu_ctx_t* ctx)
{
    if (ctx->header.type != IOLINK_ISDU_SERVICE_TYPE_READ) {
        ctx->response_buf[0] = 0x80U;
        ctx->response_buf[1] = IOLINK_ISDU_ERROR_WRITE_PROTECTED;
        ctx->response_len = 2U;
        return;
    }

    if (ctx->event_ctx == NULL) {
        ctx->response_len = 0U;
        return;
    }

    iolink_critical_enter();
    iolink_events_ctx_t* event_ctx = (iolink_events_ctx_t*) ctx->event_ctx;
    uint8_t count = event_ctx->count;
    if (count > 8U) count = 8U; /* Limit to 8 events in response */

    for (uint8_t i = 0U; i < count; i++) {
        /* Calculate index in FIFO */
        uint8_t idx = (uint8_t) ((event_ctx->head + i) % IOLINK_EVENT_QUEUE_SIZE);
        const iolink_event_t* ev = &event_ctx->queue[idx];

        /* EventQualifier byte per IO-Link V1.1.5 (Table B.1):
         *   MODE     (bits 7-6): 11 = event appears
         *   TYPE     (bits 5-4): 01 notification, 10 warning, 11 error
         *   SOURCE   (bit  3)  : 0 = device (local)
         *   INSTANCE (bits 2-0): 2 = data link layer (DL) for comm events
         */
        uint8_t qualifier = 0xC0U; /* MODE = event appears */
        switch (ev->type) {
            case IOLINK_EVENT_TYPE_NOTIFICATION:
                qualifier |= (0x01U << 4);
                break;
            case IOLINK_EVENT_TYPE_WARNING:
                qualifier |= (0x02U << 4);
                break;
            case IOLINK_EVENT_TYPE_ERROR:
                qualifier |= (0x03U << 4);
                break;
            default:
                break;
        }
        qualifier |= 0x02U; /* INSTANCE = DL */

        ctx->response_buf[i * 3U] = qualifier;
        ctx->response_buf[i * 3U + 1U] = (uint8_t) (ev->code >> 8);
        ctx->response_buf[i * 3U + 2U] = (uint8_t) (ev->code & 0xFF);
    }
    ctx->response_len = (uint8_t) (count * 3U);
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
static uint8_t direct_param_mseq_capability(uint8_t m_seq_type)
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
            operate_code = 5U; /* OD 2, variable PD */
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
    const iolink_device_info_t* info = iolink_device_info_get();
    uint8_t pd_in = 0U;
    uint8_t pd_out = 0U;
    uint8_t m_seq = 0U;
    if (ctx->dll_ctx != NULL) {
        const iolink_dll_ctx_t* dll = (const iolink_dll_ctx_t*) ctx->dll_ctx;
        iolink_dll_get_pd_length(dll, &pd_in, &pd_out);
        m_seq = dll->m_seq_type;
    }

    (void) memset(page, 0, 16);
    /* 0x00 MasterCommand (W) and 0x01 MasterCycleTime default to 0. */
    page[0x02] = (info != NULL) ? info->min_cycle_time : 0U; /* MinCycleTime */
    page[0x03] = direct_param_mseq_capability(m_seq);        /* M-sequenceCapability */
    page[0x04] = 0x11U;                                      /* RevisionID = v1.1 */
    page[0x05] = direct_param_encode_pd(pd_in);              /* ProcessDataIn */
    page[0x06] = direct_param_encode_pd(pd_out);             /* ProcessDataOut */
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
    if (ctx->header.index == IOLINK_IDX_SYSTEM_COMMAND) {
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
