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

typedef enum {
    IOLINK_DLL_STATE_STARTUP = 0,
    IOLINK_DLL_STATE_PREOPERATE,
    IOLINK_DLL_STATE_OPERATE
} iolink_dll_state_t;

#include "iolinki/events.h"
#include "iolinki/isdu.h"
#include "iolinki/data_storage.h"

/**
 * @brief Data Link Layer Context
 */
typedef struct {
    iolink_dll_state_t state;
    const iolink_phy_api_t *phy;
    uint32_t last_activity_ms;
    
    /* Configuration */
    uint8_t m_seq_type; /* iolink_m_seq_type_t */
    uint8_t pd_in_len;
    uint8_t pd_out_len;
    uint8_t od_len;     /* On-request Data length (1 or 2 bytes) */
    bool pd_valid; /* Input Data Validity */
    
    /* Variable PD Support (for Type 1_V and 2_V) */
    uint8_t pd_in_len_current;   /* Current PD_In length (2-32) */
    uint8_t pd_out_len_current;  /* Current PD_Out length (2-32) */
    uint8_t pd_in_len_max;       /* Maximum PD_In length */
    uint8_t pd_out_len_max;      /* Maximum PD_Out length */
    
    /* SIO Mode */
    iolink_phy_mode_t phy_mode;  /* Current PHY mode (SDCI/SIO) */
    
    /* Unified Frame Assembly */
    uint8_t frame_buf[48];
    uint8_t frame_index;
    uint8_t req_len;
    uint64_t last_frame_us;

    /* Process Data Buffers */
    uint8_t pd_in[IOLINK_PD_IN_MAX_SIZE];  /* Device -> Master */
    uint8_t pd_out[IOLINK_PD_OUT_MAX_SIZE]; /* Master -> Device */

    /* Error Counters & Statistics */
    uint32_t crc_errors;        /* CRC error count */
    uint32_t timeout_errors;    /* Timeout error count */
    uint32_t framing_errors;    /* Framing error count */
    uint8_t retry_count;        /* Current retry attempt */
    uint8_t max_retries;        /* Maximum retry attempts (default: 3) */
    
    /* Timing Statistics */
    uint64_t last_response_us;  /* Timestamp of last response */
    uint32_t response_time_us;  /* Last response time in microseconds */

    /* Sub-modules */
    iolink_events_ctx_t events;
    iolink_isdu_ctx_t isdu;
    iolink_ds_ctx_t ds;
} iolink_dll_ctx_t;

/**
 * @brief Initialize DLL context
 * @param ctx DLL context
 * @param phy PHY provider
 */
void iolink_dll_init(iolink_dll_ctx_t *ctx, const iolink_phy_api_t *phy);

/**
 * @brief Process DLL logic
 * @param ctx DLL context
 */
void iolink_dll_process(iolink_dll_ctx_t *ctx);

/**
 * Set current PD lengths for variable types (1_V, 2_V)
 */
int iolink_dll_set_pd_length(iolink_dll_ctx_t *ctx, uint8_t pd_in_len, uint8_t pd_out_len);

/**
 * Get current PD lengths
 */
void iolink_dll_get_pd_length(const iolink_dll_ctx_t *ctx, uint8_t *pd_in_len, uint8_t *pd_out_len);

/**
 * Set SIO mode (single-wire communication)
 */
int iolink_dll_set_sio_mode(iolink_dll_ctx_t *ctx);

/**
 * Set SDCI mode (separate TX/RX)
 */
int iolink_dll_set_sdci_mode(iolink_dll_ctx_t *ctx);

/**
 * Get current PHY mode
 */
iolink_phy_mode_t iolink_dll_get_phy_mode(const iolink_dll_ctx_t *ctx);

#endif // IOLINK_DLL_H
