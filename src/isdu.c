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
#include "iolinki/crc.h"
#include "iolinki/events.h"
#include "iolinki/device_info.h"
#include "iolinki/params.h"
#include "iolinki/data_storage.h"
#include "iolinki/platform.h"
#include "iolinki/utils.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>

/*
 * IO-Link ISDU Segmentation Engine
 *
 * ISDU Header (2 or 3 bytes):
 * [7:4] Service (Read/Write)
 * [3:0] Length (if < 15)
 * [ Index (16-bit) ]
 * [ Subindex (8-bit) ]
 */

/* ... includes ... */

/* Structs moved to header iolink_isdu_ctx_t */

void iolink_isdu_init(iolink_isdu_ctx_t *ctx)
{
    if (!iolink_ctx_zero(ctx, sizeof(iolink_isdu_ctx_t))) {
        return;
    }
    ctx->state = ISDU_STATE_IDLE;
    ctx->next_state = ISDU_STATE_IDLE;
}

static int isdu_handle_idle(iolink_isdu_ctx_t *ctx, uint8_t byte)
{
    bool start = ((byte & IOLINK_ISDU_CTRL_START) != 0U);
    bool last = ((byte & IOLINK_ISDU_CTRL_LAST) != 0U);
    uint8_t seq = (uint8_t) (byte & IOLINK_ISDU_CTRL_SEQ_MASK);

    if (!start) {
        return -1;
    }

    ctx->is_segmented = !last;
    ctx->segment_seq = seq;
    ctx->error_code = IOLINK_ISDU_ERROR_NONE;
    ctx->is_response_control_sent = false;
    ctx->buffer_idx = 0U;
    ctx->response_len = 0U;
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_HEADER_INITIAL;
    return 0;
}

