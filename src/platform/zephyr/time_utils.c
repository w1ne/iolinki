/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/time_utils.h"
#include <zephyr/kernel.h>

uint32_t iolink_time_get_ms(void)
{
    return (uint32_t)k_uptime_get();
}

uint64_t iolink_time_get_us(void)
{
    /* k_ticks_to_us_near64(k_uptime_ticks()) is better but this is simple */
    return (uint64_t)(k_uptime_get() * 1000); 
}
