/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include <stdio.h>
#include "iolinki/iolink.h"

/**
 * @brief Placeholder for a real hardware PHY implementation
 */
static const iolink_phy_api_t g_hw_phy = {
    .init = NULL, /* No hardware yet */
    .set_mode = NULL,
    .set_baudrate = NULL,
    .send = NULL,
    .recv_byte = NULL
};

int main(void)
{
    printf("IO-Link Simple Device Example\n");

    /* Use default configuration */
    if (iolink_init(&g_hw_phy, NULL) != 0) {
        printf("Failed to initialize IO-Link stack\n");
        return -1;
    }

    printf("IO-Link stack initialized successfully\n");

    return 0;
}
