/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/phy_virtual.h"
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <errno.h>
#include <string.h>

static int g_fd = -1;
static const char *g_port_path = NULL;

void iolink_phy_virtual_set_port(const char *port)
{
    g_port_path = port;
}

static int virtual_init(void)
{
    if (g_port_path == NULL) {
        printf("[PHY-VIRTUAL] Error: Port not set\n");
        return -1;
    }
    
    g_fd = open(g_port_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (g_fd < 0) {
        printf("[PHY-VIRTUAL] Error opening %s: %s\n", g_port_path, strerror(errno));
        return -1;
    }
    
    /* Config for raw mode */
    struct termios tty;
    if (tcgetattr(g_fd, &tty) != 0) {
        printf("[PHY-VIRTUAL] Error from tcgetattr: %s\n", strerror(errno));
        return -1;
    }
    
    cfmakeraw(&tty);
    
    /* Set timeouts: No blocking */
    tty.c_cc[VMIN] = 0U;
    tty.c_cc[VTIME] = 0U;
    
    if (tcsetattr(g_fd, TCSANOW, &tty) != 0) {
        printf("[PHY-VIRTUAL] Error from tcsetattr: %s\n", strerror(errno));
        return -1;
    }
    
    printf("[PHY-VIRTUAL] Initialized connection to %s (fd=%d)\n", g_port_path, g_fd);
    return 0;
}

static void virtual_set_mode(iolink_phy_mode_t mode)
{
    printf("[PHY-VIRTUAL] Mode set to: %d\n", (int)mode);
}

static void virtual_set_baudrate(iolink_baudrate_t baudrate)
{
    printf("[PHY-VIRTUAL] Baudrate set to: %d\n", (int)baudrate);
}

static int virtual_send(const uint8_t *data, size_t len)
{
    if ((g_fd < 0) || (data == NULL)) {
        return -1;
    }
    if (len == 0U) {
        return 0;
    }
    return (int)write(g_fd, data, len);
}

static uint8_t g_peek_buf = 0;
static bool g_peek_valid = false;

static int virtual_recv_byte(uint8_t *byte)
{
    if ((g_fd < 0) || (byte == NULL)) {
        return 0;
    }
    
    /* Check peek buffer first */
    if (g_peek_valid) {
        *byte = g_peek_buf;
        g_peek_valid = false;
        return 1;
    }
    
    ssize_t n = read(g_fd, byte, 1);
    return (n > 0) ? 1 : 0;
}

static int virtual_detect_wakeup(void)
{
    if (g_fd < 0) {
        return 0;
    }

    /* If we already have a valid peek byte, check it */
    if (g_peek_valid) {
        if (g_peek_buf == 0x55) {
            /* Consume wakeup byte */
            g_peek_valid = false;
            return 1;
        }
        return 0;
    }

    /* Try to read a byte */
    uint8_t b;
    ssize_t n = read(g_fd, &b, 1);
    if (n > 0) {
        if (b == 0x55) {
            /* Wakeup! Consume it. */
            return 1;
        } else {
            /* Not wakeup, save for recv_byte */
            g_peek_buf = b;
            g_peek_valid = true;
            return 0;
        }
    }
    return 0;
}

static const iolink_phy_api_t g_phy_virtual = {
    .init = virtual_init,
    .set_mode = virtual_set_mode,
    .set_baudrate = virtual_set_baudrate,
    .send = virtual_send,
    .recv_byte = virtual_recv_byte,
    .detect_wakeup = virtual_detect_wakeup
};

const iolink_phy_api_t* iolink_phy_virtual_get(void)
{
    return &g_phy_virtual;
}
