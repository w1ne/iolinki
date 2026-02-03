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

typedef enum {
    IOLINK_PHY_MODE_INACTIVE = 0,
    IOLINK_PHY_MODE_SIO,
    IOLINK_PHY_MODE_SDCI
} iolink_phy_mode_t;

typedef enum {
    IOLINK_BAUDRATE_COM1 = 0, /* 4.8 kbit/s */
    IOLINK_BAUDRATE_COM2,      /* 38.4 kbit/s */
    IOLINK_BAUDRATE_COM3       /* 230.4 kbit/s */
} iolink_baudrate_t;

/**
 * @brief PHY API Structure
 * 
 * This structure must be implemented by hardware-specific drivers.
 */
typedef struct {
    /**
     * @brief Initialize hardware
     * @return 0 on success, negative on error
     */
    int (*init)(void);

    /**
     * @brief Set PHY operating mode
     * @param mode Desired mode (SIO/SDCI)
     */
    void (*set_mode)(iolink_phy_mode_t mode);

    /**
     * @brief Set communication baudrate
     * @param baudrate Target baudrate
     */
    void (*set_baudrate)(iolink_baudrate_t baudrate);

    /**
     * @brief Send a buffer of data
     * @param data Pointer to data
     * @param len Number of bytes to send
     * @return Number of bytes actually sent, or negative on error
     */
    int (*send)(const uint8_t *data, size_t len);

    /**
     * @brief Receive a byte
     * @param byte Pointer to store received byte
     * @return 1 if byte received, 0 if no data, negative on error
     */
    int (*recv_byte)(uint8_t *byte);
    
    /* Optional Functions (can be NULL if not supported) */
    
    /**
     * @brief Detect wake-up pulse (80μs pulse on C/Q line)
     * @return 1 if wake-up pulse detected, 0 otherwise
     */
    int (*detect_wakeup)(void);
    
    /**
     * @brief Set C/Q line state
     * @param state 0=low, 1=high
     */
    void (*set_cq_line)(uint8_t state);
    
    /**
     * @brief Get L+ voltage in millivolts
     * @return Voltage in mV (18000-30000 range), negative on error
     */
    int (*get_voltage_mv)(void);
    
    /**
     * @brief Check for short circuit condition
     * @return true if short circuit detected, false otherwise
     */
    bool (*is_short_circuit)(void);
} iolink_phy_api_t;

#endif // IOLINK_PHY_H
