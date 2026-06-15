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
#include <zephyr/sys/ring_buffer.h>

LOG_MODULE_REGISTER(iolink_phy_uart, LOG_LEVEL_INF);

/*
 * Devicetree resolution for the IO-Link UART.
 *
 * Preferred: a "zephyr,iolink-uart" chosen node.
 * Fallback:  an "iolink-uart" alias.
 */
#if DT_HAS_CHOSEN(zephyr_iolink_uart)
#define IOLINK_UART_NODE DT_CHOSEN(zephyr_iolink_uart)
#elif DT_NODE_EXISTS(DT_ALIAS(iolink_uart))
#define IOLINK_UART_NODE DT_ALIAS(iolink_uart)
#endif

#ifndef CONFIG_IOLINK_PHY_UART_RX_RING_SIZE
#define CONFIG_IOLINK_PHY_UART_RX_RING_SIZE 256
#endif

static const struct device* g_uart_dev;

static uint8_t g_rx_storage[CONFIG_IOLINK_PHY_UART_RX_RING_SIZE];
static struct ring_buf g_rx_rb;

/*
 * UART interrupt service routine: drains the hardware FIFO into the RX ring
 * buffer. Bytes that do not fit are dropped (and counted via a log warning) to
 * keep the ISR bounded.
 */
static void uart_phy_isr(const struct device* dev, void* user_data)
{
    ARG_UNUSED(user_data);

    if (!uart_irq_update(dev)) {
        return;
    }

    while (uart_irq_rx_ready(dev)) {
        uint8_t buf[32];
        int n = uart_fifo_read(dev, buf, sizeof(buf));

        if (n <= 0) {
            break;
        }

        uint32_t put = ring_buf_put(&g_rx_rb, buf, (uint32_t) n);
        if (put < (uint32_t) n) {
            LOG_WRN("RX ring overflow, dropped %u bytes", (unsigned) ((uint32_t) n - put));
        }
    }
}

static int uart_phy_init(void)
{
    if (g_uart_dev == NULL) {
        LOG_ERR("UART device not set; call iolink_phy_uart_init[_default]() first");
        return -1;
    }

    if (!device_is_ready(g_uart_dev)) {
        LOG_ERR("UART device %s not ready", g_uart_dev->name);
        return -1;
    }

    ring_buf_init(&g_rx_rb, sizeof(g_rx_storage), g_rx_storage);

    int ret = uart_irq_callback_user_data_set(g_uart_dev, uart_phy_isr, NULL);
    if (ret < 0) {
        if (ret == -ENOTSUP || ret == -ENOSYS) {
            LOG_WRN("UART %s lacks IRQ API; falling back to polled RX", g_uart_dev->name);
        }
        else {
            LOG_ERR("Failed to set UART callback: %d", ret);
            return ret;
        }
    }
    else {
        uart_irq_rx_enable(g_uart_dev);
    }

    return 0;
}

static void uart_phy_set_mode(iolink_phy_mode_t mode)
{
    /*
     * A plain UART cannot drive SIO/SDCI mode switching on the C/Q line; the
     * line discipline is owned by the transceiver front-end. The mode hint is
     * logged for diagnostics only.
     */
    LOG_DBG("set_mode(%d): no-op for plain UART PHY", mode);
}

static void uart_phy_set_baudrate(iolink_baudrate_t baudrate)
{
    if (g_uart_dev == NULL) {
        return;
    }

    struct uart_config cfg;
    if (uart_config_get(g_uart_dev, &cfg) != 0) {
        LOG_ERR("Failed to read UART config");
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
            LOG_WRN("Unknown baudrate enum %d, ignoring", baudrate);
            return;
    }

    int ret = uart_configure(g_uart_dev, &cfg);
    if (ret == -ENOSYS || ret == -ENOTSUP) {
        LOG_WRN("Runtime reconfigure unsupported; using devicetree current-speed");
    }
    else if (ret != 0) {
        LOG_ERR("Failed to set baudrate %d: %d", cfg.baudrate, ret);
    }
    else {
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
    return (int) len;
}

static int uart_phy_recv_byte(uint8_t* byte)
{
    if (g_uart_dev == NULL) {
        return -1;
    }

    /* Prefer the interrupt-fed ring buffer; fall back to polled reads. */
    if (ring_buf_get(&g_rx_rb, byte, 1) == 1) {
        return 1;
    }

    unsigned char c;
    if (uart_poll_in(g_uart_dev, &c) == 0) {
        *byte = (uint8_t) c;
        return 1;
    }

    return 0;
}

static const iolink_phy_api_t g_phy_uart = {
    .init = uart_phy_init,
    .set_mode = uart_phy_set_mode,
    .set_baudrate = uart_phy_set_baudrate,
    .send = uart_phy_send,
    .recv_byte = uart_phy_recv_byte,
    /*
     * detect_wakeup / set_cq_line / get_voltage_mv / is_short_circuit are left
     * NULL: a plain UART cannot observe or drive the C/Q wake-up pulse, and L+
     * monitoring belongs to the transceiver front-end, not the UART.
     */
};

int iolink_phy_uart_init(const struct device* uart_dev)
{
    g_uart_dev = uart_dev;
    return 0;
}

int iolink_phy_uart_init_default(void)
{
#ifdef IOLINK_UART_NODE
    const struct device* dev = DEVICE_DT_GET(IOLINK_UART_NODE);

    if (!device_is_ready(dev)) {
        LOG_ERR("IO-Link UART device not ready");
        return -ENODEV;
    }

    g_uart_dev = dev;
    LOG_INF("IO-Link UART PHY bound to %s", dev->name);
    return 0;
#else
    LOG_ERR("No 'zephyr,iolink-uart' chosen node or 'iolink-uart' alias defined");
    return -ENODEV;
#endif
}

const iolink_phy_api_t* iolink_phy_uart_get(void)
{
    return &g_phy_uart;
}
