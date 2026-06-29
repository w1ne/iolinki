/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/phy_generic.h"

static int generic_init(void* user)
{
    (void) user;
    /* Template: initialize UART/GPIO/transceiver here. */
    return -1;
}

static void generic_set_mode(void* user, iolink_phy_mode_t mode)
{
    (void) user;
    (void) mode;
    /* Template: configure transceiver for SIO/SDCI. */
}

static void generic_set_baudrate(void* user, iolink_baudrate_t baudrate)
{
    (void) user;
    (void) baudrate;
    /* Template: configure UART speed for COM1/2/3. */
}

static int generic_send(void* user, const uint8_t* data, size_t len)
{
    (void) user;
    (void) data;
    (void) len;
    /* Template: transmit data over C/Q line. */
    return -1;
}

static int generic_recv_byte(void* user, uint8_t* byte)
{
    (void) user;
    (void) byte;
    /* Template: non-blocking receive. */
    return 0;
}

static int generic_detect_wakeup(void* user)
{
    (void) user;
    /* Template: detect 80us wake-up pulse on C/Q line. */
    return 0;
}

static void generic_set_cq_line(void* user, uint8_t state)
{
    (void) user;
    (void) state;
    /* Template: drive C/Q line high/low in SIO mode. */
}

static int generic_get_voltage_mv(void* user)
{
    (void) user;
    /* Template: return supply voltage in mV, or negative if unavailable. */
    return -1;
}

static bool generic_is_short_circuit(void* user)
{
    (void) user;
    /* Template: return true if fault detected. */
    return false;
}

static const iolink_phy_api_t g_phy_generic = {.init = generic_init,
                                               .set_mode = generic_set_mode,
                                               .set_baudrate = generic_set_baudrate,
                                               .send = generic_send,
                                               .recv_byte = generic_recv_byte,
                                               .detect_wakeup = generic_detect_wakeup,
                                               .set_cq_line = generic_set_cq_line,
                                               .get_voltage_mv = generic_get_voltage_mv,
                                               .is_short_circuit = generic_is_short_circuit};

const iolink_phy_api_t* iolink_phy_generic_get(void)
{
    return &g_phy_generic;
}
