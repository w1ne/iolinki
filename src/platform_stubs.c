/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/platform.h"

/**
 * Weak defaults for NVM access (to be overridden by platform port)
 */
__attribute__((weak)) int iolink_nvm_read(uint32_t offset, uint8_t *data, size_t len)
{
    (void) offset;
    (void) data;
    (void) len;
    return -1; /* Not implemented */
}

__attribute__((weak)) int iolink_nvm_write(uint32_t offset, const uint8_t *data, size_t len)
{
    (void) offset;
    (void) data;
    (void) len;
    return -1; /* Not implemented */
}
