/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_ISDU_H
#define IOLINK_ISDU_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "iolinki/config.h"
#include "iolinki/params.h"

/**
 * @file isdu.h
 * @brief IO-Link Indexed Service Data Unit (ISDU) Acyclic Messaging
 */

/**
 * @defgroup iolinki_isdu ISDU Service (acyclic parameters)
 * @brief Indexed acyclic read/write service engine and request/response handling.
 * @{
 */

/**
 * @brief ISDU Service Types
 */
typedef enum
{
    IOLINK_ISDU_SERVICE_TYPE_READ = 0U, /**< Read from Index/Subindex */
    IOLINK_ISDU_SERVICE_TYPE_WRITE = 1U /**< Write to Index/Subindex */
} iolink_isdu_service_type_t;

/**
 * @brief ISDU Service Header
 *
 * Defines the addressing and metadata for an acyclic request.
 */
typedef struct
{
    uint8_t type;     /**< Service type (Read/Write) */
    uint8_t length;   /**< Payload length (if <= 232) */
    uint16_t index;   /**< Parameter Index (0-65535) */
    uint8_t subindex; /**< Parameter Subindex (0-255) */
} iolink_isdu_header_t;

/**
 * @brief ISDU engine internal states
 */
typedef enum
{
    ISDU_STATE_IDLE = 0U,              /**< Waiting for new Request */
    ISDU_STATE_HEADER_INITIAL = 1U,    /**< Parsing first byte (RW + Len) */
    ISDU_STATE_HEADER_EXT_LEN = 2U,    /**< Parsing extended length byte */
    ISDU_STATE_HEADER_INDEX_HIGH = 3U, /**< Parsing Index High byte */
    ISDU_STATE_HEADER_INDEX_LOW = 4U,  /**< Parsing Index Low byte */
    ISDU_STATE_HEADER_SUBINDEX = 5U,   /**< Parsing Subindex byte */
    ISDU_STATE_DATA_COLLECT = 6U,      /**< Collecting payload for WRITE */
    ISDU_STATE_SEGMENT_COLLECT = 7U,   /**< Waiting for next segment in multi-frame write */
    ISDU_STATE_SERVICE_EXECUTE = 8U,   /**< Dispatching to application layer */
    ISDU_STATE_RESPONSE_READY = 9U,    /**< Response buffer populated, awaiting retrieval */
    ISDU_STATE_BUSY = 10U              /**< Internal command execution in progress */
} isdu_state_t;

/**
 * @brief ISDU Service Context
 *
 * Holds buffers and state for the acyclic messaging engine.
 */
typedef struct
{
    isdu_state_t state;                            /**< Current state machine position */
    uint8_t buffer[IOLINK_ISDU_BUFFER_SIZE];       /**< Request payload buffer */
    size_t buffer_idx;                             /**< Bytes captured in current buffer */
    iolink_isdu_header_t header;                   /**< Decoded request header */
    uint8_t response_buf[IOLINK_ISDU_BUFFER_SIZE]; /**< Response payload buffer */
    size_t response_idx;                           /**< Bytes sent from response buffer */
    size_t response_len;                           /**< Total bytes in response buffer */

    /* Spec ISDU transport (C3, Table 52) */
    uint8_t req_buf[IOLINK_ISDU_BUFFER_SIZE];  /**< Raw ISDU request octets (incl. CHKPDU) */
    uint8_t resp_buf[IOLINK_ISDU_BUFFER_SIZE]; /**< Raw ISDU response octets (incl. CHKPDU) */
    size_t req_idx;                            /**< Octets collected in req_buf */
    size_t req_total;      /**< Expected total ISDU length from Length/ExtLength */
    size_t resp_idx;       /**< Octets already emitted from resp_buf */
    size_t resp_total;     /**< Total octets in resp_buf */
    uint8_t last_flowctrl; /**< FlowCTRL of the previous ISDU message */
    bool flowctrl_seen;    /**< A FlowCTRL has been received since START */
    bool repeat_pending;   /**< Last FlowCTRL repeated: ignore the payload */
    uint8_t error_code;    /**< IO-Link ISDU Error Code (0x80XX) */

    /* Pointers to external dependencies */
    void* event_ctx;                 /**< Diagnostic host backlink */
    void* ds_ctx;                    /**< Data Storage context for system commands */
    void* dll_ctx;                   /**< DLL context for statistics access */
    iolink_params_ctx_t* params_ctx; /**< Device-local writable parameter context */
    void* direct_param_page2;        /**< Per-device Direct Parameter page 2 storage */

    /* System Command Flags */
    bool reset_pending;     /**< Device reset requested (0x80) */
    bool app_reset_pending; /**< Application reset requested (0x81) */
} iolink_isdu_ctx_t;

