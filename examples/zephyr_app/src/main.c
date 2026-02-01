/*
 * Copyright (c) 2026 iolinki-project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "iolinki/iolink.h"
#include "iolinki/phy_virtual.h"

LOG_MODULE_REGISTER(iolink_demo, LOG_LEVEL_INF);

int main(void)
{
    LOG_INF("Starting IO-Link Zephyr Demo");

    /* Use virtual PHY for demo */
    iolink_init(iolink_phy_virtual_get());

    while (1) {
        iolink_process();
        k_msleep(1); /* 1ms cycle */
    }
    return 0;
}
