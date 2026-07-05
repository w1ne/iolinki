/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file time_utils.c
 * @brief Linux time utilities implementation (CLOCK_MONOTONIC).
 * @ingroup iolinki_time
 *
 * Provides the millisecond and microsecond monotonic time sources used by the
 * stack, backed by POSIX clock_gettime().
 */

#include "iolinki/time_utils.h"
#include <time.h>

uint32_t iolink_time_get_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0U;
    }
    return (uint32_t) ((uint64_t) ts.tv_sec * 1000U + (uint64_t) ts.tv_nsec / 1000000U);
}

uint64_t iolink_time_get_us(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0ULL;
    }
    return ((uint64_t) ts.tv_sec * 1000000ULL + (uint64_t) ts.tv_nsec / 1000ULL);
}
