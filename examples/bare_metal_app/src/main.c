/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/iolink.h"
#include "iolinki/phy_virtual.h"

/* Extern the volatile tick counter from time_utils_baremetal.c */
extern volatile uint32_t g_iolink_ticks_ms;

void sys_tick_handler(void)
{
    g_iolink_ticks_ms++;
}

int main(void)
{
    /* Initialize stack with virtual PHY (simplified for bare metal, typically uses UART/SPI) */
    const iolink_phy_api_t *phy = iolink_phy_virtual_get();

    if (iolink_init(phy, NULL) != 0) {
        return -1;
    }

    /* Main Super-Loop */
    while (1) {
        /* Process Stack */
        iolink_process();

        /* Simulate System Tick (Time Passage) */
        sys_tick_handler();

        /* In a real bare-metal app, we would wait for interrupt or sleep until next tick
         * For this test/demo, we just loop for a bit to prevent 100% CPU on host simulation
         * or just break after N cycles for automated testing.
         */
        if (g_iolink_ticks_ms > 100) {
            break; /* Exit for test success */
        }
    }

    return 0;
}