/**
 * @brief Initialize the ISDU engine
 *
 * @param ctx ISDU context to initialize
 */
void iolink_isdu_init(iolink_isdu_ctx_t* ctx);

/**
 * @brief Process ISDU engine logic
 *
 * Executes service dispatch when a request is fully collected.
 *
 * @param ctx ISDU context to process
 */
void iolink_isdu_process(iolink_isdu_ctx_t* ctx);

/**
 * @brief Feed one On-request Data message received on the ISDU channel (C3).
 *
 * The M-sequence control octet carries FlowCTRL in its address bits
 * (Table 52). START resets the request buffer; COUNT 1,2,..,0,1,.. follows;
 * a repeated FlowCTRL repeats the previous message (its payload is ignored);
 * any other value is an ISDUError (drop the request, return to Idle).
 * The request is complete when the received octet count equals the Length
 * (or ExtLength) declared in the I-Service octet; CHKPDU is verified then.
 *
 * @param ctx ISDU context
 * @param flowctrl FlowCTRL value (mc & 0x1F)
 * @param od Incoming OD octets of this message
 * @param od_len Number of OD octets
 */
void iolink_isdu_od_write(iolink_isdu_ctx_t* ctx, uint8_t flowctrl, const uint8_t* od,
                          uint8_t od_len);

/**
 * @brief Produce the OD octets of a device reply on the ISDU channel (C3).
 *
 * On a START read while the application response is not ready the device
 * answers a single Busy octet (0x01, Table A.14). Once the response is
 * ready the framed ISDU octets follow across reads carrying COUNT. IDLE
 * returns to Idle; ABORT discards the service.
 *
 * @param ctx ISDU context
 * @param flowctrl FlowCTRL value (mc & 0x1F)
 * @param od_out [out] Buffer of od_len octets
 * @param od_len Number of OD octets to fill
 */
void iolink_isdu_od_read(iolink_isdu_ctx_t* ctx, uint8_t flowctrl, uint8_t* od_out, uint8_t od_len);

/**
 * @brief Get the next octet of the application-level ISDU response payload.
 *
 * The wire framing (I-Service/Length/CHKPDU) is applied by the transport; this
 * accessor exposes only the payload so callers and tests can inspect the
 * handler result. Negative responses yield {0x80, AdditionalCode}.
 *
 * @param ctx ISDU context
 * @param byte [out] Pointer to store the response octet
 * @return int 1 if an octet was produced, 0 when the response is exhausted
 */
int iolink_isdu_get_response_byte(iolink_isdu_ctx_t* ctx, uint8_t* byte);

/**
 * @brief Read a single Direct Parameter page 1 octet.
 *
 * Builds the current Direct Parameter page 1 image (Table B.1) and returns the
 * octet at the given address. Used by the DLL to answer a startup Type-0 read on
 * the page communication channel.
 *
 * @param ctx ISDU context
 * @param addr Direct Parameter page address (0x00-0x0F)
 * @return uint8_t Octet value, or 0 for an out-of-range address or NULL context
 */
uint8_t iolink_isdu_direct_param_page1_octet(iolink_isdu_ctx_t* ctx, uint8_t addr);

/** @} */ /* end of iolinki_isdu */

#endif  // IOLINK_ISDU_H
