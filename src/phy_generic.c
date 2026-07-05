/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file phy_generic.c
 * @brief Template/skeleton generic UART PHY adapter.
 * @ingroup iolinki_phy_generic
 *
 * Provides a non-functional reference PHY implementation whose hooks are meant
 * to be filled in with target-specific UART/GPIO/transceiver code.
 */

#include "iolinki/phy_generic.h"

/** @brief Template PHY init hook (returns failure until implemented). */
static int generic_init(void* user)
{
    (void) user;
    /* Template: initialize UART/GPIO/transceiver here. */
    return -1;
}

/** @brief Template hook to configure the transceiver for SIO/SDCI mode. */
static void generic_set_mode(void* user, iolink_phy_mode_t mode)
{
    (void) user;
    (void) mode;
    /* Template: configure transceiver for SIO/SDCI. */
}

/** @brief Template hook to configure UART speed for COM1/2/3. */
static void generic_set_baudrate(void* user, iolink_baudrate_t baudrate)
{
    (void) user;
    (void) baudrate;
    /* Template: configure UART speed for COM1/2/3. */
}

/** @brief Template hook to transmit data over the C/Q line. */
static int generic_send(void* user, const uint8_t* data, size_t len)
{
    (void) user;
    (void) data;
    (void) len;
    /* Template: transmit data over C/Q line. */
    return -1;
}

/** @brief Template hook for non-blocking single-byte receive. */
static int generic_recv_byte(void* user, uint8_t* byte)
{
    (void) user;
    (void) byte;
    /* Template: non-blocking receive. */
    return 0;
}

/** @brief Template hook to detect the 80us wake-up pulse on the C/Q line. */
static int generic_detect_wakeup(void* user)
{
    (void) user;
    /* Template: detect 80us wake-up pulse on C/Q line. */
    return 0;
}

/** @brief Template hook to drive the C/Q line high/low in SIO mode. */
static void generic_set_cq_line(void* user, uint8_t state)
{
    (void) user;
    (void) state;
    /* Template: drive C/Q line high/low in SIO mode. */
}

/** @brief Template hook to read supply voltage in mV (negative if unavailable). */
static int generic_get_voltage_mv(void* user)
{
    (void) user;
    /* Template: return supply voltage in mV, or negative if unavailable. */
    return -1;
}

/** @brief Template hook returning true when a short-circuit fault is detected. */
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