int iolink_isdu_collect_byte(iolink_isdu_ctx_t *ctx, uint8_t byte)
{
    if (ctx == NULL) {
        return 0;
    }

    /* IO-Link V1.1 Control Byte parsing */
    bool start = ((byte & 0x80U) != 0U);
    bool last = ((byte & 0x40U) != 0U);
    uint8_t seq = (uint8_t) (byte & 0x3FU);

    /* Guard: Concurrent request detection.
     * Only trigger Busy if we receive a 'Start' bit while NOT expecting a Data byte.
     * States that expect Data: HEADER_INITIAL...DATA_COLLECT.
     */
    bool is_expecting_data =
        (ctx->state >= ISDU_STATE_HEADER_INITIAL && ctx->state <= ISDU_STATE_DATA_COLLECT);

    /* DEBUG */
    /* printf("ISDU: State=%d Byte=0x%02X Start=%d ExpectData=%d\n", ctx->state, byte, start,
     * is_expecting_data); */

    if (start && !is_expecting_data && (ctx->state != ISDU_STATE_IDLE) &&
        (ctx->state != ISDU_STATE_RESPONSE_READY)) {
        /* Collision: New Request Start Bit detected */

        /* Case 1: Service is already executing (Application Layer busy) */
        if (ctx->state == ISDU_STATE_SERVICE_EXECUTE || ctx->state == ISDU_STATE_BUSY) {
            ctx->response_buf[0] = 0x80U;
            ctx->response_buf[1] = IOLINK_ISDU_ERROR_BUSY;
            ctx->response_len = 2U;
            ctx->response_idx = 0U;
            ctx->is_response_control_sent = false;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return 1;
        }

        /* Case 2: Protocol Layer busy (Header/Data Collection) */
        /* Respond with BUSY to indicate we can't handle a new request immediately */
        ctx->response_buf[0] = 0x80U;
        ctx->response_buf[1] = IOLINK_ISDU_ERROR_BUSY;
        ctx->response_len = 2U;
        ctx->response_idx = 0U;
        ctx->is_response_control_sent = false;
        ctx->state = ISDU_STATE_RESPONSE_READY;
        return 1;
    }

    switch (ctx->state) {
        case ISDU_STATE_IDLE:
            return isdu_handle_idle(ctx, byte); /* Must start with 'Start' bit */

        case ISDU_STATE_HEADER_INITIAL: {
            uint8_t service = (uint8_t) ((byte >> 4) & 0x0FU);
            uint8_t length = (uint8_t) (byte & 0x0FU);

            if (service == IOLINK_ISDU_SERVICE_READ) {
                ctx->header.type = IOLINK_ISDU_SERVICE_READ;
                ctx->header.length = 0U;
                ctx->next_state = ISDU_STATE_HEADER_INDEX_HIGH;
            }
            else if (service == IOLINK_ISDU_SERVICE_WRITE) {
                ctx->header.type = IOLINK_ISDU_SERVICE_WRITE;
                if (length == 0U) {
                    ctx->next_state = ISDU_STATE_HEADER_EXT_LEN;
                }
                else {
                    ctx->header.length = length;
                    ctx->next_state = ISDU_STATE_HEADER_INDEX_HIGH;
                }
            }
            else {
                ctx->response_buf[0] = 0x80U;
                ctx->response_buf[1] = IOLINK_ISDU_ERROR_SERVICE_NOT_AVAIL;
                ctx->response_len = 2U;
                ctx->response_idx = 0U;
                ctx->state = ISDU_STATE_RESPONSE_READY;
                return -1;
            }
            ctx->buffer_idx = 0U;
            ctx->state = ctx->is_segmented ? ISDU_STATE_SEGMENT_COLLECT : ctx->next_state;
        } break;

        case ISDU_STATE_HEADER_EXT_LEN:
            ctx->header.length = byte;
            ctx->next_state = ISDU_STATE_HEADER_INDEX_HIGH;
            ctx->state = ctx->is_segmented ? ISDU_STATE_SEGMENT_COLLECT : ctx->next_state;
            break;

        case ISDU_STATE_HEADER_INDEX_HIGH:
            ctx->header.index = (uint16_t) (byte << 8);
            ctx->next_state = ISDU_STATE_HEADER_INDEX_LOW;
            ctx->state = ctx->is_segmented ? ISDU_STATE_SEGMENT_COLLECT : ctx->next_state;
            break;

        case ISDU_STATE_HEADER_INDEX_LOW:
            ctx->header.index |= byte;
            ctx->next_state = ISDU_STATE_HEADER_SUBINDEX;
            ctx->state = ctx->is_segmented ? ISDU_STATE_SEGMENT_COLLECT : ctx->next_state;
            break;

        case ISDU_STATE_HEADER_SUBINDEX:
            ctx->header.subindex = byte;
            if (ctx->header.type == IOLINK_ISDU_SERVICE_WRITE) {
                ctx->next_state = ISDU_STATE_DATA_COLLECT;
            }
            else {
                ctx->state = ISDU_STATE_SERVICE_EXECUTE;
                return 1;
            }
            ctx->state = ctx->is_segmented ? ISDU_STATE_SEGMENT_COLLECT : ctx->next_state;
            break;

        case ISDU_STATE_DATA_COLLECT:
            ctx->buffer[ctx->buffer_idx++] = byte;
            if (ctx->buffer_idx >= ctx->header.length) {
                ctx->state = ISDU_STATE_SERVICE_EXECUTE;
                return 1;
            }
            ctx->next_state = ISDU_STATE_DATA_COLLECT;
            if (ctx->is_segmented) {
                ctx->state = ISDU_STATE_SEGMENT_COLLECT;
            }
            break;

        case ISDU_STATE_SEGMENT_COLLECT:
            /* Expecting Control Byte */
            /* Start collision is handled at top of function now */
            if (seq != (uint8_t) ((ctx->segment_seq + 1) & 0x3F)) {
                ctx->response_buf[0] = 0x80U;
                ctx->response_buf[1] = 0x81U; /* Segmentation Error */
                ctx->response_len = 2U;
                ctx->response_idx = 0U;
                ctx->state = ISDU_STATE_RESPONSE_READY;
                return -1;
            }
            ctx->segment_seq = seq;
            ctx->is_segmented = (last == false);
            ctx->state = ctx->next_state;
            break;

        case ISDU_STATE_RESPONSE_READY:
            /* Response is being sent. Collection of NEW requests can only happen after response is
             * fully read. */
            /* Start collision handled at top? No, RESPONSE_READY excluded there. */
            if (start) {
                /* Implicit Abort of Response, Start New Request */
                ctx->state = ISDU_STATE_IDLE;
                return isdu_handle_idle(ctx, byte);
            }
            return 0;

        default:
            ctx->state = ISDU_STATE_IDLE;
            break;
    }
    return 0;
}

