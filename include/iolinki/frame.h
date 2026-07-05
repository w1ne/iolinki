/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_FRAME_H
#define IOLINK_FRAME_H

#include "iolinki/config.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @file frame.h
 * @brief Shared IO-Link frame encoding and decoding helpers.
 *
 * Provides M-sequence frame builders for Type-0 and Type-1 requests and a
 * decoder for the Device's OPERATE-mode response frame.
 */

/**
 * @defgroup iolinki_frame M-sequence Frame Handling
 * @brief Encode/decode helpers for IO-Link M-sequence frames.
 * @{
 */

/**
 * @brief Decoded OPERATE-mode response frame.
 *
 * Holds the fields extracted from a Device response during cyclic exchange.
 */
typedef struct
{
    uint8_t status;                   /**< Status/checksum (CKS) byte from the frame. */
    bool pd_valid;                    /**< True if the Process Data valid flag is set. */
    bool event_pending;               /**< True if the frame signals a pending event. */
    bool checksum_ok;                 /**< True if the frame checksum verified correctly. */
    uint8_t pd[IOLINK_PD_IN_MAX_SIZE]; /**< Decoded input Process Data bytes. */
    uint8_t pd_len;                   /**< Number of valid bytes in @ref pd. */
    uint8_t od[IOLINK_OD_MAX_SIZE];   /**< Decoded On-request Data bytes. */
    uint8_t od_len;                   /**< Number of valid bytes in @ref od. */
} iolink_frame_operate_response_t;

/**
 * @brief Encode a Type-0 (read) request frame.
 *
 * @param mc Master Control (MC) byte for the request.
 * @param[out] out Destination buffer for the encoded frame.
 * @param out_size Capacity of @p out in bytes.
 * @return Number of bytes written on success, negative on error (e.g. buffer too small).
 */
int iolink_frame_encode_type0(uint8_t mc, uint8_t* out, size_t out_size);

/**
 * @brief Encode a Type-0 write request frame with a single On-request Data byte.
 *
 * @param mc Master Control (MC) byte for the request.
 * @param od On-request Data byte to transmit.
 * @param[out] out Destination buffer for the encoded frame.
 * @param out_size Capacity of @p out in bytes.
 * @return Number of bytes written on success, negative on error.
 */
int iolink_frame_encode_type0_write(uint8_t mc, uint8_t od, uint8_t* out, size_t out_size);

/**
 * @brief Encode a Type-1 cyclic exchange frame.
 *
 * @param pd_out Pointer to the output Process Data to embed.
 * @param pd_out_len Number of output Process Data bytes.
 * @param od_len Number of On-request Data bytes in the exchange.
 * @param[out] out Destination buffer for the encoded frame.
 * @param out_size Capacity of @p out in bytes.
 * @return Number of bytes written on success, negative on error.
 */
int iolink_frame_encode_type1_cycle(const uint8_t* pd_out, uint8_t pd_out_len, uint8_t od_len,
                                    uint8_t* out, size_t out_size);

/**
 * @brief Decode a Device OPERATE-mode response frame.
 *
 * @param frame Pointer to the raw received frame bytes.
 * @param frame_len Length of @p frame in bytes.
 * @param pd_in_len Expected input Process Data length.
 * @param od_len Expected On-request Data length.
 * @param[out] out Structure populated with the decoded response fields.
 * @return 0 on success, negative on decode/length error.
 */
int iolink_frame_decode_operate_response(const uint8_t* frame, size_t frame_len, uint8_t pd_in_len,
                                         uint8_t od_len, iolink_frame_operate_response_t* out);

/** @} */ /* end of iolinki_frame */

#endif /* IOLINK_FRAME_H */
