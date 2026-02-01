#ifndef IOLINK_H
#define IOLINK_H

#include <stdint.h>
#include "iolinki/phy.h"

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

#endif // IOLINK_H
