/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file device.c
 * @brief Device application layer: top-level stack lifecycle and I/O.
 * @ingroup iolinki_device
 *
 * Wires together the DLL, ISDU, parameter, device-info and data-storage
 * sub-contexts, drives the per-cycle processing, and exposes process-data
 * exchange and status accessors to the application.
 */

#include "iolinki/device.h"
#include "iolinki/platform.h"
#include "iolinki/time_utils.h"
#include <string.h>

/** @brief DLL state-change callback: dispatches to the app startup/preoperate/operate hooks. */
static void device_state_cb(void* user, iolink_dll_state_t state)
{
    iolink_device_ctx_t* ctx = (iolink_device_ctx_t*) user;

    if ((ctx == NULL) || (ctx->config == NULL) || (ctx->config->app_callbacks == NULL)) {
        return;
    }

    switch (state) {
        case IOLINK_DLL_STATE_STARTUP:
            if (ctx->config->app_callbacks->on_startup != NULL) {
                ctx->config->app_callbacks->on_startup();
            }
            break;
        case IOLINK_DLL_STATE_PREOPERATE:
            if (ctx->config->app_callbacks->on_preoperate != NULL) {
                ctx->config->app_callbacks->on_preoperate();
            }
            break;
        case IOLINK_DLL_STATE_OPERATE:
            if (ctx->config->app_callbacks->on_operate != NULL) {
                ctx->config->app_callbacks->on_operate();
            }
            break;
        default:
            break;
    }
}

/** @brief Copy the user stack configuration into the DLL context and derive lengths/timing. */
static void device_apply_stack_config(iolink_device_ctx_t* ctx)
{
    ctx->dll.m_seq_type = (uint8_t) ctx->stack_config.m_seq_type;
    ctx->dll.pd_in_len = ctx->stack_config.pd_in_len;
    ctx->dll.pd_out_len = ctx->stack_config.pd_out_len;
    ctx->dll.min_cycle_time_us = (uint32_t) ctx->stack_config.min_cycle_time * 100U;
    ctx->dll.t_pd_delay_us = ctx->stack_config.t_pd_us;
    if (ctx->dll.t_pd_delay_us > 0U) {
        ctx->dll.t_pd_deadline_us = iolink_time_get_us() + (uint64_t) ctx->dll.t_pd_delay_us;
    }
    else {
        ctx->dll.t_pd_deadline_us = 0U;
    }

    if ((ctx->dll.m_seq_type == IOLINK_M_SEQ_TYPE_2_1) ||
        (ctx->dll.m_seq_type == IOLINK_M_SEQ_TYPE_2_2) ||
        (ctx->dll.m_seq_type == IOLINK_M_SEQ_TYPE_2_V)) {
        ctx->dll.od_len = 2U;
    }
    else {
        ctx->dll.od_len = 1U;
    }

    ctx->dll.pd_in_len_current = ctx->dll.pd_in_len;
    ctx->dll.pd_out_len_current = ctx->dll.pd_out_len;
    ctx->dll.pd_in_len_max = ctx->dll.pd_in_len;
    ctx->dll.pd_out_len_max = ctx->dll.pd_out_len;
}

size_t iolink_device_ctx_size(void)
{
    return sizeof(iolink_device_ctx_t);
}

int iolink_device_init(iolink_device_ctx_t* ctx, const iolink_device_config_t* config)
{
    if ((ctx == NULL) || (config == NULL)) {
        return -1;
    }

    (void) memset(ctx, 0, sizeof(*ctx));
    (void) memcpy(&ctx->stack_config, &config->stack, sizeof(ctx->stack_config));
    ctx->config = config;

    if (config->phy.init != NULL) {
        int err = config->phy.init(config->phy.user);
        if (err != 0) {
            return err;
        }
    }

    iolink_device_info_ctx_init(&ctx->device_info, config->device_info);
    iolink_params_ctx_init(&ctx->params, &ctx->device_info);
    iolink_dll_init(&ctx->dll, &config->phy);
    ctx->dll.isdu.direct_param_page2 = ctx->direct_param_page2;
    ctx->dll.isdu.params_ctx = &ctx->params;
    ctx->dll.state_cb = device_state_cb;
    ctx->dll.state_cb_user = ctx;
    if (config->ds_storage != NULL) {
        iolink_ds_init(&ctx->dll.ds, config->ds_storage);
        ctx->dll.isdu.ds_ctx = &ctx->dll.ds;
    }
    iolink_ds_bind_params(&ctx->dll.ds, &ctx->params);
    device_apply_stack_config(ctx);

    if ((config->app_callbacks != NULL) && (config->app_callbacks->on_startup != NULL)) {
        config->app_callbacks->on_startup();
    }

    return 0;
}

void iolink_device_process(iolink_device_ctx_t* ctx)
{
    if (ctx == NULL) {
        return;
    }

    iolink_dll_process(&ctx->dll);

    if (ctx->dll.isdu.reset_pending) {
        ctx->dll.isdu.reset_pending = false;
        if (ctx->reset_handler != NULL) {
            ctx->reset_handler(IOLINK_RESET_DEVICE);
        }
    }
    if (ctx->dll.isdu.app_reset_pending) {
        ctx->dll.isdu.app_reset_pending = false;
        if (ctx->reset_handler != NULL) {
            ctx->reset_handler(IOLINK_RESET_APPLICATION);
        }
    }

    if ((ctx->config != NULL) && (ctx->config->app_callbacks != NULL) &&
        (ctx->dll.state == IOLINK_DLL_STATE_OPERATE)) {
        if (ctx->config->app_callbacks->on_pd_output != NULL) {
            ctx->config->app_callbacks->on_pd_output(ctx->dll.pd_out, ctx->dll.pd_out_len_current);
        }
        if (ctx->config->app_callbacks->on_pd_input != NULL) {
            ctx->config->app_callbacks->on_pd_input(ctx->dll.pd_in, ctx->dll.pd_in_len_current);
        }
    }
}

