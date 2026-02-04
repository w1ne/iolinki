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

/**
 * @file isdu.h
 * @brief IO-Link Indexed Service Data Unit (ISDU) Acyclic Messaging
 */

/**
 * @brief ISDU Service Types
 */
typedef enum {
    IOLINK_ISDU_SERVICE_TYPE_READ = 0U,   /**< Read from Index/Subindex */
    IOLINK_ISDU_SERVICE_TYPE_WRITE = 1U   /**< Write to Index/Subindex */
} iolink_isdu_service_type_t;

/**
 * @brief ISDU Service Header
 * 
 * Defines the addressing and metadata for an acyclic request.
 */
typedef struct {
    uint8_t type;               /**< Service type (Read/Write) */
    uint8_t length;             /**< Payload length (if <= 232) */
    uint16_t index;             /**< Parameter Index (0-65535) */
    uint8_t subindex;           /**< Parameter Subindex (0-255) */
} iolink_isdu_header_t;

/**
 * @brief ISDU engine internal states
 */
typedef enum {
    ISDU_STATE_IDLE = 0U,            /**< Waiting for new Request */
    ISDU_STATE_HEADER_INITIAL = 1U,  /**< Parsing first byte (RW + Len) */
    ISDU_STATE_HEADER_EXT_LEN = 2U,  /**< Parsing extended length byte */
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
typedef struct {
    isdu_state_t state;                 /**< Current state machine position */
    uint8_t buffer[IOLINK_ISDU_BUFFER_SIZE]; /**< Request payload buffer */
    size_t buffer_idx;                  /**< Bytes captured in current buffer */
    iolink_isdu_header_t header;        /**< Decoded request header */
    uint8_t response_buf[IOLINK_ISDU_BUFFER_SIZE]; /**< Response payload buffer */
    size_t response_idx;                /**< Bytes sent from response buffer */
    size_t response_len;                /**< Total bytes in response buffer */
    
    /* Segmentation and Flow Control */
    isdu_state_t next_state;            /**< State to resume after sync/segmentation */
    uint8_t segment_seq;                /**< Expected Sequence Number for next segment */
    bool is_segmented;                  /**< Flag for multi-frame transfers */
    bool is_response_control_sent;      /**< Flag for per-segment Control Byte status */
    uint8_t error_code;                 /**< IO-Link ISDU Error Code (0x80XX) */
    
    /* Pointers to external dependencies */
    void *event_ctx;                    /**< Diagnostic host backlink */
} iolink_isdu_ctx_t;

/**
 * @brief Initialize the ISDU engine
 * 
 * @param ctx ISDU context to initialize
 */
void iolink_isdu_init(iolink_isdu_ctx_t *ctx);

/**
 * @brief Process ISDU engine logic
 * 
 * Executes service dispatch when a request is fully collected.
 * 
 * @param ctx ISDU context to process
 */
void iolink_isdu_process(iolink_isdu_ctx_t *ctx);

/**
 * @brief Collect a byte from an M-sequence (on-request data slot)
 * 
 * This is called by the DLL for every OD byte received while in PREOPERATE/OPERATE.
 * 
 * @param ctx ISDU context
 * @param byte Incoming data byte
 * @return int 0 if still collecting, 1 if request completely parsed, negative on protocol error
 */
int iolink_isdu_collect_byte(iolink_isdu_ctx_t *ctx, uint8_t byte);

/**
 * @brief Get the next byte to send in an ISDU response
 * 
 * Used by the DLL to fetch data for the master response.
 * 
 * @param ctx ISDU context
 * @param byte [out] Pointer to store the response byte
 * @return int 1 if byte fetched, 0 if no response data is ready
 */
int iolink_isdu_get_response_byte(iolink_isdu_ctx_t *ctx, uint8_t *byte);

#endif // IOLINK_ISDU_H
