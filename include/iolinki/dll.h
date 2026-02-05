/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_DLL_H
#define IOLINK_DLL_H

#include <stdint.h>
#include <stdbool.h>
#include "iolinki/phy.h"
#include "iolinki/config.h"

/**
 * @file dll.h
 * @brief IO-Link Data Link Layer (DLL) Implementation
 */

/**
 * @brief IO-Link DLL State Machine states
 */
typedef enum
{
    IOLINK_DLL_STATE_STARTUP = 0U,       /**< Waiting for wake-up request */
    IOLINK_DLL_STATE_AWAITING_COMM = 1U, /**< Wake-up detected, waiting for first frame */
    IOLINK_DLL_STATE_PREOPERATE = 2U,    /**< Handling ISDU (Type 0) */
    IOLINK_DLL_STATE_ESTAB_COM = 3U,     /**< Establish communication (transition to OPERATE) */
    IOLINK_DLL_STATE_OPERATE = 4U,       /**< Cyclic data exchange active */
    IOLINK_DLL_STATE_FALLBACK = 5U       /**< Error recovery / fallback */
} iolink_dll_state_t;

#include "iolinki/events.h"
#include "iolinki/isdu.h"
#include "iolinki/data_storage.h"

/**
 * @brief Data Link Layer Context
 *
 * Internal state and data storage for the DLL engine.
 */
typedef struct
{
    iolink_dll_state_t state;    /**< Current DLL state */
    const iolink_phy_api_t *phy; /**< Bound PHY API implementation */
    uint32_t last_activity_ms;   /**< Timestamp of last valid frame */
    bool wakeup_seen;            /**< Wake-up detected (if PHY supports it) */

    /* Configuration */
    uint8_t m_seq_type;         /**< Supported M-sequence type */
    uint8_t pd_in_len;          /**< Input Process Data length */
    uint8_t pd_out_len;         /**< Output Process Data length */
    uint8_t od_len;             /**< On-request Data length (1 or 2 bytes) */
    bool pd_valid;              /**< Current PD_In validity status */
    bool pd_in_toggle;          /**< Toggle bit for PD_In consistency */
    uint32_t min_cycle_time_us; /**< Minimum cycle time in microseconds */
    bool enforce_timing;        /**< Enable timing checks (t_ren / t_cycle) */
    uint32_t t_ren_limit_us;    /**< Current t_ren limit in microseconds */
    bool t_ren_override;        /**< Use overridden t_ren limit if true */
    uint32_t t_pd_delay_us;     /**< Power-on delay (t_pd) in microseconds */

    /* Variable PD Support (for Type 1_V and 2_V) */
    uint8_t pd_in_len_current;  /**< Current runtime PD_In length */
    uint8_t pd_out_len_current; /**< Current runtime PD_Out length */
    uint8_t pd_in_len_max;      /**< Maximum allowed PD_In length */
    uint8_t pd_out_len_max;     /**< Maximum allowed PD_Out length */

    /* SIO Mode */
    iolink_phy_mode_t phy_mode; /**< Current operating mode (SDCI vs SIO) */

    /* Baudrate Support */
    iolink_baudrate_t baudrate; /**< Negotiated baudrate (COM1-COM3) */

    /* Unified Frame Assembly */
    uint8_t frame_buf[48];        /**< Raw frame assembly buffer */
    uint8_t frame_index;          /**< Current byte index in assembly */
    uint8_t req_len;              /**< Expected length of current frame type */
    uint64_t last_frame_us;       /**< Microsecond timestamp of last frame start */
    uint64_t last_byte_us;        /**< Microsecond timestamp of last received byte */
    uint64_t last_cycle_start_us; /**< Microsecond timestamp of last cycle start */
    uint32_t t_byte_limit_us;     /**< Inter-byte timeout limit in microseconds */
    uint64_t wakeup_deadline_us;  /**< Earliest time to accept frames after wake-up */
    uint64_t t_pd_deadline_us;    /**< Earliest time to accept frames after power-on */

    /* Process Data Buffers */
    uint8_t pd_in[IOLINK_PD_IN_MAX_SIZE];   /**< Input PD buffer (Device -> Master) */
    uint8_t pd_out[IOLINK_PD_OUT_MAX_SIZE]; /**< Output PD buffer (Master -> Device) */

    /* Error Counters & Statistics */
    uint32_t crc_errors;            /**< Cumulative CRC error count */
    uint32_t timeout_errors;        /**< Cumulative timeout count */
    uint32_t framing_errors;        /**< Cumulative framing error count */
    uint32_t timing_errors;         /**< Cumulative timing violations */
    uint32_t t_ren_violations;      /**< t_ren violations */
    uint32_t t_cycle_violations;    /**< t_cycle violations */
    uint32_t t_byte_violations;     /**< Inter-byte timing violations */
    uint32_t t_pd_violations;       /**< t_pd violations */
    uint8_t retry_count;            /**< Retry counter for current exchange */
    uint32_t total_retries;         /**< Cumulative retry count */
    uint8_t max_retries;            /**< Configured max retries (default 3) */
    uint32_t voltage_faults;        /**< Cumulative voltage fault count */
    uint32_t short_circuits;        /**< Cumulative short circuit count */
    uint8_t fallback_count;         /**< Consecutive fallback count for SIO transition */
    uint8_t sio_fallback_threshold; /**< Fallback threshold to enter SIO mode (default 3) */

    /* Timing Statistics */
    uint64_t last_response_us; /**< Microsecond timestamp of last response */
    uint32_t response_time_us; /**< Measured stack response time (t2) */

    /* Sub-modules */
    iolink_events_ctx_t events; /**< Diagnostic Events engine */
    iolink_isdu_ctx_t isdu;     /**< ISDU Service engine */
    iolink_ds_ctx_t ds;         /**< Data Storage engine */
} iolink_dll_ctx_t;

