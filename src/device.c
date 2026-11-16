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

/** @brief SystemCommand reset service for the ISDU engine (see isdu.h). */
static int device_system_reset(void* user, uint8_t cmd, bool execute)
{
    iolink_device_ctx_t* ctx = (iolink_device_ctx_t*) user;

    if ((ctx == NULL) || (ctx->reset_handler == NULL)) {
        return -1;
    }
    if (execute) {
        if (cmd == IOLINK_CMD_APPLICATION_RESET) {
            ctx->reset_handler(IOLINK_RESET_APPLICATION);
        }
        else if (cmd == IOLINK_CMD_RESTORE_FACTORY_SETTINGS) {
            ctx->reset_handler(IOLINK_RESET_RESTORE_FACTORY);
        }
        else {
            /* 0x80 and 0x83 run deferred in device_service_deferred_reset(). */
        }
    }
    return 0;
}

/**
 * @brief Run an accepted Device reset (0x80) or Back-to-box (0x83).
 *
 * 10.7.2/10.7.5: the response is sent first and the Device waits at least
 * 3 MasterCycles after the last ISDU response before communication stops.
 * The ISDU transport is Idle once the Master has read the whole response.
 */
static void device_service_deferred_reset(iolink_device_ctx_t* ctx)
{
    iolink_isdu_ctx_t* isdu = &ctx->dll.isdu;

    if (!isdu->reset_pending && !isdu->back_to_box_pending) {
        ctx->deferred_reset_armed = false;
        return;
    }
    const bool link_lost = (ctx->dll.state == IOLINK_DLL_STATE_STARTUP);
    if (!link_lost) {
        if (isdu->state != ISDU_STATE_IDLE) {
            return; /* Response not yet read by the Master */
        }
        if (!ctx->deferred_reset_armed) {
            ctx->deferred_reset_armed = true;
            ctx->deferred_reset_mseq = ctx->dll.mseq_count;
            return;
        }
        if ((uint32_t) (ctx->dll.mseq_count - ctx->deferred_reset_mseq) < 3U) {
            return;
        }
    }

    ctx->deferred_reset_armed = false;
    if (isdu->back_to_box_pending) {
        isdu->back_to_box_pending = false;
        isdu->reset_pending = false;
        if (ctx->reset_handler != NULL) {
            ctx->reset_handler(IOLINK_RESET_BACK_TO_BOX);
        }
        iolink_dll_disable_communication(&ctx->dll);
        return;
    }
    isdu->reset_pending = false;
    if (ctx->reset_handler != NULL) {
        ctx->reset_handler(IOLINK_RESET_DEVICE);
    }
}

/** @brief Copy the user stack configuration into the DLL context and derive lengths/timing. */
static void device_apply_stack_config(iolink_device_ctx_t* ctx)
{
    ctx->dll.m_seq_type = (uint8_t) ctx->stack_config.m_seq_type;
    ctx->dll.pd_in_len = ctx->stack_config.pd_in_len;
    ctx->dll.pd_out_len = ctx->stack_config.pd_out_len;
    ctx->dll.min_cycle_time_us = (uint32_t) ctx->stack_config.min_cycle_time * 100U;
    if (ctx->stack_config.min_cycle_time != 0U) {
        /* Advertise the explicit stack limit through this device's DPP identity. */
        ctx->device_info.defaults.min_cycle_time = ctx->stack_config.min_cycle_time;
    }
    ctx->dll.t_pd_delay_us = ctx->stack_config.t_pd_us;
    if (ctx->dll.t_pd_delay_us > 0U) {
        ctx->dll.t_pd_deadline_us = iolink_time_get_us() + (uint64_t) ctx->dll.t_pd_delay_us;
    }
    else {
        ctx->dll.t_pd_deadline_us = 0U;
    }

    /* Table A.10: TYPE_1_2 carries two OD octets, TYPE_1_V eight, and
       TYPE_2_V the two-octet variant; the remaining types carry one. */
    const uint8_t configured_od = ctx->stack_config.od_len;
    ctx->dll.od_len = configured_od != 0U ? configured_od : iolink_mseq_od_len(ctx->dll.m_seq_type);

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

    if ((config->stack.pd_in_len > IOLINK_PD_IN_MAX_SIZE) ||
        (config->stack.pd_out_len > IOLINK_PD_OUT_MAX_SIZE)) {
        return -1;
    }
    if (config->stack.od_len != 0U) {
        const uint8_t width = config->stack.od_len;
        if (config->stack.m_seq_type == IOLINK_M_SEQ_TYPE_2_V) {
            if ((width != 1U) && (width != 2U)) return -1;
            if ((config->stack.pd_in_len == 0U) && (config->stack.pd_out_len == 0U)) {
                return -1;
            }
            /* This OD1 variant advertises code4, not the short-PD code0 form. */
            if ((width == 1U) && (config->stack.pd_in_len < 3U) &&
                (config->stack.pd_out_len < 3U)) {
                return -1;
            }
        }
        else if (width != iolink_mseq_od_len((uint8_t) config->stack.m_seq_type)) {
            return -1;
        }
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
    ctx->dll.isdu.vendor_service = config->vendor_service;
    ctx->dll.isdu.vendor_user = config->user;
    ctx->dll.isdu.system_reset = device_system_reset;
    ctx->dll.isdu.system_reset_user = ctx;
    ctx->dll.state_cb = device_state_cb;
    ctx->dll.state_cb_user = ctx;
    ctx->dll.pd_input_select = config->pd_input_select;
    ctx->dll.pd_input_select_user = config->user;
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

    /* Application reset already ran before its ISDU response. */
    ctx->dll.isdu.app_reset_pending = false;
    device_service_deferred_reset(ctx);

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
    int ret;

    if ((ctx == NULL) || (data == NULL) || (len > sizeof(ctx->dll.pd_in))) {
        return -1;
    }

    if ((ctx->config != NULL) && (ctx->config->lock != NULL)) {
        ctx->config->lock(ctx->config->user);
    }
    else {
        iolink_critical_enter();
    }

    /* Valid data also becomes the ProcessDataInput (0x0028) snapshot. */
    ret = iolink_dll_publish_pd_in(&ctx->dll, data, (uint8_t) len, valid);

    if ((ctx->config != NULL) && (ctx->config->unlock != NULL)) {
        ctx->config->unlock(ctx->config->user);
    }
    else {
        iolink_critical_exit();
    }

    return ret;
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
