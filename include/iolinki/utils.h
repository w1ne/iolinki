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

static inline bool iolink_buf_is_valid(const void *data, size_t len)
{
    return !((data == NULL) && (len > 0U));
}

static inline bool iolink_ctx_zero(void *ctx, size_t len)
{
    if (ctx == NULL) {
        return false;
    }
    (void) memset(ctx, 0, len);
    return true;
}

#endif /* IOLINK_UTILS_H */
