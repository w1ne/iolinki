/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_TIME_UTILS_H
#define IOLINK_TIME_UTILS_H

#include <stdint.h>

/**
 * @file time_utils.h
 * @brief Time abstractions for IO-Link timing enforcement
 */

#define IOLINK_US_PER_MS 1000ULL
#define IOLINK_US_FROM_MS(ms) ((uint64_t)(ms) * IOLINK_US_PER_MS)

/**
 * @brief Get system time in milliseconds
 * @return uint32_t current time in ms
 */
uint32_t iolink_time_get_ms(void);

/**
 * @brief Get system time in microseconds
 * @return uint64_t current time in us
 */
uint64_t iolink_time_get_us(void);

#endif // IOLINK_TIME_UTILS_H
