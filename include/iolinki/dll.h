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

typedef struct {
    iolink_dll_state_t state;
    const iolink_phy_api_t *phy;
    uint32_t last_activity_ms;
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
