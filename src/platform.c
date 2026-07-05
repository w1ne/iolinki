/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file platform.c
 * @brief Default weak platform abstraction implementations.
 * @ingroup iolinki_platform
 *
 * Provides no-op / not-implemented weak defaults for the critical-section and
 * NVM access hooks so the stack links without a platform port; a real port
 * overrides these symbols.
 */

#include "iolinki/platform.h"

/* Weak definitions allow the application to override them without link errors */

#if defined(__GNUC__) || defined(__clang__)
#define WEAK __attribute__((weak))
#else
#define WEAK
#endif

WEAK void iolink_critical_enter(void)
{
    /* Default: Do nothing (Bare metal single loop is implicitly safe if no IRQ contention) */
}

WEAK void iolink_critical_exit(void)
{
    /* Default: Do nothing */
}

WEAK int iolink_nvm_read(uint32_t offset, uint8_t* data, size_t len)
{
    (void) offset;
    (void) data;
    (void) len;
    /* Default: Not implemented */
    return -1;
}

WEAK int iolink_nvm_write(uint32_t offset, const uint8_t* data, size_t len)
{
    (void) offset;
    (void) data;
    (void) len;
    /* Default: Not implemented */
    return -1;
}