int iolink_device_pd_input_update(iolink_device_ctx_t* ctx, const uint8_t* data, size_t len,
                                  bool valid)
{
    if ((ctx == NULL) || (data == NULL) || (len > sizeof(ctx->dll.pd_in))) {
        return -1;
    }

    if ((ctx->config != NULL) && (ctx->config->lock != NULL)) {
        ctx->config->lock(ctx->config->user);
    }
    else {
        iolink_critical_enter();
    }

    (void) memcpy(ctx->dll.pd_in, data, len);
    ctx->dll.pd_in_len = (uint8_t) len;
    ctx->dll.pd_in_len_current = (uint8_t) len;
    ctx->dll.pd_valid = valid;
    ctx->dll.pd_in_toggle = !ctx->dll.pd_in_toggle;

    if ((ctx->config != NULL) && (ctx->config->unlock != NULL)) {
        ctx->config->unlock(ctx->config->user);
    }
    else {
        iolink_critical_exit();
    }

    return 0;
}

int iolink_device_pd_output_read(iolink_device_ctx_t* ctx, uint8_t* data, size_t len)
{
    uint8_t read_len;

    if ((ctx == NULL) || (data == NULL)) {
        return -1;
    }

    if ((ctx->config != NULL) && (ctx->config->lock != NULL)) {
        ctx->config->lock(ctx->config->user);
    }
    else {
        iolink_critical_enter();
    }

    read_len = (len < ctx->dll.pd_out_len_current) ? (uint8_t) len : ctx->dll.pd_out_len_current;
    (void) memcpy(data, ctx->dll.pd_out, read_len);

    if ((ctx->config != NULL) && (ctx->config->unlock != NULL)) {
        ctx->config->unlock(ctx->config->user);
    }
    else {
        iolink_critical_exit();
    }

    return (int) read_len;
}

void iolink_device_set_reset_handler(iolink_device_ctx_t* ctx, iolink_reset_handler_t handler)
{
    if (ctx != NULL) {
        ctx->reset_handler = handler;
    }
}

iolink_events_ctx_t* iolink_device_get_events_ctx(iolink_device_ctx_t* ctx)
{
    return (ctx != NULL) ? &ctx->dll.events : NULL;
}

iolink_ds_ctx_t* iolink_device_get_ds_ctx(iolink_device_ctx_t* ctx)
{
    return (ctx != NULL) ? &ctx->dll.ds : NULL;
}

iolink_dll_state_t iolink_device_get_state(const iolink_device_ctx_t* ctx)
{
    return (ctx != NULL) ? ctx->dll.state : IOLINK_DLL_STATE_STARTUP;
}

iolink_phy_mode_t iolink_device_get_phy_mode(const iolink_device_ctx_t* ctx)
{
    return (ctx != NULL) ? iolink_dll_get_phy_mode(&ctx->dll) : IOLINK_PHY_MODE_SIO;
}

iolink_baudrate_t iolink_device_get_baudrate(const iolink_device_ctx_t* ctx)
{
    return (ctx != NULL) ? iolink_dll_get_baudrate(&ctx->dll) : IOLINK_BAUDRATE_COM2;
}

void iolink_device_get_dll_stats(const iolink_device_ctx_t* ctx, iolink_dll_stats_t* out_stats)
{
    if (ctx != NULL) {
        iolink_dll_get_stats(&ctx->dll, out_stats);
    }
}

void iolink_device_set_timing_enforcement(iolink_device_ctx_t* ctx, bool enable)
{
    if (ctx != NULL) {
        iolink_dll_set_timing_enforcement(&ctx->dll, enable);
    }
}

void iolink_device_set_t_ren_limit_us(iolink_device_ctx_t* ctx, uint32_t limit_us)
{
    if (ctx != NULL) {
        iolink_dll_set_t_ren_limit_us(&ctx->dll, limit_us);
    }
}

iolink_m_seq_type_t iolink_device_get_m_seq_type(const iolink_device_ctx_t* ctx)
{
    return (ctx != NULL) ? (iolink_m_seq_type_t) ctx->dll.m_seq_type : IOLINK_M_SEQ_TYPE_0;
}

uint8_t iolink_device_get_pd_in_len(const iolink_device_ctx_t* ctx)
{
    uint8_t pd_in_len = 0U;

    if (ctx != NULL) {
        iolink_dll_get_pd_length(&ctx->dll, &pd_in_len, NULL);
    }

    return pd_in_len;
}

uint8_t iolink_device_get_pd_out_len(const iolink_device_ctx_t* ctx)
{
    uint8_t pd_out_len = 0U;

    if (ctx != NULL) {
        iolink_dll_get_pd_length(&ctx->dll, NULL, &pd_out_len);
    }

    return pd_out_len;
}

int iolink_device_set_pd_length(iolink_device_ctx_t* ctx, uint8_t pd_in_len, uint8_t pd_out_len)
{
    if (ctx == NULL) {
        return -1;
    }
    if ((ctx->dll.m_seq_type != IOLINK_M_SEQ_TYPE_1_V) &&
        (ctx->dll.m_seq_type != IOLINK_M_SEQ_TYPE_2_V)) {
        return -2;
    }
    if ((pd_in_len > ctx->dll.pd_in_len_max) || (pd_out_len > ctx->dll.pd_out_len_max)) {
        return -1;
    }
    return iolink_dll_set_pd_length(&ctx->dll, pd_in_len, pd_out_len);
}
