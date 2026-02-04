/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
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
