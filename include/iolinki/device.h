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

/**
 * @file device.h
 * @brief IO-Link Device application layer API.
 *
 * Top-level entry point for an IO-Link Device. Ties together the physical
 * layer, data link layer, device identification, parameters and application
 * callbacks behind a single opaque context, and exposes the lifecycle,
 * process-data and introspection functions used by the application.
 */

/**
 * @defgroup iolinki_device Device Application Layer
 * @brief Top-level IO-Link Device context, lifecycle and process-data API.
 * @{
 */

/** @brief Callback returning a monotonic timestamp in microseconds.
 *  @param user Opaque user pointer supplied via ::iolink_device_config_t.
 *  @return Current time in microseconds. */
typedef uint64_t (*iolink_device_time_us_fn)(void* user);
/** @brief Callback acquiring the application-provided lock (critical section).
 *  @param user Opaque user pointer supplied via ::iolink_device_config_t. */
typedef void (*iolink_device_lock_fn)(void* user);
/** @brief Callback releasing the application-provided lock (critical section).
 *  @param user Opaque user pointer supplied via ::iolink_device_config_t. */
typedef void (*iolink_device_unlock_fn)(void* user);

/** @brief Physical layer API used by the device (alias of ::iolink_phy_api_t). */
typedef iolink_phy_api_t iolink_device_phy_t;

/**
 * @brief Device configuration supplied at initialization.
 *
 * Aggregates the PHY driver, stack settings, application hooks, device
 * identification, data-storage backend and platform integration callbacks.
 */
typedef struct
{
    iolink_device_phy_t phy;                        /**< Physical layer driver API. */
    iolink_config_t stack;                          /**< Stack behavior configuration. */
    const iolink_app_callbacks_t* app_callbacks;    /**< Application lifecycle/PD callbacks (may be NULL). */
    const iolink_device_info_t* device_info;        /**< Static device identification data. */
    const iolink_ds_storage_api_t* ds_storage;      /**< Data-storage persistence backend (may be NULL). */
    iolink_device_time_us_fn time_us;               /**< Microsecond timestamp source. */
    iolink_device_lock_fn lock;                     /**< Lock callback (may be NULL). */
    iolink_device_unlock_fn unlock;                 /**< Unlock callback (may be NULL). */
    void* user;                                     /**< Opaque pointer passed back to callbacks. */
} iolink_device_config_t;

/**
 * @brief Device runtime context.
 *
 * Allocate this object directly, but access it only through the API functions.
 * All fields are private stack state.
 */
typedef struct
{
    /* Private fields. Allocate this object directly, but use API functions. */
    iolink_dll_ctx_t dll;                        /**< Data link layer context. */
    iolink_config_t stack_config;                /**< Active stack configuration. */
    iolink_device_info_ctx_t device_info;        /**< Device identification context. */
    iolink_params_ctx_t params;                  /**< Parameter manager context. */
    const iolink_device_config_t* config;        /**< Pointer to the supplied configuration. */
    iolink_reset_handler_t reset_handler;        /**< Application reset handler (may be NULL). */
    uint8_t direct_param_page2[16];              /**< Direct Parameter Page 2 storage. */
} iolink_device_ctx_t;

/**
 * @brief Get the size in bytes of ::iolink_device_ctx_t.
 * @return Size of the device context object, in bytes.
 */
size_t iolink_device_ctx_size(void);

/**
 * @brief Initialize a device context from a configuration.
 * @param ctx     Device context to initialize.
 * @param config  Configuration to apply (must remain valid for the ctx lifetime).
 * @return 0 on success, negative on error.
 */
int iolink_device_init(iolink_device_ctx_t* ctx, const iolink_device_config_t* config);

/**
 * @brief Run one iteration of the device state machine.
 *
 * Drives the DLL/PHY: services pending frames, timing and application events.
 * Call this periodically from the main loop.
 * @param ctx Device context.
 */
void iolink_device_process(iolink_device_ctx_t* ctx);

