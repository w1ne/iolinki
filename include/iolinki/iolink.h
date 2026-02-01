#ifndef IOLINK_H
#define IOLINK_H

#include <stdint.h>
#include "iolinki/phy.h"
#include "iolinki/application.h"

/**
 * @file iolink.h
 * @brief Main header for iolinki IO-Link stack
 */

/**
 * @brief Initialize the IO-Link stack
 * 
 * @param phy Pointer to the PHY implementation to use
 * @return int 0 on success, negative error code otherwise
 */
int iolink_init(const iolink_phy_api_t *phy);

/**
 * @brief Process the IO-Link stack logic
 * 
 * This must be called periodically (e.g. every 1ms).
 */
void iolink_process(void);

#endif // IOLINK_H
