/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_APPLICATION_H
#define IOLINK_APPLICATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @file application.h
 * @brief IO-Link Application Layer API for Process Data
 */

/**
 * @brief Application lifecycle and Process Data callbacks.
 *
 * Any field may be NULL. Lifecycle callbacks fire once when the DLL enters the
 * corresponding state. The PD callbacks fire on
 * every processed OPERATE cycle: @ref on_pd_output reports the output Process
 * Data received from the Master (Master -> Device), and @ref on_pd_input reports
 * the input Process Data the Device is currently publishing (Device -> Master).
 */
typedef struct
{
    void (*on_startup)(void);                              /**< Entered STARTUP */
    void (*on_preoperate)(void);                           /**< Entered PREOPERATE */
    void (*on_operate)(void);                              /**< Entered OPERATE */
    void (*on_pd_input)(const uint8_t* data, uint8_t len); /**< Input PD published */
    void (*on_pd_output)(uint8_t* data, uint8_t len);      /**< Output PD received */
} iolink_app_callbacks_t;

#endif  // IOLINK_APPLICATION_H
