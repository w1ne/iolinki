/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/params.h"
#include "iolinki/platform.h"
#include "iolinki/device_info.h"
#include <string.h>

#define PARAMS_NVM_MAGIC 0x494F4C31U /* "IOL1" */

typedef struct {
    uint32_t magic;
    char application_tag[33];
    /* Future: user parameters, etc. */
} iolink_params_nvm_t;

static iolink_params_nvm_t g_nvm_shadow;

/**
 * Weak defaults for NVM access (to be overridden by platform port)
 */
__attribute__((weak)) int iolink_nvm_read(uint32_t offset, uint8_t *data, size_t len)
{
    (void)offset;
    (void)data;
    (void)len;
    return -1; /* Not implemented */
}

__attribute__((weak)) int iolink_nvm_write(uint32_t offset, const uint8_t *data, size_t len)
{
    (void)offset;
    (void)data;
    (void)len;
    return -1; /* Not implemented */
}

void iolink_params_init(void)
{
    /* Try to load from NVM */
    if (iolink_nvm_read(0U, (uint8_t*)&g_nvm_shadow, sizeof(g_nvm_shadow)) == 0) {
        if (g_nvm_shadow.magic == PARAMS_NVM_MAGIC) {
            /* Sync with device info */
            (void)iolink_device_info_set_application_tag(
                g_nvm_shadow.application_tag,
                (uint8_t)strlen(g_nvm_shadow.application_tag)
            );
            return;
        }
    }

    /* Init default state */
    g_nvm_shadow.magic = PARAMS_NVM_MAGIC;
    const iolink_device_info_t *info = iolink_device_info_get();
    if ((info != NULL) && (info->application_tag != NULL)) {
        size_t copy_len = strlen(info->application_tag);
        if (copy_len > 32U) {
            copy_len = 32U;
        }
        (void)memcpy(g_nvm_shadow.application_tag, info->application_tag, copy_len);
        g_nvm_shadow.application_tag[copy_len] = '\0';
    } else {
        g_nvm_shadow.application_tag[0] = '\0';
    }
}

int iolink_params_get(uint16_t index, uint8_t subindex, uint8_t *buffer, size_t max_len)
{
    if (buffer == NULL) {
        return -1;
    }
    if ((index == 0x0018U) && (subindex == 0U)) {
        const iolink_device_info_t *info = iolink_device_info_get();
        if ((info != NULL) && (info->application_tag != NULL)) {
            size_t len = strlen(info->application_tag);
            if (len > max_len) {
                len = max_len;
            }
            (void)memcpy(buffer, info->application_tag, len);
            return (int)len;
        }
    }
    return -1;
}

int iolink_params_set(uint16_t index, uint8_t subindex, const uint8_t *data, size_t len, bool persist)
{
    if ((data == NULL) && (len > 0U)) {
        return -1;
    }
    if ((index == 0x0018U) && (subindex == 0U)) {
        if (iolink_device_info_set_application_tag((const char*)data, (uint8_t)len) == 0) {
            if (persist) {
                size_t copy_len = (len > 32U) ? 32U : len;
                if (copy_len > 0U) {
                    (void)memcpy(g_nvm_shadow.application_tag, data, copy_len);
                }
                g_nvm_shadow.application_tag[copy_len] = '\0';
                (void)iolink_nvm_write(0U, (uint8_t*)&g_nvm_shadow, sizeof(g_nvm_shadow));
            }
            return 0;
        }
    }
    return -1;
}
