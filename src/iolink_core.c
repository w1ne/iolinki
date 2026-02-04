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
        (void)memcpy(&g_config, config, sizeof(iolink_config_t));
    } else {
        /* Default config */
        (void)memset(&g_config, 0, sizeof(iolink_config_t));
        g_config.m_seq_type = IOLINK_M_SEQ_TYPE_0;
        g_config.min_cycle_time = 0U; /* Min */
    }

    if (phy->init != NULL) {
        int err = phy->init();
        if (err != 0) {
            return err;
        }
    }

    iolink_dll_init(&g_dll_ctx, phy);
    iolink_params_init();
    g_dll_ctx.m_seq_type = (uint8_t)g_config.m_seq_type;
    g_dll_ctx.pd_in_len = g_config.pd_in_len;
    g_dll_ctx.pd_out_len = g_config.pd_out_len;

    /* Apply config-dependent DLL fields (must run after m_seq_type is set) */
    if ((g_dll_ctx.m_seq_type == IOLINK_M_SEQ_TYPE_2_1) ||
        g_dll_ctx.m_seq_type == IOLINK_M_SEQ_TYPE_2_2 ||
        g_dll_ctx.m_seq_type == IOLINK_M_SEQ_TYPE_2_V) {
        g_dll_ctx.od_len = 2U;
    } else {
        g_dll_ctx.od_len = 1U;
    }

    if ((g_dll_ctx.m_seq_type == IOLINK_M_SEQ_TYPE_1_V) ||
        g_dll_ctx.m_seq_type == IOLINK_M_SEQ_TYPE_2_V) {
        g_dll_ctx.pd_in_len_current = g_dll_ctx.pd_in_len;
        g_dll_ctx.pd_out_len_current = g_dll_ctx.pd_out_len;
        g_dll_ctx.pd_in_len_max = g_dll_ctx.pd_in_len;
        g_dll_ctx.pd_out_len_max = g_dll_ctx.pd_out_len;
    } else {
        g_dll_ctx.pd_in_len_current = g_dll_ctx.pd_in_len;
        g_dll_ctx.pd_out_len_current = g_dll_ctx.pd_out_len;
        g_dll_ctx.pd_in_len_max = g_dll_ctx.pd_in_len;
        g_dll_ctx.pd_out_len_max = g_dll_ctx.pd_out_len;
    }

    return 0;
}

void iolink_process(void)
{
    iolink_dll_process(&g_dll_ctx);
}

int iolink_pd_input_update(const uint8_t *data, size_t len, bool valid)
{
    if (data == NULL) {
        return -1;
    }
    if (len > sizeof(g_dll_ctx.pd_in)) {
        return -1;
    }
    
    iolink_critical_enter();
    (void)memcpy(g_dll_ctx.pd_in, data, len);
    g_dll_ctx.pd_in_len = (uint8_t)len;
    g_dll_ctx.pd_valid = valid;
    iolink_critical_exit();
    
    return 0;
}

int iolink_pd_output_read(uint8_t *data, size_t len)
{
    if (data == NULL) {
        return -1;
    }

    iolink_critical_enter();
    uint8_t read_len = (len < g_dll_ctx.pd_out_len) ? (uint8_t)len : g_dll_ctx.pd_out_len;
    (void)memcpy(data, g_dll_ctx.pd_out, read_len);
    iolink_critical_exit();
    
    return (int)read_len;
}

iolink_events_ctx_t* iolink_get_events_ctx(void)
{
    return &g_dll_ctx.events;
}

iolink_ds_ctx_t* iolink_get_ds_ctx(void)
{
    return &g_dll_ctx.ds;
}
