/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_PHY_H
#define IOLINK_PHY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @file phy.h
 * @brief IO-Link Physical Layer (PHY) Abstraction Interface
 */

/**
 * @defgroup iolinki_phy Physical Layer (PHY) Interface
 * @brief Hardware-agnostic transceiver contract, operating modes and baudrates.
 * @{
 */

/**
 * @brief IO-Link Operating Modes
 */
typedef enum
{
    IOLINK_PHY_MODE_INACTIVE = 0U, /**< PHY interface disabled */
    IOLINK_PHY_MODE_SIO = 1U,      /**< Standard I/O mode (Digital Input/Output) */
    IOLINK_PHY_MODE_SDCI = 2U      /**< Digital Communication mode (IO-Link exchange) */
} iolink_phy_mode_t;

/**
 * @brief IO-Link Communication Baudrates (COMx)
 */
typedef enum
{
    IOLINK_BAUDRATE_COM1 = 0U, /**< 4.8 kbit/s */
    IOLINK_BAUDRATE_COM2 = 1U, /**< 38.4 kbit/s */
    IOLINK_BAUDRATE_COM3 = 2U  /**< 230.4 kbit/s */
} iolink_baudrate_t;

/**
 * @brief Origin of a response boundary timestamp.
 *
 * Software estimates (for example UART RX interrupt / TX register submission)
 * can detect scheduling errors. They are not wire timing qualification.
 */
typedef enum
{
    IOLINK_PHY_TIMING_UNAVAILABLE = 0U,
    IOLINK_PHY_TIMING_ESTIMATED = 1U,
    IOLINK_PHY_TIMING_CAPTURED = 2U
} iolink_phy_timing_quality_t;

/** @brief Boundaries of the request/reply associated with the latest send().
 * Both timestamps use the same monotonic microsecond clock. rx_end_us is the
 * end of the last master stop bit; tx_start_us is the first device start bit.
 * Completion of a blocking send() must never be substituted for tx_start_us.
 */
typedef struct
{
    uint64_t rx_end_us;
    uint64_t tx_start_us;
    iolink_phy_timing_quality_t quality;
} iolink_phy_response_timing_t;

/**
 * @brief Physical Layer (PHY) API Structure
 *
 * This interface defines the contract between the stack and the hardware-specific
 * transceiver driver (e.g. for MAX14827, L6362, or a Virtual TTY).
 * PHY callbacks are invoked synchronously from foreground stack APIs (the
 * initializer calls @c init; runtime callbacks are called by the process API).
 * They are not ISR entry points. Keep them bounded, keep @c recv_byte
 * nonblocking, and do not call stack processing recursively from a PHY callback.
 */
typedef struct
{
    void* user; /**< Opaque driver context passed back to every callback. */

    /**
     * @brief Initialize transceiver hardware
     * @return 0 on success, negative error code on hardware failure
     */
    int (*init)(void* user);

    /**
     * @brief Set PHY operating mode (SDCI vs SIO)
     * @param mode Target mode
     */
    void (*set_mode)(void* user, iolink_phy_mode_t mode);

    /**
     * @brief Set communication baudrate
     * @param baudrate Target COMx speed
     */
    void (*set_baudrate)(void* user, iolink_baudrate_t baudrate);

    /**
     * @brief Send a buffer of data over the line
     *
     * The caller waits synchronously for this function to return. A hardware
     * driver may wait for bounded TX completion, but must leave interrupts
     * enabled when its UART/DMA completion depends on them.
     * @param data Pointer to source buffer
     * @param len Number of bytes to transmit
     * @return Number of bytes actually sent, or negative on error
     */
    int (*send)(void* user, const uint8_t* data, size_t len);

    /**
     * @brief Non-blocking receive for a single byte
     * @param byte Pointer to store received byte
     * @return 1 if byte available and read, 0 if nothing received, negative on
     *         a receive discontinuity. A negative result invalidates the
     *         partially received frame and any remaining queued bytes; the
     *         stack discards that queue before accepting another frame.
     */
    int (*recv_byte)(void* user, uint8_t* byte);

    /* Optional Diagnostic/Support Functions (can be NULL) */

    /**
     * @brief Detect wake-up pulse (80µs pulse on C/Q line)
     * @return 1 if wake-up pulse detected during current window, 0 otherwise
     */
    int (*detect_wakeup)(void* user);

    /**
     * @brief Manually set C/Q line state (for SIO push-pull)
     * @param state 0 for Low, 1 for High
     */
    void (*set_cq_line)(void* user, uint8_t state);

    /**
     * @brief Get L+ supply voltage
     * @return Voltage in millivolts, negative if measurement unavailable
     */
    int (*get_voltage_mv)(void* user);

    /**
     * @brief Check for hardware fault condition
     * @return true if short circuit or overtemperature detected
     */
    bool (*is_short_circuit)(void* user);

    /**
     * @brief Read fresh request/reply boundary timestamps for the latest send.
     * @return 1 for a fresh sample, 0 if unavailable, negative on invalid data.
     * Clear validity before every send and on RX/TX errors. A stale sample from
     * a preceding exchange must not be returned. This optional callback is
     * required for an evaluated t_A check; missing data is counted as
     * unavailable, not as a timing violation. Invalid fresh data remains an
     * enforcement error.
     */
    int (*get_response_timing)(void* user, iolink_phy_response_timing_t* timing);
} iolink_phy_api_t;

/** @} */ /* end of iolinki_phy */

#endif  // IOLINK_PHY_H
