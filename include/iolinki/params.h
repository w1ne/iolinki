/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_PARAMS_H
#define IOLINK_PARAMS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "iolinki/device_info.h"

/**
 * @file params.h
 * @brief IO-Link Parametrization Manager
 */

/**
 * @defgroup iolinki_params Parameter Manager
 * @brief Read/write access to IO-Link parameters by ISDU index/subindex.
 * @{
 */

/** @brief Maximum octets of a tag parameter (Index 0x0018-0x001A, StringT). */
#define IOLINK_PARAMS_TAG_MAX 32U

/**
 * @brief NVM parameter record identification.
 *
 * The record is serialized octet by octet (no struct padding), big-endian:
 *   [0..3]   magic "IOLP"
 *   [4]      layout version (@ref IOLINK_PARAMS_NVM_LAYOUT_VERSION)
 *   [5]      flags (@ref IOLINK_PARAMS_NVM_FLAG_ACCESS_LOCKS, tag-valid bits)
 *   [6..7]   DeviceAccessLocks (Index 0x000C)
 *   [8]      ApplicationTag length, [9..40]   ApplicationTag octets
 *   [41]     FunctionTag length,    [42..73]  FunctionTag octets
 *   [74]     LocationTag length,    [75..106] LocationTag octets
 *   [107..108] Fletcher-16 over octets [0..106]
 *
 * A record with a wrong magic, unknown version, length > 32 or bad checksum is
 * ignored at init (defaults apply). The legacy layout-1 record ("IOL1") is
 * still accepted for reading; every write uses the current layout.
 */
#define IOLINK_PARAMS_NVM_MAGIC 0x494F4C50U /* "IOLP" */
#define IOLINK_PARAMS_NVM_LAYOUT_VERSION 2U
#define IOLINK_PARAMS_NVM_RECORD_SIZE 109U
#define IOLINK_PARAMS_NVM_FLAG_APPLICATION_TAG 0x01U
#define IOLINK_PARAMS_NVM_FLAG_FUNCTION_TAG 0x02U
#define IOLINK_PARAMS_NVM_FLAG_LOCATION_TAG 0x04U
#define IOLINK_PARAMS_NVM_FLAG_ACCESS_LOCKS 0x08U

/** @brief Default NVM offset of the parameter record (legacy singleton and ctx_init). */
#ifndef IOLINK_PARAMS_NVM_DEFAULT_OFFSET
#define IOLINK_PARAMS_NVM_DEFAULT_OFFSET 0U
#endif

/** @brief DeviceAccessLocks bits defined by V1.1.5 (bits 4..15 are reserved). */
#define IOLINK_PARAMS_ACCESS_LOCKS_DEFINED 0x000FU

/**
 * @brief Per-device parameter manager context.
 */
typedef struct
{
    char application_tag[33];              /**< Application tag (32 bytes + NUL). */
    char function_tag[33];                 /**< Function tag (32 bytes + NUL). */
    char location_tag[33];                 /**< Location tag (32 bytes + NUL). */
    bool application_tag_valid;            /**< True if the application tag has been set. */
    bool function_tag_valid;               /**< True if the function tag has been set. */
    bool location_tag_valid;               /**< True if the location tag has been set. */
    iolink_device_info_ctx_t* device_info; /**< Associated device-info context. */
    uint32_t nvm_offset;                   /**< NVM offset of this device's parameter record. */
} iolink_params_ctx_t;

/**
 * @brief Initialize a parameter manager context.
 * @param ctx          Context to initialize.
 * @param device_info  Device-info context to bind (must remain valid).
 */
void iolink_params_ctx_init(iolink_params_ctx_t* ctx, iolink_device_info_ctx_t* device_info);

/**
 * @brief Initialize a parameter manager context bound to a device-specific NVM offset.
 *
 * Each device instance that shares one NVM must use a distinct, non-overlapping
 * offset (at least @ref IOLINK_PARAMS_NVM_RECORD_SIZE apart).
 * A valid record at @p nvm_offset restores tags and DeviceAccessLocks.
 *
 * @param ctx          Context to initialize.
 * @param device_info  Device-info context to bind (must remain valid).
 * @param nvm_offset   NVM offset of this device's parameter record.
 * @return 0 if a valid record was restored, 1 if defaults were used,
 *         negative if @p ctx is NULL or the record range overflows the 32-bit
 *         NVM address space (the context then holds defaults and every
 *         persistent write fails).
 */
int iolink_params_ctx_init_at(iolink_params_ctx_t* ctx, iolink_device_info_ctx_t* device_info,
                              uint32_t nvm_offset);

/**
 * @brief Retrieve a parameter value from a context by its IO-Link address.
 * @param ctx          Parameter manager context.
 * @param index        ISDU index.
 * @param subindex     ISDU subindex (0 for entire index, or 1-255).
 * @param[out] buffer  Destination buffer to store the value.
 * @param max_len      Size of the destination buffer.
 * @return Number of bytes read on success, or negative IO-Link ErrorCode.
 * Index 0x000C returns DeviceAccessLocks as 2 octets, big-endian.
 */
int iolink_params_ctx_get(const iolink_params_ctx_t* ctx, uint16_t index, uint8_t subindex,
                          uint8_t* buffer, size_t max_len);

