/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#ifndef IOLINK_CRC_H
#define IOLINK_CRC_H

#include <stdint.h>

/**
 * @file crc.h
 * @brief IO-Link CRC calculation (Spec V1.1.5)
 */

/**
 * @brief Calculate IO-Link 6-bit CRC
 * 
 * Used for M-sequences and ISDU headers.
 * Polynomial: x^6 + x^4 + x^3 + x^2 + 1 (0x1D)
 * Initial value: 0x15
 * 
 * @param data Data to checksum
 * @param len Length in bytes
 * @return uint8_t 6-bit CRC
 */
uint8_t iolink_crc6(const uint8_t *data, uint8_t len);

/**
 * @brief Calculate IO-Link 8-bit Checksum (CK)
 * 
 * Used in M-sequences.
 * @param mc Master Command byte
 * @param ckt Checksum/Status byte
 * @return uint8_t Calculated CK
 */
uint8_t iolink_checksum_ck(uint8_t mc, uint8_t ckt);

#endif // IOLINK_CRC_H
