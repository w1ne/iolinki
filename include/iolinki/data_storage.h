/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_DATA_STORAGE_H
#define IOLINK_DATA_STORAGE_H

#include "iolinki/protocol.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @file data_storage.h
 * @brief IO-Link Data Storage (DS) for parameter backup and restore
 */

/**
 * @brief Maximum size of the serialized Data Storage parameter image.
 *
 * Must hold every DS-backed parameter record. Each record is laid out as
 * [Index(2, big-endian)][Subindex(1)][Length(1)][Data(Length)].
 */
#ifndef IOLINK_DS_IMAGE_MAX
#define IOLINK_DS_IMAGE_MAX 256U
#endif

/**
 * @brief DS engine states
 */
/**
 * @brief Data Storage (DS) Engine States
 *
 * Manages the transition between idle, uploading, and downloading states.
 */
typedef enum
{
    IOLINK_DS_STATE_IDLE = 0U,         /**< No active DS operation */
    IOLINK_DS_STATE_UPLOAD_REQ = 1U,   /**< Master requested parameter upload */
    IOLINK_DS_STATE_UPLOADING = 2U,    /**< Parameter upload in progress */
    IOLINK_DS_STATE_DOWNLOAD_REQ = 3U, /**< Master requested parameter download */
    IOLINK_DS_STATE_DOWNLOADING = 4U,  /**< Parameter download in progress */
    IOLINK_DS_STATE_LOCKED = 5U        /**< DS operation disabled/locked */
} iolink_ds_state_t;

/**
 * @brief Storage Abstraction Layer (SAL)
 *
 * Hardware-independent interface for persisting Data Storage parameters
 * to Non-Volatile Memory (NVM) such as Flash or EEPROM.
 */
typedef struct
{
    /**
     * @brief Read data from NVM
     * @param addr Destination-relative address
     * @param buf Source buffer
     * @param len Number of bytes to read
     * @return 0 on success, negative on hardware error
     */
    int (*read)(uint32_t addr, uint8_t* buf, size_t len);

    /**
     * @brief Write data to NVM
     * @param addr Destination-relative address
     * @param buf Source buffer
     * @param len Number of bytes to write
     * @return 0 on success, negative on hardware error
     */
    int (*write)(uint32_t addr, const uint8_t* buf, size_t len);

    /**
     * @brief Erase a range of NVM (if required by hardware)
     * @param addr Starting address
     * @param len Range length
     * @return 0 on success, negative on hardware error
     */
    int (*erase)(uint32_t addr, size_t len);
} iolink_ds_storage_api_t;

/**
 * @brief Data Storage Engine Context
 *
 * Tracks the state and checksums for the DS synchronization process.
 */
typedef struct
{
    iolink_ds_state_t state;                /**< Current DS state machine position */
    const iolink_ds_storage_api_t* storage; /**< Bound storage implementation API */
    uint16_t current_checksum;              /**< Last calculated local parameter checksum */
    uint16_t master_checksum;               /**< Most recent checksum verified by Master */
    uint8_t image[IOLINK_DS_IMAGE_MAX];     /**< Serialized parameter image (DS_Data 0x0003) */
    size_t image_len;                       /**< Valid bytes in @ref image */
} iolink_ds_ctx_t;

/**
 * @brief Initialize the Data Storage engine
 *
 * @param ctx DS context to initialize
 * @param storage Optional storage implementation hooks (can be NULL for RAM-only)
 */
void iolink_ds_init(iolink_ds_ctx_t* ctx, const iolink_ds_storage_api_t* storage);

/**
 * @brief Calculate a standard 16-bit checksum for a parameter block
 *
 * Used during DS compare operations to detect parity between Master and Device.
 *
 * @param data Pointer to the parameter data structure
 * @param len Length of the data in bytes
 * @return uint16_t Calculated CCITT-style or parity checksum
 */
uint16_t iolink_ds_calc_checksum(const uint8_t* data, size_t len);

/**
 * @brief Process Data Storage engine logic
 *
 * Handles state transitions and background synchronization tasks.
 *
 * @param ctx DS context to process
 */
void iolink_ds_process(iolink_ds_ctx_t* ctx);

/**
 * @brief Trigger a DS consistency check with the Master
 *
 * typically triggered by the ISDU engine upon Master comparison requests.
 *
 * @param ctx DS context
 * @param master_checksum The 16-bit checksum provided by the IO-Link Master
 */
void iolink_ds_check(iolink_ds_ctx_t* ctx, uint16_t master_checksum);

/**
 * @brief Start parameter upload to Master (System Command 0x95)
 *
 * @param ctx DS context
 * @return int 0 on success, negative if DS not initialized
 */
int iolink_ds_start_upload(iolink_ds_ctx_t* ctx);

/**
 * @brief Start parameter download from Master (System Command 0x96)
 *
 * @param ctx DS context
 * @return int 0 on success, negative if DS not initialized
 */
int iolink_ds_start_download(iolink_ds_ctx_t* ctx);

/**
 * @param ctx DS context
 * @return int 0 on success
 */
int iolink_ds_abort(iolink_ds_ctx_t* ctx);

/**
 * @brief Handle standard DS System Command
 *
 * @param ctx DS context
 * @param cmd System Command (0x05-0x08)
 * @param access_locks Current Access Lock state (Index 0x000C)
 * @return int 0: Success, -1: Busy, -2: Access Denied, -3: Unknown
 */
int iolink_ds_handle_command(iolink_ds_ctx_t* ctx, uint8_t cmd, uint16_t access_locks);

/**
 * @brief Serialize the device's DS-backed parameters into the image buffer.
 *
 * Reads every parameter in the DS set from the parameter manager and packs it
 * into @ref iolink_ds_ctx_t::image. Updates @ref iolink_ds_ctx_t::current_checksum.
 *
 * @param ctx DS context
 * @return int Number of image bytes built (>= 0), or negative on error.
 */
int iolink_ds_build_image(iolink_ds_ctx_t* ctx);

/**
 * @brief Apply a parameter image received from the Master (restore).
 *
 * Parses the serialized records and writes each parameter back through the
 * parameter manager (persisting to NVM). Updates @ref iolink_ds_ctx_t::current_checksum.
 *
 * @param ctx DS context
 * @param data Serialized image bytes
 * @param len  Number of image bytes
 * @return int 0 on success, negative if the image is malformed or rejected.
 */
int iolink_ds_apply_image(iolink_ds_ctx_t* ctx, const uint8_t* data, size_t len);

/**
 * @brief Get the current serialized parameter image (building it on demand).
 *
 * @param ctx DS context
 * @param out_len [out] Receives the image length in bytes
 * @return const uint8_t* Pointer to the image buffer, or NULL on error.
 */
const uint8_t* iolink_ds_get_image(iolink_ds_ctx_t* ctx, size_t* out_len);

/**
 * @brief Verify the stored image is consistent with its recorded checksum.
 *
 * @param ctx DS context
 * @return true if checksum(image) == current_checksum, false otherwise.
 */
bool iolink_ds_verify(const iolink_ds_ctx_t* ctx);

#endif  // IOLINK_DATA_STORAGE_H
