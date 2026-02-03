/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/*
 * Copyright (c) 2026 iolinki-project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include "iolinki/iolink.h"
#include "iolinki/phy_virtual.h"

#include <stdlib.h>

LOG_MODULE_REGISTER(iolink_demo, LOG_LEVEL_INF);

int main(void)
{
    LOG_INF("Starting IO-Link Zephyr Demo");

    const char *port = getenv("IOLINK_PORT");
    if (port) {
        iolink_phy_virtual_set_port(port);
        LOG_INF("Connecting to %s", port);
    } else {
        LOG_WRN("IOLINK_PORT not set, using default");
        /* phy_virtual default is /dev/pts/1 or similar? */
    }

    /* Use virtual PHY for demo */
    if (iolink_init(iolink_phy_virtual_get(), NULL) != 0) {
        LOG_ERR("Failed to init IO-Link");
        return -1;
    }

    while (1) {
        iolink_process();
        k_msleep(1); /* 1ms cycle */
    }
    return 0;
}
