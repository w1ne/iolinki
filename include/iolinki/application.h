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
 * @defgroup iolinki_application Application Layer Callbacks
 * @brief Application lifecycle and process-data callback hooks.
 * @{
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

/**
 * @brief Select a prebuilt PD-in buffer for the current validated M-sequence.
 *
 * Invoked once after the complete Type-1/2 request passes checksum and type
 * validation, after its PD-out is copied, and before its reply is assembled.
 * The callback must be bounded, nonblocking, and must not re-enter the stack.
 * Selecting a pointer requires no application copy. The stack copies the
 * selected bytes into the reply and records them for subsequent PD reads.
 *
 * @param user Application context supplied in the device configuration.
 * @param pd_out PD-out of this request; do not retain this pointer.
 * @param pd_out_len Number of current PD-out octets.
 * @param pd_in_len Required length of the selected buffer.
 * @param valid In/out validity flag, initially the previously configured value.
 * @return Buffer containing at least pd_in_len octets, valid until this
 *         iolink_device_process call returns; NULL keeps the cached PD-in.
 */
typedef const uint8_t* (*iolink_pd_input_select_cb_t)(void* user, const uint8_t* pd_out,
                                                      uint8_t pd_out_len, uint8_t pd_in_len,
                                                      bool* valid);

/** @} */ /* end of iolinki_application */

#endif  // IOLINK_APPLICATION_H
