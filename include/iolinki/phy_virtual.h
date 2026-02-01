#ifndef IOLINK_PHY_VIRTUAL_H
#define IOLINK_PHY_VIRTUAL_H

#include "iolinki/phy.h"

/**
 * @file phy_virtual.h
 * @brief Virtual PHY implementation for simulation
 */

/**
 * @brief Get the virtual PHY provider
 * 
 * This PHY communicates with a virtual IO-Link Master over a network socket.
 * 
 * @return const iolink_phy_api_t* 
 */
const iolink_phy_api_t* iolink_phy_virtual_get(void);

#endif // IOLINK_PHY_VIRTUAL_H