static void handle_mandatory_indices(iolink_isdu_ctx_t *ctx)
{
    const iolink_device_info_t *info = iolink_device_info_get();
    const char *str_data = NULL;

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
            ctx->response_buf[0] = (uint8_t) (info->device_id >> 24);
            ctx->response_buf[1] = (uint8_t) (info->device_id >> 16);
            ctx->response_buf[2] = (uint8_t) (info->device_id >> 8);
            ctx->response_buf[3] = (uint8_t) (info->device_id & 0xFF);
            ctx->response_len = 4U;
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
            if (ctx->header.type == IOLINK_ISDU_SERVICE_WRITE) {
                if (iolink_params_set(IOLINK_IDX_APPLICATION_TAG, 0U, ctx->buffer, ctx->buffer_idx,
                                      true) == 0) {
                    ctx->response_len = 0U;
                    ctx->response_idx = 0U;
                    ctx->state = ISDU_STATE_RESPONSE_READY;
                    return;
                }
            }
            else {
                int res = iolink_params_get(IOLINK_IDX_APPLICATION_TAG, 0U, ctx->response_buf,
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
            if (ctx->header.type == IOLINK_ISDU_SERVICE_WRITE) {
                if (iolink_params_set(IOLINK_IDX_FUNCTION_TAG, 0U, ctx->buffer, ctx->buffer_idx,
                                      true) == 0) {
                    ctx->response_len = 0U;
                    ctx->response_idx = 0U;
                    ctx->state = ISDU_STATE_RESPONSE_READY;
                    return;
                }
            }
            else {
                int res = iolink_params_get(IOLINK_IDX_FUNCTION_TAG, 0U, ctx->response_buf,
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
            if (ctx->header.type == IOLINK_ISDU_SERVICE_WRITE) {
                if (iolink_params_set(IOLINK_IDX_LOCATION_TAG, 0U, ctx->buffer, ctx->buffer_idx,
                                      true) == 0) {
                    ctx->response_len = 0U;
                    ctx->response_idx = 0U;
                    ctx->state = ISDU_STATE_RESPONSE_READY;
                    return;
                }
            }
            else {
                int res = iolink_params_get(IOLINK_IDX_LOCATION_TAG, 0U, ctx->response_buf,
                                            (size_t) IOLINK_ISDU_BUFFER_SIZE);
                if (res >= 0) {
                    ctx->response_len = (uint8_t) res;
                    ctx->response_idx = 0U;
                    ctx->state = ISDU_STATE_RESPONSE_READY;
                    return;
                }
            }
            break;

        case IOLINK_IDX_PDIN_DESCRIPTOR:
            /* Read-only: Returns PD Input descriptor (1 byte: PD length) */
            if (ctx->header.type == IOLINK_ISDU_SERVICE_WRITE) {
                ctx->response_buf[0] = 0x80U;
                ctx->response_buf[1] = IOLINK_ISDU_ERROR_WRITE_PROTECTED;
                ctx->response_len = 2U;
                ctx->response_idx = 0U;
                ctx->state = ISDU_STATE_RESPONSE_READY;
                return;
            }
            /* Return PD Input length (default: 2 bytes) */
            ctx->response_buf[0] = 2U; /* Default PD length */
            ctx->response_len = 1U;
            ctx->response_idx = 0U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;

        case IOLINK_IDX_DEVICE_STATUS:
            ctx->response_buf[0] =
                iolink_events_get_highest_severity((iolink_events_ctx_t *) ctx->event_ctx);
            ctx->response_len = 1U;
            ctx->response_idx = 0U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;

        case IOLINK_IDX_DETAILED_DEVICE_STATUS: {
            iolink_event_t events[8];
            uint8_t count =
                iolink_events_get_all((iolink_events_ctx_t *) ctx->event_ctx, events, 8U);
            if (count == 0U) {
                /* Special case: No events -> return 3 bytes of 0 as per spec pattern?
                   Actually, spec says it returns a list of active events. If none, length 0 or
                   error? Most masters expect 3 bytes per event: [Qualifier] [Code(2)]. If no
                   events, we'll return 0 length. */
                ctx->response_len = 0U;
            }
            else {
                for (uint8_t i = 0U; i < count; i++) {
                    /* Qualifier: [Type(7-6)] [Instance(5-0)]
                       IO-Link Spec: 00=Reserved, 01=Notification, 10=Warning, 11=Error */
                    uint8_t qualifier = 0U;
                    switch (events[i].type) {
                        case IOLINK_EVENT_TYPE_NOTIFICATION:
                            qualifier = 0x40U;
                            break;
                        case IOLINK_EVENT_TYPE_WARNING:
                            qualifier = 0x80U;
                            break;
                        case IOLINK_EVENT_TYPE_ERROR:
                            qualifier = 0xC0U;
                            break;
                        default:
                            qualifier = 0x00U;
                            break;
                    }
                    ctx->response_buf[i * 3] = qualifier;
                    ctx->response_buf[i * 3 + 1] = (uint8_t) (events[i].code >> 8);
                    ctx->response_buf[i * 3 + 2] = (uint8_t) (events[i].code & 0xFF);
                }
                ctx->response_len = (uint8_t) (count * 3U);
            }
            ctx->response_idx = 0U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
        }
            return;

        case IOLINK_IDX_REVISION_ID:
            ctx->response_buf[0] = (uint8_t) (info->revision_id >> 8);
            ctx->response_buf[1] = (uint8_t) (info->revision_id & 0xFF);
            ctx->response_len = 2U;
            ctx->response_idx = 0U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;

        case IOLINK_IDX_MIN_CYCLE_TIME:
            ctx->response_buf[0] = info->min_cycle_time;
            ctx->response_len = 1U;
            ctx->response_idx = 0U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
            return;

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

static void handle_system_command(iolink_isdu_ctx_t *ctx, uint8_t cmd)
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
            iolink_params_factory_reset();
            break;

        case IOLINK_CMD_RESTORE_APP_DEFAULTS: /* 0x83 */
            /* Reset application-specific parameters (currently same as factory) */
            iolink_params_factory_reset();
            break;

        case IOLINK_CMD_SET_COMM_MODE: /* 0x84 */
            /* Communication mode switching handled by DLL - this is a no-op */
            break;

        case IOLINK_CMD_PARAM_UPLOAD: /* 0x95 */
            /* Trigger Data Storage upload to Master */
            if (ctx->ds_ctx != NULL) {
                if (iolink_ds_start_upload((iolink_ds_ctx_t *) ctx->ds_ctx) != 0) {
                    /* DS busy or error */
                    ctx->response_buf[0] = 0x80U;
                    ctx->response_buf[1] = IOLINK_ISDU_ERROR_BUSY;
                    ctx->response_len = 2U;
                    ctx->response_idx = 0U;
                    ctx->state = ISDU_STATE_RESPONSE_READY;
                    return;
                }
            }
            break;

        case IOLINK_CMD_PARAM_DOWNLOAD: /* 0x96 */
            /* Trigger Data Storage download from Master */
            if (ctx->ds_ctx != NULL) {
                if (iolink_ds_start_download((iolink_ds_ctx_t *) ctx->ds_ctx) != 0) {
                    /* DS busy or error */
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
                (void) iolink_ds_abort((iolink_ds_ctx_t *) ctx->ds_ctx);
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

static void handle_access_locks(iolink_isdu_ctx_t *ctx)
{
    if (ctx->header.type == IOLINK_ISDU_SERVICE_READ) {
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

static void handle_detailed_device_status(iolink_isdu_ctx_t *ctx)
{
    if (ctx->header.type != IOLINK_ISDU_SERVICE_READ) {
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
    iolink_events_ctx_t *event_ctx = (iolink_events_ctx_t *) ctx->event_ctx;
    uint8_t count = event_ctx->count;
    if (count > 8U) count = 8U; /* Limit to 8 events in response */

    for (uint8_t i = 0U; i < count; i++) {
        /* Calculate index in FIFO */
        uint8_t idx = (uint8_t) ((event_ctx->head + i) % IOLINK_EVENT_QUEUE_SIZE);
        iolink_event_t *ev = &event_ctx->queue[idx];

        /* EventQualifier:
         * Mode: Appeared (0b10 << 6 = 0x80)
         * Type: Map iolink_event_type_t
         * Instance: DLL (0x02) for these, but let's keep it simple.
         */
        uint8_t qualifier = 0x80U; /* Appeared */
        switch (ev->type) {
            case IOLINK_EVENT_TYPE_NOTIFICATION:
                qualifier |= (0x01U << 3);
                break;
            case IOLINK_EVENT_TYPE_WARNING:
                qualifier |= (0x02U << 3);
                break;
            case IOLINK_EVENT_TYPE_ERROR:
                qualifier |= (0x03U << 3);
                break;
            default:
                break;
        }
        qualifier |= 0x02U; /* DLL instance as default for these errors */

        ctx->response_buf[i * 3U] = qualifier;
        ctx->response_buf[i * 3U + 1U] = (uint8_t) (ev->code >> 8);
        ctx->response_buf[i * 3U + 2U] = (uint8_t) (ev->code & 0xFF);
    }
    ctx->response_len = (uint8_t) (count * 3U);
    ctx->response_idx = 0U;
    ctx->state = ISDU_STATE_RESPONSE_READY;
    iolink_critical_exit();
}

static void isdu_write_u32_be(uint8_t *buf, size_t *idx, uint32_t value)
{
    buf[(*idx)++] = (uint8_t) ((value >> 24) & 0xFFU);
    buf[(*idx)++] = (uint8_t) ((value >> 16) & 0xFFU);
    buf[(*idx)++] = (uint8_t) ((value >> 8) & 0xFFU);
    buf[(*idx)++] = (uint8_t) (value & 0xFFU);
}

static void handle_error_stats(iolink_isdu_ctx_t *ctx)
{
    if (ctx->header.type != IOLINK_ISDU_SERVICE_READ) {
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
    iolink_dll_get_stats((const iolink_dll_ctx_t *) ctx->dll_ctx, &stats);

    size_t idx = 0U;
    isdu_write_u32_be(ctx->response_buf, &idx, stats.crc_errors);
    isdu_write_u32_be(ctx->response_buf, &idx, stats.timeout_errors);
    isdu_write_u32_be(ctx->response_buf, &idx, stats.framing_errors);
    isdu_write_u32_be(ctx->response_buf, &idx, stats.timing_errors);
    ctx->response_len = (uint8_t) idx;
}

static void handle_standard_commands(iolink_isdu_ctx_t *ctx)
{
    if (ctx->header.index == IOLINK_IDX_SYSTEM_COMMAND) {
        if (ctx->header.type == IOLINK_ISDU_SERVICE_WRITE) {
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
            /* Read of Index 2 - returns the oldest pending event code (2 bytes) */
            iolink_event_t ev;
            iolink_events_ctx_t *event_ctx = (iolink_events_ctx_t *) ctx->event_ctx;
            if (event_ctx != NULL && iolink_events_pop(event_ctx, &ev)) {
                ctx->response_buf[0] = (uint8_t) (ev.code >> 8);
                ctx->response_buf[1] = (uint8_t) (ev.code & 0xFF);
                ctx->response_len = 2U;
            }
            else {
                /* No events pending: return 0x0000 or error?
                 * Spec says if no events, return error or empty.
                 * We'll return 0x0000 (no event). */
                ctx->response_buf[0] = 0x00U;
                ctx->response_buf[1] = 0x00U;
                ctx->response_len = 2U;
            }
            ctx->response_idx = 0U;
            ctx->state = ISDU_STATE_RESPONSE_READY;
        }
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

void iolink_isdu_process(iolink_isdu_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    if (ctx->state == ISDU_STATE_BUSY) {
        /* In real implementation, we would check if the background task is done.
           For now, we just move to RESPONSE_READY or IDLE. */
        return;
    }

    if (ctx->state == ISDU_STATE_SERVICE_EXECUTE) {
        handle_standard_commands(ctx);
        if (ctx->state != ISDU_STATE_RESPONSE_READY) {
            ctx->state = ISDU_STATE_IDLE;
        }
    }
}

int iolink_isdu_get_response_byte(iolink_isdu_ctx_t *ctx, uint8_t *byte)
{
    if ((ctx == NULL) || (byte == NULL)) {
        return 0;
    }
    if (ctx->state != ISDU_STATE_RESPONSE_READY) {
        return 0;
    }

    if (!ctx->is_response_control_sent) {
        /* Send Control Byte: [Done(1)] [Error(1)] [Seq(6)] */
        uint8_t ctrl = 0x00U;
        if (ctx->response_idx == 0U) {
            ctrl |= IOLINK_ISDU_CTRL_START;
        }

        /* Last bit if this is the final segment */
        if ((uint8_t) (ctx->response_idx + 1U) >= ctx->response_len) {
            ctrl |= IOLINK_ISDU_CTRL_LAST;
        }
        /* Sequence number */
        ctrl |= (uint8_t) (ctx->segment_seq & IOLINK_ISDU_CTRL_SEQ_MASK);

        *byte = ctrl;
        ctx->is_response_control_sent = true;
        return 1;
    }

    if (ctx->response_idx < ctx->response_len) {
        *byte = ctx->response_buf[ctx->response_idx++];
        if (ctx->response_idx >= ctx->response_len) {
            ctx->state = ISDU_STATE_IDLE;
        }
        else {
            /* Mandatory for V1.1.5 on OD=1: Every byte is preceded by Control Byte. */
            ctx->is_response_control_sent = false;
            ctx->segment_seq = (ctx->segment_seq + 1) & 0x3F;
        }
        return 1;
    }

    ctx->state = ISDU_STATE_IDLE;
    return 0;
}