/**
 * @brief Update the input process data (Device -> Master).
 * @param ctx    Device context.
 * @param data   Input process data bytes to publish.
 * @param len    Length of @p data in bytes.
 * @param valid  Whether the process data is currently valid.
 * @return 0 on success, negative on error.
 */
int iolink_device_pd_input_update(iolink_device_ctx_t* ctx, const uint8_t* data, size_t len,
                                  bool valid);

/**
 * @brief Read the latest output process data (Master -> Device).
 * @param ctx        Device context.
 * @param[out] data  Buffer filled with the output process data.
 * @param len        Size of @p data in bytes.
 * @return 0 on success, negative on error.
 */
int iolink_device_pd_output_read(iolink_device_ctx_t* ctx, uint8_t* data, size_t len);

/**
 * @brief Register the handler invoked on Master reset commands.
 * @param ctx      Device context.
 * @param handler  Reset handler callback (may be NULL to clear).
 */
void iolink_device_set_reset_handler(iolink_device_ctx_t* ctx, iolink_reset_handler_t handler);

/**
 * @brief Get the event handling context.
 * @param ctx Device context.
 * @return Pointer to the device's events context.
 */
iolink_events_ctx_t* iolink_device_get_events_ctx(iolink_device_ctx_t* ctx);

/**
 * @brief Get the data-storage context.
 * @param ctx Device context.
 * @return Pointer to the device's data-storage context.
 */
iolink_ds_ctx_t* iolink_device_get_ds_ctx(iolink_device_ctx_t* ctx);

/**
 * @brief Get the current DLL state.
 * @param ctx Device context.
 * @return Current data link layer state.
 */
iolink_dll_state_t iolink_device_get_state(const iolink_device_ctx_t* ctx);

/**
 * @brief Get the current PHY communication mode.
 * @param ctx Device context.
 * @return Current physical layer mode.
 */
iolink_phy_mode_t iolink_device_get_phy_mode(const iolink_device_ctx_t* ctx);

/**
 * @brief Get the current communication baud rate.
 * @param ctx Device context.
 * @return Active baud rate.
 */
iolink_baudrate_t iolink_device_get_baudrate(const iolink_device_ctx_t* ctx);

/**
 * @brief Copy the DLL statistics counters.
 * @param ctx             Device context.
 * @param[out] out_stats  Filled with the current DLL statistics.
 */
void iolink_device_get_dll_stats(const iolink_device_ctx_t* ctx, iolink_dll_stats_t* out_stats);

/**
 * @brief Enable or disable timing enforcement.
 * @param ctx     Device context.
 * @param enable  True to enforce timing limits, false to only measure.
 */
void iolink_device_set_timing_enforcement(iolink_device_ctx_t* ctx, bool enable);

/**
 * @brief Set the response-time (t_ren) limit.
 * @param ctx       Device context.
 * @param limit_us  Response-time limit in microseconds.
 */
void iolink_device_set_t_ren_limit_us(iolink_device_ctx_t* ctx, uint32_t limit_us);

/**
 * @brief Get the negotiated M-sequence type.
 * @param ctx Device context.
 * @return Active M-sequence type.
 */
iolink_m_seq_type_t iolink_device_get_m_seq_type(const iolink_device_ctx_t* ctx);

/**
 * @brief Get the input process data length.
 * @param ctx Device context.
 * @return Input process data length in bytes.
 */
uint8_t iolink_device_get_pd_in_len(const iolink_device_ctx_t* ctx);

/**
 * @brief Get the output process data length.
 * @param ctx Device context.
 * @return Output process data length in bytes.
 */
uint8_t iolink_device_get_pd_out_len(const iolink_device_ctx_t* ctx);

/**
 * @brief Set the input and output process data lengths.
 * @param ctx         Device context.
 * @param pd_in_len   Input process data length in bytes.
 * @param pd_out_len  Output process data length in bytes.
 * @return 0 on success, negative on error.
 */
int iolink_device_set_pd_length(iolink_device_ctx_t* ctx, uint8_t pd_in_len, uint8_t pd_out_len);

/** @} */ /* end of iolinki_device */

#endif  // IOLINK_DEVICE_H
