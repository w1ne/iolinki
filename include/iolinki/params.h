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
} iolink_params_ctx_t;

/**
 * @brief Initialize a parameter manager context.
 * @param ctx          Context to initialize.
 * @param device_info  Device-info context to bind (must remain valid).
 */
void iolink_params_ctx_init(iolink_params_ctx_t* ctx, iolink_device_info_ctx_t* device_info);

/**
 * @brief Retrieve a parameter value from a context by its IO-Link address.
 * @param ctx          Parameter manager context.
 * @param index        ISDU index.
 * @param subindex     ISDU subindex (0 for entire index, or 1-255).
 * @param[out] buffer  Destination buffer to store the value.
 * @param max_len      Size of the destination buffer.
 * @return Number of bytes read on success, or negative IO-Link ErrorCode.
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
 * @return 0 on success, or negative IO-Link ErrorCode.
 */
int iolink_params_ctx_set(iolink_params_ctx_t* ctx, uint16_t index, uint8_t subindex,
                          const uint8_t* data, size_t len, bool persist);

/**
 * @brief Reset all parameters in a context to factory defaults.
 * @param ctx Parameter manager context.
 */
void iolink_params_ctx_factory_reset(iolink_params_ctx_t* ctx);

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
 * Clears NVM and resets all writable parameters to their default values.
 */
void iolink_params_factory_reset(void);

/** @} */ /* end of iolinki_params */

#endif  // IOLINK_PARAMS_H
