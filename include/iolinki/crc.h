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
#include <stddef.h>

/**
 * @file crc.h
 * @brief IO-Link message checksum (Spec V1.1.5 A.1.6)
 */

/**
 * @defgroup iolinki_crc Checksum
 * @brief IO-Link 6-bit message checksum computation (A.1.6).
 * @{
 */

/**
 * @brief Calculate the IO-Link message checksum (A.1.6).
 *
 * Every octet of the message is XOR processed with a seed of 0x52, then
 * compressed from 8 to 6 bits using equations (A.1). The checksum/M-sequence
 * type octet (CKT for master messages, CKS for device replies) is part of the
 * message with its checksum bits (0-5) set to zero; its type/status bits are
 * included as-is. Callers OR the returned 6-bit value into that octet.
 *
 * @param octets Message octets; the checksum octet must already have bits 0-5
 *               cleared.
 * @param len Number of octets in @p octets.
 * @return uint8_t 6-bit compressed checksum (0x00-0x3F).
 */
uint8_t iolink_checksum6(const uint8_t* octets, size_t len);

/** @} */ /* end of iolinki_crc */

#endif  // IOLINK_CRC_H
