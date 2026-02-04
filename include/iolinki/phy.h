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
 * @brief IO-Link Operating Modes
 */
typedef enum {
    IOLINK_PHY_MODE_INACTIVE = 0U,   /**< PHY interface disabled */
    IOLINK_PHY_MODE_SIO = 1U,        /**< Standard I/O mode (Digital Input/Output) */
    IOLINK_PHY_MODE_SDCI = 2U        /**< Digital Communication mode (IO-Link exchange) */
} iolink_phy_mode_t;

/**
 * @brief IO-Link Communication Baudrates (COMx)
 */
typedef enum {
    IOLINK_BAUDRATE_COM1 = 0U,      /**< 4.8 kbit/s */
    IOLINK_BAUDRATE_COM2 = 1U,      /**< 38.4 kbit/s */
    IOLINK_BAUDRATE_COM3 = 2U       /**< 230.4 kbit/s */
} iolink_baudrate_t;

/**
 * @brief Physical Layer (PHY) API Structure
 * 
 * This interface defines the contract between the stack and the hardware-specific
 * transceiver driver (e.g. for MAX14827, L6362, or a Virtual TTY).
 */
typedef struct {
    /**
     * @brief Initialize transceiver hardware
     * @return 0 on success, negative error code on hardware failure
     */
    int (*init)(void);

    /**
     * @brief Set PHY operating mode (SDCI vs SIO)
     * @param mode Target mode
     */
    void (*set_mode)(iolink_phy_mode_t mode);

    /**
     * @brief Set communication baudrate
     * @param baudrate Target COMx speed
     */
    void (*set_baudrate)(iolink_baudrate_t baudrate);

    /**
     * @brief Send a buffer of data over the line
     * @param data Pointer to source buffer
     * @param len Number of bytes to transmit
     * @return Number of bytes actually sent, or negative on error
     */
    int (*send)(const uint8_t *data, size_t len);

    /**
     * @brief Non-blocking receive for a single byte
     * @param byte Pointer to store received byte
     * @return 1 if byte available and read, 0 if nothing received, negative on error
     */
    int (*recv_byte)(uint8_t *byte);
    
    /* Optional Diagnostic/Support Functions (can be NULL) */
    
    /**
     * @brief Detect wake-up pulse (80µs pulse on C/Q line)
     * @return 1 if wake-up pulse detected during current window, 0 otherwise
     */
    int (*detect_wakeup)(void);
    
    /**
     * @brief Manually set C/Q line state (for SIO push-pull)
     * @param state 0 for Low, 1 for High
     */
    void (*set_cq_line)(uint8_t state);
    
    /**
     * @brief Get L+ supply voltage
     * @return Voltage in millivolts, negative if measurement unavailable
     */
    int (*get_voltage_mv)(void);
    
    /**
     * @brief Check for hardware fault condition
     * @return true if short circuit or overtemperature detected
     */
    bool (*is_short_circuit)(void);
} iolink_phy_api_t;

#endif // IOLINK_PHY_H
