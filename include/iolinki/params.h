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
 * @brief Initialize the parameter manager.
 * 
 * Attempts to load persistent parameters from NVM.
 */
void iolink_params_init(void);

/**
 * @brief Get a parameter value.
 * @param index ISDU Index
 * @param subindex ISDU Subindex
 * @param buffer Buffer to store value
 * @param max_len Size of buffer
 * @return int Number of bytes read, or negative error code
 */
int iolink_params_get(uint16_t index, uint8_t subindex, uint8_t *buffer, size_t max_len);

/**
 * @brief Set a parameter value.
 * @param index ISDU Index
 * @param subindex ISDU Subindex
 * @param data Data to write
 * @param len Length of data
 * @param persist If true, save to NVM immediately
 * @return int 0 on success, negative on error
 */
int iolink_params_set(uint16_t index, uint8_t subindex, const uint8_t *data, size_t len, bool persist);

#endif // IOLINK_PARAMS_H
