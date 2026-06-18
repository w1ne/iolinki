/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_H
#define IOLINK_H

#include <stdint.h>
#include <stdbool.h>
#include "iolinki/phy.h"
#include "iolinki/application.h"
#include "iolinki/dll.h"

/**
 * @file iolink.h
 * @brief Main header for iolinki IO-Link stack
 */

/**
 * @brief IO-Link M-sequence types
 *
 * Defines the frame structure and capabilities of the communication cycle.
 * See IO-Link Interface and System Specification V1.1.2, section 7.3.
 */
typedef enum
{
    IOLINK_M_SEQ_TYPE_0 = 0U,   /**< M-Sequence Type 0: On-request data (ISDU) only */
    IOLINK_M_SEQ_TYPE_1_1 = 1U, /**< M-Sequence Type 1_1: PD (fixed) + OD (1 byte) */
    IOLINK_M_SEQ_TYPE_1_2 = 2U, /**< M-Sequence Type 1_2: PD (fixed) + OD (1 byte) + ISDU */
    IOLINK_M_SEQ_TYPE_1_V = 3U, /**< M-Sequence Type 1_V: PD (variable) + OD (1 byte) */
    IOLINK_M_SEQ_TYPE_2_1 = 4U, /**< M-Sequence Type 2_1: PD (fixed) + OD (2 bytes) */
    IOLINK_M_SEQ_TYPE_2_2 = 5U, /**< M-Sequence Type 2_2: PD (fixed) + OD (2 bytes) + ISDU */
    IOLINK_M_SEQ_TYPE_2_V = 6U, /**< M-Sequence Type 2_V: PD (variable) + OD (2 bytes) + ISDU */
} iolink_m_seq_type_t;

/**
 * @brief IO-Link stack configuration
 *
 * Holds parameters required to initialize the stack behavior.
 */
typedef struct
{
    iolink_m_seq_type_t m_seq_type; /**< Primary M-sequence type supported by device */
    uint8_t min_cycle_time;         /**< Minimum cycle time in 0.1ms units (e.g. 20 = 2.0ms) */
    uint8_t pd_in_len;              /**< Process Data Input length (Device to Master) in bytes */
    uint8_t pd_out_len;             /**< Process Data Output length (Master to Device) in bytes */
    uint32_t t_pd_us;               /**< Power-on delay (t_pd) in microseconds */
} iolink_config_t;

/**
 * @brief Initialize the IO-Link stack
 *
 * Configures the internal state machine, ISDU engine, and PHY interface.
 *
 * Lifetime contract: the config is copied internally, but the PHY is retained
 * BY POINTER (the stack stores `phy`, not a copy). The `iolink_phy_api_t` must
 * outlive the stack — pass a pointer to storage with at least the stack's
 * lifetime, never an automatic/stack temporary (it compiles but dangles on the
 * next iolink_process()).
 *
 * @param phy Pointer to the PHY implementation API (retained by pointer)
 * @param config Pointer to stack configuration (copied internally)
 * @return int 0 on success, negative error code (e.g. -1 for NULL PHY)
 */
int iolink_init(const iolink_phy_api_t* phy, const iolink_config_t* config);

/**
 * @brief Process the IO-Link stack logic
 *
 * Main execution entry point. Handles PHY byte collection, state transitions,
 * frame assembly, and response generation. Must be called in the main loop.
 */
void iolink_process(void);

/**
 * @brief Reset request type delivered to the application reset handler.
 */
typedef enum
{
    IOLINK_RESET_DEVICE = 0,     /**< System Command 0x80 (Device Reset) */
    IOLINK_RESET_APPLICATION = 1 /**< System Command 0x81 (Application Reset) */
} iolink_reset_type_t;

/**
 * @brief Application callback invoked when the Master requests a reset.
 *
 * @param type IOLINK_RESET_DEVICE or IOLINK_RESET_APPLICATION
 */
