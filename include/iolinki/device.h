/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_DEVICE_H
#define IOLINK_DEVICE_H

#include "iolinki/application.h"
#include "iolinki/data_storage.h"
#include "iolinki/device_info.h"
#include "iolinki/dll.h"
#include "iolinki/iolink.h"
#include "iolinki/params.h"
#include "iolinki/phy.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint64_t (*iolink_device_time_us_fn)(void* user);
typedef void (*iolink_device_lock_fn)(void* user);
typedef void (*iolink_device_unlock_fn)(void* user);

typedef struct
{
    void* user;
    int (*init)(void* user);
    void (*set_mode)(void* user, iolink_phy_mode_t mode);
    void (*set_baudrate)(void* user, iolink_baudrate_t baudrate);
    int (*send)(void* user, const uint8_t* data, size_t len);
    int (*recv_byte)(void* user, uint8_t* byte);
    int (*detect_wakeup)(void* user);
    void (*set_cq_line)(void* user, uint8_t state);
    int (*get_voltage_mv)(void* user);
    bool (*is_short_circuit)(void* user);
} iolink_device_phy_t;

typedef struct
{
    iolink_device_phy_t phy;
    iolink_config_t stack;
    const iolink_app_callbacks_t* app_callbacks;
    const iolink_device_info_t* device_info;
    const iolink_ds_storage_api_t* ds_storage;
    iolink_device_time_us_fn time_us;
    iolink_device_lock_fn lock;
    iolink_device_unlock_fn unlock;
    void* user;
} iolink_device_config_t;

typedef struct
{
    /* Private fields. Allocate this object directly, but use API functions. */
    iolink_dll_ctx_t dll;
    iolink_config_t stack_config;
    iolink_device_info_ctx_t device_info;
    iolink_params_ctx_t params;
    const iolink_device_config_t* config;
    iolink_reset_handler_t reset_handler;
    uint8_t direct_param_page2[16];
} iolink_device_ctx_t;

size_t iolink_device_ctx_size(void);
int iolink_device_init(iolink_device_ctx_t* ctx, const iolink_device_config_t* config);
void iolink_device_process(iolink_device_ctx_t* ctx);
int iolink_device_pd_input_update(iolink_device_ctx_t* ctx, const uint8_t* data, size_t len,
                                  bool valid);
int iolink_device_pd_output_read(iolink_device_ctx_t* ctx, uint8_t* data, size_t len);
void iolink_device_set_reset_handler(iolink_device_ctx_t* ctx, iolink_reset_handler_t handler);
iolink_events_ctx_t* iolink_device_get_events_ctx(iolink_device_ctx_t* ctx);
iolink_ds_ctx_t* iolink_device_get_ds_ctx(iolink_device_ctx_t* ctx);
iolink_dll_state_t iolink_device_get_state(const iolink_device_ctx_t* ctx);
iolink_phy_mode_t iolink_device_get_phy_mode(const iolink_device_ctx_t* ctx);
iolink_baudrate_t iolink_device_get_baudrate(const iolink_device_ctx_t* ctx);
void iolink_device_get_dll_stats(const iolink_device_ctx_t* ctx, iolink_dll_stats_t* out_stats);
void iolink_device_set_timing_enforcement(iolink_device_ctx_t* ctx, bool enable);
void iolink_device_set_t_ren_limit_us(iolink_device_ctx_t* ctx, uint32_t limit_us);
iolink_m_seq_type_t iolink_device_get_m_seq_type(const iolink_device_ctx_t* ctx);
uint8_t iolink_device_get_pd_in_len(const iolink_device_ctx_t* ctx);
uint8_t iolink_device_get_pd_out_len(const iolink_device_ctx_t* ctx);
int iolink_device_set_pd_length(iolink_device_ctx_t* ctx, uint8_t pd_in_len,
                                uint8_t pd_out_len);

#endif  // IOLINK_DEVICE_H
