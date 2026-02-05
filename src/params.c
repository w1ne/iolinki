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
#include "iolinki/utils.h"
#include <string.h>

#define PARAMS_NVM_MAGIC 0x494F4C31U /* "IOL1" */

typedef struct
{
    uint32_t magic;
    char application_tag[33];
    char function_tag[33];
    char location_tag[33];
    /* Future: user parameters, etc. */
} iolink_params_nvm_t;

static iolink_params_nvm_t g_nvm_shadow;

void iolink_params_init(void)
{
    /* Try to load from NVM */
    if (iolink_nvm_read(0U, (uint8_t *) &g_nvm_shadow, sizeof(g_nvm_shadow)) == 0) {
        if (g_nvm_shadow.magic == PARAMS_NVM_MAGIC) {
            /* Sync with device info */
            (void) iolink_device_info_set_application_tag(
                g_nvm_shadow.application_tag, (uint8_t) strlen(g_nvm_shadow.application_tag));
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
        (void) memcpy(g_nvm_shadow.application_tag, info->application_tag, copy_len);
        g_nvm_shadow.application_tag[copy_len] = '\0';
    }
    else {
        g_nvm_shadow.application_tag[0] = '\0';
    }
    g_nvm_shadow.function_tag[0] = '\0';
    g_nvm_shadow.location_tag[0] = '\0';
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
            (void) memcpy(buffer, info->application_tag, len);
            return (int) len;
        }
    }
    if ((index == 0x0019U) && (subindex == 0U)) {
        size_t len = strlen(g_nvm_shadow.function_tag);
        if (len > max_len) {
            len = max_len;
        }
        (void) memcpy(buffer, g_nvm_shadow.function_tag, len);
        return (int) len;
    }
    if ((index == 0x001AU) && (subindex == 0U)) {
        size_t len = strlen(g_nvm_shadow.location_tag);
        if (len > max_len) {
            len = max_len;
        }
        (void) memcpy(buffer, g_nvm_shadow.location_tag, len);
        return (int) len;
    }
    return -1;
}

int iolink_params_set(uint16_t index, uint8_t subindex, const uint8_t *data, size_t len,
                      bool persist)
{
    if (!iolink_buf_is_valid(data, len)) {
        return -1;
    }
    if ((index == 0x0018U) && (subindex == 0U)) {
        if (iolink_device_info_set_application_tag((const char *) data, (uint8_t) len) == 0) {
            if (persist) {
                size_t copy_len = (len > 32U) ? 32U : len;
                if (copy_len > 0U) {
                    (void) memcpy(g_nvm_shadow.application_tag, data, copy_len);
                }
                g_nvm_shadow.application_tag[copy_len] = '\0';
                (void) iolink_nvm_write(0U, (uint8_t *) &g_nvm_shadow, sizeof(g_nvm_shadow));
            }
            return 0;
        }
    }
    if ((index == 0x0019U) && (subindex == 0U)) {
        size_t copy_len = (len > 32U) ? 32U : len;
        if (copy_len > 0U) {
            (void) memcpy(g_nvm_shadow.function_tag, data, copy_len);
        }
        g_nvm_shadow.function_tag[copy_len] = '\0';
        if (persist) {
            (void) iolink_nvm_write(0U, (uint8_t *) &g_nvm_shadow, sizeof(g_nvm_shadow));
        }
        return 0;
    }
    if ((index == 0x001AU) && (subindex == 0U)) {
        size_t copy_len = (len > 32U) ? 32U : len;
        if (copy_len > 0U) {
            (void) memcpy(g_nvm_shadow.location_tag, data, copy_len);
        }
        g_nvm_shadow.location_tag[copy_len] = '\0';
        if (persist) {
            (void) iolink_nvm_write(0U, (uint8_t *) &g_nvm_shadow, sizeof(g_nvm_shadow));
        }
        return 0;
    }
    return -1;
}

void iolink_params_factory_reset(void)
{
    /* Reset to factory defaults */
    g_nvm_shadow.magic = PARAMS_NVM_MAGIC;
    g_nvm_shadow.application_tag[0] = '\0';
    g_nvm_shadow.function_tag[0] = '\0';
    g_nvm_shadow.location_tag[0] = '\0';

    /* Clear device info application tag */
    (void) iolink_device_info_set_application_tag("", 0U);

    /* Erase NVM (write zeros or default structure) */
    (void) iolink_nvm_write(0U, (uint8_t *) &g_nvm_shadow, sizeof(g_nvm_shadow));
}
