/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_PLATFORM_H
#define IOLINK_PLATFORM_H

#include <stdint.h>
#include <stddef.h>

/**
 * @file platform.h
 * @brief Platform encapsulation for RTOS integration.
 *
 * This file provides weak definitions or macros for critical sections.
 * Platforms/RTOS integrations should override these to ensure thread safety.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Enter a critical section (disable interrupts/scheduler).
 *
 * This function must prevent context switches or ISRs that could
 * corrupt shared data structures (Events, ISDU).
 */
void iolink_critical_enter(void);

/**
 * @brief Exit a critical section (restore interrupts/scheduler).
 */
void iolink_critical_exit(void);

/**
 * @brief Read data from non-volatile memory (NVM).
 * @param offset Offset in NVM
 * @param data Buffer to read into
 * @param len Length to read
 * @return int 0 on success
 */
int iolink_nvm_read(uint32_t offset, uint8_t *data, size_t len);

/**
 * @brief Write data to non-volatile memory (NVM).
 * @param offset Offset in NVM
 * @param data Data to write
 * @param len Length to write
 * @return int 0 on success
 */
int iolink_nvm_write(uint32_t offset, const uint8_t *data, size_t len);

/* Default (Weak) Implementation typically provided in a platform source file.
 * If strictly header-only or macro-based is preferred, use #define macros here.
 * For this stack, we declere functions that the user must implement or we provide weak defaults.
 */

#ifdef __cplusplus
}
#endif

#endif // IOLINK_PLATFORM_H
