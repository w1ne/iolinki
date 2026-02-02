#ifndef IOLINK_DLL_H
#define IOLINK_DLL_H

#include <stdint.h>
#include <stdbool.h>
#include "iolinki/phy.h"

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
    
    /* Unified Frame Assembly */
    uint8_t frame_buf[48];
    uint8_t frame_index;
    uint8_t req_len;
    uint64_t last_frame_us;

    /* Process Data Buffers */
    uint8_t pd_in[32];  /* Device -> Master */
    uint8_t pd_out[32]; /* Master -> Device */

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

#endif // IOLINK_DLL_H
