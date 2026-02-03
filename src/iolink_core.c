/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/iolink.h"
#include "iolinki/dll.h"
#include "iolinki/application.h"
#include "iolinki/data_storage.h"
#include "iolinki/params.h"
#include "iolinki/platform.h"
#include <string.h>

static iolink_dll_ctx_t g_dll_ctx;
static iolink_config_t g_config;

int iolink_init(const iolink_phy_api_t *phy, const iolink_config_t *config)
{
    if (phy == NULL) {
        return -1;
    }

    if (config != NULL) {
        memcpy(&g_config, config, sizeof(iolink_config_t));
    } else {
        /* Default config */
        memset(&g_config, 0, sizeof(iolink_config_t));
        g_config.m_seq_type = IOLINK_M_SEQ_TYPE_0;
        g_config.min_cycle_time = 0; /* Min */
    }

    if (phy->init) {
        int err = phy->init();
        if (err != 0) return err;
    }

    iolink_dll_init(&g_dll_ctx, phy);
    iolink_params_init();
    g_dll_ctx.m_seq_type = (uint8_t)g_config.m_seq_type;
    g_dll_ctx.pd_in_len = g_config.pd_in_len;
    g_dll_ctx.pd_out_len = g_config.pd_out_len;

    return 0;
}

void iolink_process(void)
{
    iolink_dll_process(&g_dll_ctx);
}

int iolink_pd_input_update(const uint8_t *data, size_t len, bool valid)
{
    if (len > sizeof(g_dll_ctx.pd_in)) return -1;
    
    iolink_critical_enter();
    memcpy(g_dll_ctx.pd_in, data, len);
    g_dll_ctx.pd_in_len = (uint8_t)len;
    g_dll_ctx.pd_valid = valid;
    iolink_critical_exit();
    
    return 0;
}

int iolink_pd_output_read(uint8_t *data, size_t len)
{
    iolink_critical_enter();
    uint8_t read_len = (len < g_dll_ctx.pd_out_len) ? (uint8_t)len : g_dll_ctx.pd_out_len;
    memcpy(data, g_dll_ctx.pd_out, read_len);
    iolink_critical_exit();
    
    return (int)read_len;
}