/**
 * @brief DLL statistics snapshot
 */
typedef struct
{
    uint32_t crc_errors;
    uint32_t timeout_errors;
    uint32_t framing_errors;
    uint32_t timing_errors;
    uint32_t t_ren_violations;
    uint32_t t_cycle_violations;
    uint32_t t_byte_violations;
    uint32_t t_pd_violations;
    uint32_t total_retries;
    uint32_t voltage_faults;
    uint32_t short_circuits;
} iolink_dll_stats_t;

/**
 * @brief Initialize DLL context
 *
 * Sets defaults for state, retries, and resets counters.
 *
 * @param ctx DLL context to initialize
 * @param phy PHY implementation to bind
 */
void iolink_dll_init(iolink_dll_ctx_t *ctx, const iolink_phy_api_t *phy);

/**
 * @brief Process DLL logic
 *
 * Handles byte-level processing and state machine transitions.
 *
 * @param ctx DLL context to process
 */
void iolink_dll_process(iolink_dll_ctx_t *ctx);

/**
 * @brief Set current PD lengths for variable types (1_V, 2_V)
 *
 * @param ctx DLL context
 * @param pd_in_len New PD_In length
 * @param pd_out_len New PD_Out length
 * @return int 0 on success, negative on range error
 */
int iolink_dll_set_pd_length(iolink_dll_ctx_t *ctx, uint8_t pd_in_len, uint8_t pd_out_len);

/**
 * @brief Get current PD lengths
 *
 * @param ctx DLL context
 * @param pd_in_len [out] Current PD_In length
 * @param pd_out_len [out] Current PD_Out length
 */
void iolink_dll_get_pd_length(const iolink_dll_ctx_t *ctx, uint8_t *pd_in_len, uint8_t *pd_out_len);

/**
 * @brief Request transition to SIO mode (single-wire communication)
 *
 * @param ctx DLL context
 * @return int 0 on success
 */
int iolink_dll_set_sio_mode(iolink_dll_ctx_t *ctx);

/**
 * @brief Request transition to SDCI mode (UART-based exchange)
 *
 * @param ctx DLL context
 * @return int 0 on success
 */
int iolink_dll_set_sdci_mode(iolink_dll_ctx_t *ctx);

/**
 * @brief Get current operating mode
 *
 * @param ctx DLL context
 * @return iolink_phy_mode_t Current mode
 */
iolink_phy_mode_t iolink_dll_get_phy_mode(const iolink_dll_ctx_t *ctx);

/**
 * @brief Set the communication baudrate
 *
 * @param ctx DLL context
 * @param baudrate Desired baudrate (COM1, COM2, or COM3)
 * @return int 0 on success
 */
int iolink_dll_set_baudrate(iolink_dll_ctx_t *ctx, iolink_baudrate_t baudrate);

/**
 * @brief Get current negotiated baudrate
 *
 * @param ctx DLL context
 * @return iolink_baudrate_t Current baudrate
 */
iolink_baudrate_t iolink_dll_get_baudrate(const iolink_dll_ctx_t *ctx);

/**
 * @brief Get DLL statistics
 *
 * @param ctx DLL context
 * @param out_stats Output stats structure
 */
void iolink_dll_get_stats(const iolink_dll_ctx_t *ctx, iolink_dll_stats_t *out_stats);

/**
 * @brief Enable/disable timing enforcement (t_ren / t_cycle)
 *
 * @param ctx DLL context
 * @param enable true to enable, false to disable
 */
void iolink_dll_set_timing_enforcement(iolink_dll_ctx_t *ctx, bool enable);

/**
 * @brief Override t_ren limit (applies to all baudrates)
 *
 * @param ctx DLL context
 * @param limit_us New t_ren limit in microseconds (0 disables enforcement)
 */
void iolink_dll_set_t_ren_limit_us(iolink_dll_ctx_t *ctx, uint32_t limit_us);

#endif  // IOLINK_DLL_H
