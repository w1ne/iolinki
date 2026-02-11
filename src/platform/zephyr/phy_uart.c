/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "phy_uart.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(iolink_phy_uart, LOG_LEVEL_INF);

static const struct device *g_uart_dev = NULL;

static int uart_phy_init(void)
{
    if (g_uart_dev == NULL) {
        LOG_ERR("UART device not set");
        return -1;
    }

    if (!device_is_ready(g_uart_dev)) {
        LOG_ERR("UART device not ready");
        return -1;
    }

    return 0;
}

static void uart_phy_set_mode(iolink_phy_mode_t mode)
{
    LOG_INF("Set mode: %d (Not fully implemented for UART PHY, assuming always active)", mode);
}

static void uart_phy_set_baudrate(iolink_baudrate_t baudrate)
{
    if (g_uart_dev == NULL) {
        return;
    }

    struct uart_config cfg;
    if (uart_config_get(g_uart_dev, &cfg) != 0) {
        LOG_ERR("Failed to get UART config");
        return;
    }

    switch (baudrate) {
        case IOLINK_BAUDRATE_COM1:
            cfg.baudrate = 4800;
            break;
        case IOLINK_BAUDRATE_COM2:
            cfg.baudrate = 38400;
            break;
        case IOLINK_BAUDRATE_COM3:
            cfg.baudrate = 230400;
            break;
        default:
            LOG_WRN("Unknown baudrate %d, ignoring", baudrate);
            return;
    }

    if (uart_configure(g_uart_dev, &cfg) != 0) {
        LOG_ERR("Failed to set baudrate to %d", cfg.baudrate);
    } else {
        LOG_INF("Baudrate set to %d", cfg.baudrate);
    }
}

static int uart_phy_send(const uint8_t* data, size_t len)
{
    if (g_uart_dev == NULL) {
        return -1;
    }

    for (size_t i = 0; i < len; i++) {
        uart_poll_out(g_uart_dev, data[i]);
    }
    return (int)len;
}

static int uart_phy_recv_byte(uint8_t* byte)
{
    if (g_uart_dev == NULL) {
        return -1;
    }

    unsigned char c;
    if (uart_poll_in(g_uart_dev, &c) == 0) {
        *byte = c;
        return 1;
    }
    return 0;
}

/* Wakeup detection: Look for 0x55 byte which simulates the WAKEUP pulse */
static int uart_phy_detect_wakeup(void)
{
    if (g_uart_dev == NULL) {
        return 0;
    }

    uint8_t c;
    /* Drain FIFO looking for 0x55 */
    while (uart_poll_in(g_uart_dev, &c) == 0) {
        if (c == 0x55) {
            LOG_INF("Wakeup detected (0x55)");
            return 1;
        }
    }
    return 0;
}


static const iolink_phy_api_t g_phy_uart = {
    .init = uart_phy_init,
    .set_mode = uart_phy_set_mode,
    .set_baudrate = uart_phy_set_baudrate,
    .send = uart_phy_send,
    .recv_byte = uart_phy_recv_byte,
    .detect_wakeup = uart_phy_detect_wakeup
};

int iolink_phy_uart_init(const struct device *uart_dev)
{
    g_uart_dev = uart_dev;
    return 0;
}

const iolink_phy_api_t* iolink_phy_uart_get(void)
{
    return &g_phy_uart;
}
