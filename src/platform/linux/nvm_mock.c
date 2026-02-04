/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/platform.h"
#include "iolinki/utils.h"
#include <stdio.h>
#include <string.h>

#define NVM_FILE "iolink_nvm.bin"

int iolink_nvm_read(uint32_t offset, uint8_t *data, size_t len)
{
    if (!iolink_buf_is_valid(data, len)) {
        return -1;
    }
    FILE *f = fopen(NVM_FILE, "rb");
    if (f == NULL) {
        return -1;
    }

    if (fseek(f, (long) offset, SEEK_SET) != 0) {
        (void) fclose(f);
        return -1;
    }

    size_t read = fread(data, 1, len, f);
    (void) fclose(f);

    return (read == len) ? 0 : -1;
}

int iolink_nvm_write(uint32_t offset, const uint8_t *data, size_t len)
{
    if (!iolink_buf_is_valid(data, len)) {
        return -1;
    }
    /* Use a+b to avoid truncating and create file if missing */
    FILE *f = fopen(NVM_FILE, "a+b");
    if (f == NULL) {
        return -1;
    }

    if (fseek(f, (long) offset, SEEK_SET) != 0) {
        (void) fclose(f);
        return -1;
    }

    size_t written = fwrite(data, 1, len, f);
    (void) fclose(f);

    return (written == len) ? 0 : -1;
}
