/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef STM32G0_TIMEBASE_H
#define STM32G0_TIMEBASE_H

#include <stdint.h>

/* 2^32 microseconds = 4294967 whole milliseconds + 296 microseconds.
 * Keep that fractional carry so get_ms needs only one 32-bit division, with
 * exactly the same modulo-2^32 result as the extended 64-bit clock / 1000. */
static inline void board_time_advance_epoch(uint32_t* milliseconds, uint32_t* remainder_us,
                                            uint32_t new_high)
{
    if (new_high == 0U) {
        /* Preserve the extended microsecond clock's eventual 64-bit wrap. */
        *milliseconds = 0U;
        *remainder_us = 0U;
        return;
    }
    *milliseconds += 4294967U;
    *remainder_us += 296U;
    if (*remainder_us >= 1000U) {
        *remainder_us -= 1000U;
        ++*milliseconds;
    }
}

static inline uint32_t board_time_milliseconds(uint32_t epoch_ms, uint32_t remainder_us,
                                               uint32_t low_us)
{
    const uint32_t whole = low_us / 1000U;
    const uint32_t fraction = low_us - whole * 1000U;
    /* Adding the fraction to low_us directly could overflow at timer wrap. */
    return epoch_ms + whole + ((fraction + remainder_us >= 1000U) ? 1U : 0U);
}

#endif
