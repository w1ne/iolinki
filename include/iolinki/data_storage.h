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
#include "iolinki/params.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @file data_storage.h
 * @brief IO-Link Data Storage (DS) for parameter backup and restore
 */

/**
 * @defgroup iolinki_data_storage Data Storage (DS)
 * @brief Parameter image serialization, checksum compare, and upload/download services.
 * @{
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
 * @brief Layout version of the serialized DS image record format above.
 *
 * The on-wire image (DS_Data, index 0x0003) carries no header, so the Master
 * checksum covers only the records. The version is stored in the header of
 * the backend record written by @ref iolink_ds_save.
 */
#define IOLINK_DS_IMAGE_LAYOUT_VERSION 1U

/** @brief Size of the header that @ref iolink_ds_save writes before the image. */
#define IOLINK_DS_STORE_HEADER_SIZE 8U

/**
 * @name Data Storage return codes
 * @{
 */
#define IOLINK_DS_OK 0                   /**< Success. */
#define IOLINK_DS_ERR_INVALID (-1)       /**< Bad argument, bad state or rejected image. */
#define IOLINK_DS_ERR_STORAGE (-2)       /**< NVM/storage backend missing or failed. */
#define IOLINK_DS_CMD_BUSY (-1)          /**< handle_command: state does not allow command. */
#define IOLINK_DS_CMD_DENIED (-2)        /**< handle_command: blocked by DeviceAccessLocks. */
#define IOLINK_DS_CMD_UNKNOWN (-3)       /**< handle_command: unsupported command. */
#define IOLINK_DS_CMD_COMMIT_FAILED (-4) /**< handle_command: download commit/store failed. */
/** @} */

/**
 * @name DS_Command values (Index 0x0003 Subindex 1, Table B.10)
 * @{
 */
#define IOLINK_DS_COMMAND_UPLOAD_START 0x01U   /**< DS_UploadStart. */
#define IOLINK_DS_COMMAND_UPLOAD_END 0x02U     /**< DS_UploadEnd. */
#define IOLINK_DS_COMMAND_DOWNLOAD_START 0x03U /**< DS_DownloadStart. */
#define IOLINK_DS_COMMAND_DOWNLOAD_END 0x04U   /**< DS_DownloadEnd. */
#define IOLINK_DS_COMMAND_BREAK 0x05U          /**< DS_Break. */
/** @} */

/**
 * @name State_Property coding (Index 0x0003 Subindex 2, Table B.10)
 * @{
 */
#define IOLINK_DS_STATE_PROPERTY_INACTIVE 0x00U    /**< Bits 1-2 = 0b00: Inactive. */
#define IOLINK_DS_STATE_PROPERTY_UPLOAD 0x02U      /**< Bits 1-2 = 0b01: Upload. */
#define IOLINK_DS_STATE_PROPERTY_DOWNLOAD 0x04U    /**< Bits 1-2 = 0b10: Download. */
#define IOLINK_DS_STATE_PROPERTY_LOCKED 0x06U      /**< Bits 1-2 = 0b11: Data Storage locked. */
#define IOLINK_DS_STATE_PROPERTY_UPLOAD_FLAG 0x80U /**< Bit 7: DS_UPLOAD_FLAG. */
/** @} */

/**
 * @brief Data Storage (DS) Engine States
 *
 * A session is opened in a REQ state by a Start command (or by
 * @ref iolink_ds_check). @ref iolink_ds_process moves REQ to the matching
 * active state (UPLOADING/DOWNLOADING). Both the REQ and the active state
 * stay open until an End, Store or Break command, @ref iolink_ds_abort, or a
 * communication restart closes the session. @ref iolink_ds_process never
 * closes a session.
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
    iolink_params_ctx_t* params_ctx;        /**< Device-local writable parameter context */
    uint16_t current_checksum;              /**< Last calculated local parameter checksum */
    uint16_t master_checksum;               /**< Most recent checksum verified by Master */
    uint8_t image[IOLINK_DS_IMAGE_MAX];     /**< Last valid snapshot of actual parameters */
    size_t image_len;                       /**< Valid bytes in @ref image */
    iolink_params_ctx_t candidate;          /**< Download staging copy, not yet applied */
    bool staged_valid;                      /**< True if @ref candidate holds staged changes */
    bool session_invalid;                   /**< A staged write in this download was rejected */
    bool ds_command_session;                /**< Session opened by DS_Command (0x0003/1) */
    bool upload_requested;                  /**< DS_UPLOAD_FLAG (Table B.10 State_Property bit 7) */
    int last_result;                        /**< Result of the last download commit */
} iolink_ds_ctx_t;

/**
 * @brief Initialize the Data Storage engine
 *
 * @param ctx DS context to initialize
 * @param storage Optional storage implementation hooks (can be NULL for RAM-only)
 */
void iolink_ds_init(iolink_ds_ctx_t* ctx, const iolink_ds_storage_api_t* storage);

