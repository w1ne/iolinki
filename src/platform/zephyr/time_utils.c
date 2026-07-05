/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file time_utils.c
 * @brief Zephyr time utilities implementation (kernel uptime).
 * @ingroup iolinki_time
 *
 * Provides the millisecond and microsecond time sources backed by the Zephyr
 * kernel uptime.
 */

#include "iolinki/time_utils.h"
#include <zephyr/kernel.h>

uint32_t iolink_time_get_ms(void)
{
    return (uint32_t) k_uptime_get();
}

uint64_t iolink_time_get_us(void)
{
    /* k_ticks_to_us_near64(k_uptime_ticks()) is better but this is simple */
    return iolink_us_from_ms(k_uptime_get());
}