/**
 * @brief Update a parameter value on a context.
 * @param ctx       Parameter manager context.
 * @param index     ISDU index.
 * @param subindex  ISDU subindex.
 * @param data      Pointer to the new data to write.
 * @param len       Length of the new data in bytes.
 * @param persist   If true, synchronously commit the change to NVM.
 * A failed NVM commit returns failure without changing the context/readback.
 * Tags (0x0018-0x001A) accept 0..32 octets; longer values are rejected.
 * Index 0x000C (DeviceAccessLocks) takes exactly 2 octets, big-endian.
 * @return 0 on success, or negative IO-Link ErrorCode.
 */
int iolink_params_ctx_set(iolink_params_ctx_t* ctx, uint16_t index, uint8_t subindex,
                          const uint8_t* data, size_t len, bool persist);

/**
 * @brief Reset all parameters in a context to factory defaults.
 * Resets RAM and attempts NVM; this legacy void API cannot report storage failure.
 * @param ctx Parameter manager context.
 */
void iolink_params_ctx_factory_reset(iolink_params_ctx_t* ctx);

/**
 * @brief Reset a context to factory defaults and report the persistence result.
 *
 * Tags become empty and DeviceAccessLocks return to the configured default.
 * The reset record is written to NVM first; RAM changes only if that write
 * succeeds. With no NVM backend the call fails and RAM is unchanged.
 *
 * @param ctx Parameter manager context.
 * @return 0 on success, negative on invalid argument or NVM failure.
 */
int iolink_params_ctx_factory_reset_checked(iolink_params_ctx_t* ctx);

/**
 * @brief Validate one parameter write into a staging copy only.
 *
 * Changes only the fields of @p staged. Never writes NVM and never touches the
 * bound device-info context. Use with @ref iolink_params_ctx_commit to apply
 * several parameters with one NVM write.
 *
 * @param staged   Staging copy (typically a struct copy of the live context).
 * @param index    ISDU index.
 * @param subindex ISDU subindex.
 * @param data     New value.
 * @param len      Value length in octets.
 * @return 0 if accepted, negative if the index is unknown or the value is invalid.
 */
int iolink_params_ctx_stage(iolink_params_ctx_t* staged, uint16_t index, uint8_t subindex,
                            const uint8_t* data, size_t len);

/**
 * @brief Commit a staged copy to the live context.
 *
 * If @p persist is true, the complete record is written to NVM with one
 * iolink_nvm_write() call first. If that write fails, @p ctx is unchanged and
 * the call returns negative. The NVM record itself is not transactional: the
 * write result depends on the platform backend, and a torn record is detected
 * by its checksum at the next init.
 *
 * @param ctx     Live context.
 * @param staged  Staging copy produced from @p ctx by @ref iolink_params_ctx_stage.
 * @param persist If true, write the record to NVM before the RAM commit.
 * @return 0 on success, negative on invalid argument or NVM failure.
 */
int iolink_params_ctx_commit(iolink_params_ctx_t* ctx, const iolink_params_ctx_t* staged,
                             bool persist);

/**
 * @brief Get DeviceAccessLocks (Index 0x000C) of the bound device.
 * @param ctx Parameter manager context.
 * @return Current locks value, 0 if no device-info context is bound.
 */
uint16_t iolink_params_ctx_get_access_locks(const iolink_params_ctx_t* ctx);

/**
 * @brief Set DeviceAccessLocks (Index 0x000C) of the bound device.
 *
 * Reserved bits (4..15) are rejected. If @p persist is true, the record is
 * written to NVM first and RAM changes only if the write succeeds.
 *
 * @param ctx     Parameter manager context.
 * @param locks   New locks value.
 * @param persist If true, store the value in NVM.
 * @return 0 on success, negative on invalid value or NVM failure.
 */
int iolink_params_ctx_set_access_locks(iolink_params_ctx_t* ctx, uint16_t locks, bool persist);

/**
 * @brief Initialize the parameter manager
 *
 * Compatibility wrapper around the context API.
 */
void iolink_params_init(void);

/**
 * @brief Retrieve a parameter value by its IO-Link address
 *
 * @param index ISDU Index (e.g. 0x10 for Vendor Name)
 * @param subindex ISDU Subindex (0 for entire index, or 1-255)
 * @param buffer [out] Destination buffer to store the value
 * @param max_len Size of the destination buffer
 * @return int Number of bytes read on success, or negative IO-Link ErrorCode
 */
int iolink_params_get(uint16_t index, uint8_t subindex, uint8_t* buffer, size_t max_len);

/**
 * @brief Update a parameter value
 *
 * @param index ISDU Index
 * @param subindex ISDU Subindex
 * @param data Pointer to the new data to write
 * @param len Length of the new data in bytes
 * @param persist If true, synchronously commit the change to NVM
 * @return int 0 on success, or negative IO-Link ErrorCode (e.g. 0x80XX)
 */
int iolink_params_set(uint16_t index, uint8_t subindex, const uint8_t* data, size_t len,
                      bool persist);

/**
 * @brief Reset all parameters to factory defaults
 *
 * Resets writable parameters in RAM and attempts to persist the reset.
 * This legacy void API cannot report unavailable or failed storage.
 */
void iolink_params_factory_reset(void);

/**
 * @brief Get the legacy singleton parameter context used by the context-less API.
 * @return Pointer to the process-wide legacy context (initialized on demand).
 */
iolink_params_ctx_t* iolink_params_get_legacy_ctx(void);

/** @} */ /* end of iolinki_params */

#endif  // IOLINK_PARAMS_H