/**
 * @brief Bind the writable parameter context used as the DS parameter source.
 *
 * @param ctx DS context
 * @param params_ctx Device-local parameter manager context to serialize/restore
 */
void iolink_ds_bind_params(iolink_ds_ctx_t* ctx, iolink_params_ctx_t* params_ctx);

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
 * Moves UPLOAD_REQ to UPLOADING and DOWNLOAD_REQ to DOWNLOADING. It does not
 * close, commit or cancel a session, so it is safe to call on every device
 * cycle. iolink_dll_process() calls it for the device context.
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
 * @brief Start parameter upload to Master (opens an upload session)
 *
 * @param ctx DS context
 * @return int 0 on success, negative if DS not initialized
 */
int iolink_ds_start_upload(iolink_ds_ctx_t* ctx);

/**
 * @brief Start parameter download from Master (opens a download session)
 *
 * @param ctx DS context
 * @return int 0 on success, negative if DS not initialized
 */
int iolink_ds_start_download(iolink_ds_ctx_t* ctx);

/**
 * @brief Abort any in-progress DS upload/download and return to idle.
 *
 * Discards a staged download image. Live parameters and the last valid image
 * are not changed.
 *
 * @param ctx DS context
 * @return int 0 on success
 */
int iolink_ds_abort(iolink_ds_ctx_t* ctx);

/**
 * @brief Handle a Block Parameterization SystemCommand (Table B.9, 0x01-0x06)
 *
 * - ParamDownloadStart (0x03): discards staging and opens a download session.
 * - ParamDownloadEnd (0x04): if a staged write was rejected in this session,
 *   fails without changes (Table 98 rule 4/7). Otherwise commits all staged
 *   changes with one parameter commit, then rebuilds image and checksum from
 *   the actual parameters. Outside a session it only rebuilds the image.
 * - ParamDownloadStore (0x05): as ParamDownloadEnd, then sets @ref
 *   iolink_ds_ctx_t::upload_requested and, if storage hooks are bound, stores
 *   the image with @ref iolink_ds_save. Also valid outside a session.
 * - ParamUploadStart/End (0x01/0x02): snapshot for upload / end of upload.
 * - ParamBreak (0x06): discards staging, live parameters unchanged.
 *
 * Download commands are refused while the DS bit of @p access_locks is set.
 * The ISDU layer passes the locks without the DS bit, because B.2.4 limits
 * the Data Storage lock to the DataStorageIndex.
 * A failed commit leaves live parameters and the last valid image unchanged
 * (@ref iolink_ds_ctx_t::last_result tells a rejected set from an NVM error).
 * A failed DS-backend store after a successful commit returns -4 although the
 * parameters are already committed: the two steps are not one transaction.
 *
 * @param ctx DS context
 * @param cmd SystemCommand (IOLINK_CMD_PARAM_*)
 * @param access_locks Current Access Lock state (Index 0x000C)
 * @return int 0: Success, -1: Busy, -2: Access Denied, -3: Unknown,
 *         -4: Download commit or store failed
 */
int iolink_ds_handle_command(iolink_ds_ctx_t* ctx, uint8_t cmd, uint16_t access_locks);

/**
 * @brief Handle a DS_Command (Index 0x0003 Subindex 1, Table B.10, Table 100).
 *
 * - DS_UploadStart / DS_DownloadStart: open a session (only from IDLE).
 * - DS_UploadEnd: close the upload session, clear DS_UPLOAD_FLAG (T9).
 * - DS_DownloadEnd: commit as ParamDownloadEnd; on success clear
 *   DS_UPLOAD_FLAG (T9). On failure the flag and parameters are unchanged.
 * - DS_Break: discard staging and close any session (T10).
 *
 * The caller checks the Data Storage access lock (Table 100 DSLocked_1).
 *
 * @param ctx DS context
 * @param ds_cmd DS_Command value (IOLINK_DS_COMMAND_*)
 * @return int 0: Success, -1: state does not permit the command,
 *         -3: reserved value, -4: commit failed (see last_result)
 */
int iolink_ds_handle_ds_command(iolink_ds_ctx_t* ctx, uint8_t ds_cmd);

/**
 * @brief Stage one parameter write inside an open download session.
 *
 * Validates the value into the session copy only (Table 98 rules 1-3). A
 * rejected value marks the session invalid, so the next End/Store fails
 * without changes (rule 4).
 *
 * @param ctx DS context
 * @param index ISDU index
 * @param subindex ISDU subindex
 * @param data Value octets
 * @param len Value length
 * @return int 0 if staged, @ref IOLINK_DS_ERR_INVALID otherwise.
 */
int iolink_ds_stage_param(iolink_ds_ctx_t* ctx, uint16_t index, uint8_t subindex,
                          const uint8_t* data, size_t len);

/** @brief True while a download session (REQ or active) is open. */
bool iolink_ds_download_open(const iolink_ds_ctx_t* ctx);

