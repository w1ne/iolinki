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

/**
 * @file params.h
 * @brief IO-Link Parametrization Manager
 */

/**
 * @brief Initialize the parameter manager
 *
 * Sets up internal lookup tables and attempts to load persistent
 * configuration from Non-Volatile Memory (NVM).
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
int iolink_params_get(uint16_t index, uint8_t subindex, uint8_t *buffer, size_t max_len);

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
int iolink_params_set(uint16_t index, uint8_t subindex, const uint8_t *data, size_t len,
                      bool persist);

/**
 * @brief Reset all parameters to factory defaults
 *
 * Clears NVM and resets all writable parameters to their default values.
 */
void iolink_params_factory_reset(void);

#endif  // IOLINK_PARAMS_H
