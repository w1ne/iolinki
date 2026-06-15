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
 * @brief Update Process Data Input (Device -> Master)
 *
 * @param data Pointer to input data
 * @param len Length in bytes
 * @param valid Data validity flag (true = valid, false = invalid)
 * @return int 0 on success, negative on error
 */
int iolink_pd_input_update(const uint8_t* data, size_t len, bool valid);

/**
 * @brief Read Process Data Output (Master -> Device)
 *
 * @param data Pointer to buffer to store output data
 * @param len Max length to read
 * @return int Number of bytes read, negative on error
 */
int iolink_pd_output_read(uint8_t* data, size_t len);

/**
 * @brief Application lifecycle and Process Data callbacks.
 *
 * Any field may be NULL. Lifecycle callbacks fire once when the DLL enters the
 * corresponding state (driven from iolink_process()). The PD callbacks fire on
 * every processed OPERATE cycle: @ref on_pd_output reports the output Process
 * Data received from the Master (Master -> Device), and @ref on_pd_input reports
 * the input Process Data the Device is currently publishing (Device -> Master).
 * They complement the polling API (iolink_pd_output_read / iolink_pd_input_update).
 */
typedef struct
{
    void (*on_startup)(void);                              /**< Entered STARTUP */
    void (*on_preoperate)(void);                           /**< Entered PREOPERATE */
    void (*on_operate)(void);                              /**< Entered OPERATE */
    void (*on_pd_input)(const uint8_t* data, uint8_t len); /**< Input PD published */
    void (*on_pd_output)(uint8_t* data, uint8_t len);      /**< Output PD received */
} iolink_app_callbacks_t;

/**
 * @brief Register application callbacks (or NULL to clear).
 *
 * The structure is referenced, not copied, so it must remain valid for the
 * lifetime of the stack.
 *
 * @param callbacks Pointer to a callbacks structure, or NULL.
 */
void iolink_app_register(const iolink_app_callbacks_t* callbacks);

#endif  // IOLINK_APPLICATION_H