/** @brief True while an upload session (REQ or active) is open. */
bool iolink_ds_upload_open(const iolink_ds_ctx_t* ctx);

/** @brief True if @p index/@p subindex is in the DS parameter set. */
bool iolink_ds_is_ds_param(uint16_t index, uint8_t subindex);

/**
 * @brief State_Property octet (Index 0x0003 Subindex 2, Table B.10).
 *
 * @param ctx DS context
 * @param access_locks DeviceAccessLocks; the DS bit reports "locked"
 * @return uint8_t State_Property value
 */
uint8_t iolink_ds_state_property(const iolink_ds_ctx_t* ctx, uint16_t access_locks);

/**
 * @brief Data_Storage_Size (Index 0x0003 Subindex 3, Table B.10).
 *
 * Octets for all DS parameters at maximum length, including the structural
 * information (Index, Subindex, Length) of each record.
 */
uint32_t iolink_ds_storage_size(void);

/**
 * @brief Encode the Index_List (Index 0x0003 Subindex 5, Table B.11).
 *
 * Writes one [Index MSB][Index LSB][Subindex] entry per DS parameter and the
 * Termination_Marker 0x0000.
 *
 * @param out Output buffer
 * @param max_len Size of @p out
 * @return size_t Octets written, 0 if @p out is too small
 */
size_t iolink_ds_index_list(uint8_t* out, size_t max_len);

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
 * @brief Check a complete image without side effects.
 *
 * Rejects: empty image, length above @ref IOLINK_DS_IMAGE_MAX, truncated
 * header or data, an index/subindex outside the DS parameter set, duplicate
 * records, a value longer than the parameter maximum, and invalid values
 * (tags: embedded NUL octets).
 *
 * @param data Serialized image bytes
 * @param len  Number of image bytes
 * @return int 0 if the image is valid, @ref IOLINK_DS_ERR_INVALID otherwise.
 */
int iolink_ds_validate_image(const uint8_t* data, size_t len);

/**
 * @brief Apply a parameter image received from the Master (restore).
 *
 * Validates the whole image first (@ref iolink_ds_validate_image). Only a
 * valid image is staged into a copy of the parameters and committed with one
 * NVM write. On success, image and checksum are rebuilt from the actual
 * parameters. On any failure, live parameters, @ref iolink_ds_ctx_t::image
 * and @ref iolink_ds_ctx_t::current_checksum are unchanged.
 *
 * The commit is one iolink_nvm_write() call. This module does not guarantee
 * that the NVM backend writes it atomically.
 *
 * @param ctx DS context
 * @param data Serialized image bytes
 * @param len  Number of image bytes
 * @return int 0 on success, @ref IOLINK_DS_ERR_INVALID if the image is
 *         rejected, @ref IOLINK_DS_ERR_STORAGE if NVM is missing or failed.
 */
int iolink_ds_apply_image(iolink_ds_ctx_t* ctx, const uint8_t* data, size_t len);

/**
 * @brief Stage a download image for the next ParamDownloadEnd/Store.
 *
 * Allowed only in a download session (DOWNLOAD_REQ or DOWNLOADING). The image
 * is validated completely and its records are staged into the session copy.
 * A rejected image discards everything staged before and marks the session
 * invalid, so the next End/Store fails and cannot apply stale data.
 *
 * @param ctx DS context
 * @param data Serialized image bytes
 * @param len  Number of image bytes
 * @return int 0 if staged, @ref IOLINK_DS_ERR_INVALID otherwise.
 */
int iolink_ds_stage_image(iolink_ds_ctx_t* ctx, const uint8_t* data, size_t len);

/**
 * @brief Store the current image in the DS storage backend.
 *
 * Record: ['D']['S'][layout version][0][len(2, BE)][checksum(2, BE)][image].
 * Calls erase() first if the hook exists.
 *
 * @param ctx DS context
 * @return int 0 on success, @ref IOLINK_DS_ERR_STORAGE if no storage hooks
 *         are bound or a hook fails, @ref IOLINK_DS_ERR_INVALID on bad argument.
 */
int iolink_ds_save(iolink_ds_ctx_t* ctx);

/**
 * @brief Read and verify the image stored by @ref iolink_ds_save.
 *
 * Does not change live parameters or the context image.
 *
 * @param ctx      DS context
 * @param out      Destination buffer
 * @param max_len  Size of @p out
 * @param out_len  [out] Image length
 * @return int 0 on success, @ref IOLINK_DS_ERR_STORAGE if storage is missing
 *         or fails, @ref IOLINK_DS_ERR_INVALID if the record is corrupt or
 *         does not fit @p out.
 */
int iolink_ds_load(const iolink_ds_ctx_t* ctx, uint8_t* out, size_t max_len, size_t* out_len);

/**
 * @brief Get the serialized image of the actual parameters (rebuilt on each call).
 *
 * Never returns a staged or rejected download image.
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

/** @} */ /* end of iolinki_data_storage */

#endif  // IOLINK_DATA_STORAGE_H