typedef void (*iolink_reset_handler_t)(iolink_reset_type_t type);

/**
 * @brief Register an optional handler for Master-issued reset System Commands.
 *
 * The stack records reset requests (0x80/0x81) during ISDU processing and
 * dispatches them to @p handler from within iolink_process(), then clears the
 * pending flag. If no handler is registered the request is acknowledged and
 * discarded (the stack cannot reboot the host on its own).
 *
 * @param handler Callback, or NULL to clear.
 */
void iolink_set_reset_handler(iolink_reset_handler_t handler);

#include "iolinki/events.h"
#include "iolinki/data_storage.h"

/**
 * @brief Get the events context of the stack
 *
 * Used to trigger or check for pending diagnostic events.
 *
 * @return iolink_events_ctx_t* Pointer to the internal events context
 */
iolink_events_ctx_t* iolink_get_events_ctx(void);

/**
 * @brief Get the data storage context of the stack
 *
 * Used to manage Data Storage (DS) upload/download and synchronization.
 *
 * @return iolink_ds_ctx_t* Pointer to the internal DS context
 */
iolink_ds_ctx_t* iolink_get_ds_ctx(void);

/**
 * @brief Get current DLL state
 *
 * @return iolink_dll_state_t Current state
 */
iolink_dll_state_t iolink_get_state(void);

/**
 * @brief Get current PHY mode
 *
 * @return iolink_phy_mode_t Current mode
 */
iolink_phy_mode_t iolink_get_phy_mode(void);

/**
 * @brief Get current baudrate
 *
 * @return iolink_baudrate_t Current baudrate
 */
iolink_baudrate_t iolink_get_baudrate(void);

/**
 * @brief Get DLL statistics snapshot
 *
 * @param out_stats Output stats structure
 */
void iolink_get_dll_stats(iolink_dll_stats_t* out_stats);

/**
 * @brief Enable/disable timing enforcement (t_ren / t_cycle)
 *
 * @param enable true to enable, false to disable
 */
void iolink_set_timing_enforcement(bool enable);

/**
 * @brief Override t_ren limit (applies to all baudrates)
 *
 * @param limit_us New t_ren limit in microseconds (0 disables enforcement)
 */
void iolink_set_t_ren_limit_us(uint32_t limit_us);

/**
 * @brief Get configured M-sequence type
 *
 * @return iolink_m_seq_type_t Current M-sequence type
 */
iolink_m_seq_type_t iolink_get_m_seq_type(void);

/**
 * @brief Get the current PD In length
 *
 * Returns the runtime length (equal to the configured value for fixed
 * M-sequence types; tracks iolink_set_pd_length() for variable types).
 *
 * @return uint8_t PD In length in bytes
 */
uint8_t iolink_get_pd_in_len(void);

/**
 * @brief Get the current PD Out length
 *
 * Returns the runtime length (equal to the configured value for fixed
 * M-sequence types; tracks iolink_set_pd_length() for variable types).
 *
 * @return uint8_t PD Out length in bytes
 */
uint8_t iolink_get_pd_out_len(void);

/**
 * @brief Set the runtime Process Data lengths for variable-length M-sequences.
 *
 * Only valid for variable PD M-sequence types (TYPE_1_V / TYPE_2_V). The new
 * lengths take effect on the next cyclic exchange and are reflected in the PD
 * descriptors (index 0x001D and Direct Parameter page 1), so a Master can
 * re-read them. Lengths are clamped to the maximum configured at init.
 *
 * @param pd_in_len  New input PD length in bytes (Device -> Master)
 * @param pd_out_len New output PD length in bytes (Master -> Device)
 * @return int 0 on success, -1 if a length exceeds the configured maximum,
 *         -2 if the configured M-sequence type is not variable.
 */
int iolink_set_pd_length(uint8_t pd_in_len, uint8_t pd_out_len);

#endif  // IOLINK_H
