/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/time_utils.h"

/*
 * Bare Metal Implementation Check:
 * Ideally this should be linked against a hardware specific driver
 * or the user provides a 'ticks' variable.
 * For now, we provide a weak symbol or just a stub that increments
 * so basics don't crash, but it requires external SysTick integration.
 */

/* Volatile tick counter - expected to be incremented by SysTick ISR */
volatile uint32_t g_iolink_ticks_ms = 0;

uint32_t iolink_time_get_ms(void)
{
    return g_iolink_ticks_ms;
}

uint64_t iolink_time_get_us(void)
{
    /* Rough approximation or need a high-res timer */
    return IOLINK_US_FROM_MS(g_iolink_ticks_ms);
}
