/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_UTILS_H
#define IOLINK_UTILS_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/**
 * @file utils.h
 * @brief Miscellaneous helper utilities.
 *
 * Small inline helpers for buffer validation and context zeroing used
 * throughout the IO-Link stack.
 */

/**
 * @defgroup iolinki_utils Miscellaneous Utilities
 * @brief Inline buffer-validation and memory helpers.
 * @{
 */

/**
 * @brief Validate a (pointer, length) buffer pair.
 *
 * A buffer is considered valid unless it is NULL while claiming a non-zero
 * length. A NULL pointer with zero length is valid.
 *
 * @param data  Buffer pointer to validate.
 * @param len   Claimed length of @p data in bytes.
 * @return true if the pair is valid, false otherwise.
 */
static inline bool iolink_buf_is_valid(const void* data, size_t len)
{
    return !((data == NULL) && (len > 0U));
}

/**
 * @brief Zero-initialize a context object.
 *
 * @param[out] ctx  Object to clear; ignored (returns false) if NULL.
 * @param len       Number of bytes to zero.
 * @return true if @p ctx was cleared, false if @p ctx was NULL.
 */
static inline bool iolink_ctx_zero(void* ctx, size_t len)
{
    if (ctx == NULL) {
        return false;
    }
    (void) memset(ctx, 0, len);
    return true;
}

/** @} */ /* end of iolinki_utils */

#endif /* IOLINK_UTILS_H */
