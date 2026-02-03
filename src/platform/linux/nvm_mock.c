/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/platform.h"
#include <stdio.h>
#include <string.h>

#define NVM_FILE "iolink_nvm.bin"

int iolink_nvm_read(uint32_t offset, uint8_t *data, size_t len)
{
    FILE *f = fopen(NVM_FILE, "rb");
    if (!f) return -1;
    
    if (fseek(f, offset, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    
    size_t read = fread(data, 1, len, f);
    fclose(f);
    
    return (read == len) ? 0 : -1;
}

int iolink_nvm_write(uint32_t offset, const uint8_t *data, size_t len)
{
    /* Use r+b to avoid truncating, or wb if new */
    FILE *f = fopen(NVM_FILE, "r+b");
    if (!f) {
        f = fopen(NVM_FILE, "wb");
        if (!f) return -1;
    }
    
    if (fseek(f, offset, SEEK_SET) != 0) {
        fclose(f);
        return -1;
    }
    
    size_t written = fwrite(data, 1, len, f);
    fclose(f);
    
    return (written == len) ? 0 : -1;
}
