/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

/**
 * @file crc.c
 * @brief IO-Link message checksum implementation (A.1.6).
 * @ingroup iolinki_crc
 *
 * Implements the XOR-seeded, 8-to-6-bit compressed message checksum used to
 * protect every IO-Link M-sequence.
 */

#include "iolinki/crc.h"
#include "iolinki/utils.h"
#include <stddef.h>

/*
 * IO-Link message checksum, IO-Link Interface Specification V1.1.5 A.1.6.
 *
 * All octets (including the checksum/type octet with bits 0-5 zeroed) are XOR
 * processed with a seed of 0x52. The 8-bit result is compressed to 6 bits by
 * the equations in (A.1):
 *
 *   D5_6 = D7_8 xor D5_8 xor D3_8 xor D1_8
 *   D4_6 = D6_8 xor D4_8 xor D2_8 xor D0_8
 *   D3_6 = D7_8 xor D6_8
 *   D2_6 = D5_8 xor D4_8
 *   D1_6 = D3_8 xor D2_8
 *   D0_6 = D1_8 xor D0_8
 */
uint8_t iolink_checksum6(const uint8_t* octets, size_t len)
{
    uint8_t ck8 = 0x52U; /* A.1.6 seed value */

    if (!iolink_buf_is_valid(octets, len)) {
        return 0U;
    }

    for (size_t i = 0U; i < len; i++) {
        ck8 ^= octets[i];
    }

    const uint8_t b0 = (uint8_t) (ck8 & 0x01U);
    const uint8_t b1 = (uint8_t) ((ck8 >> 1U) & 0x01U);
    const uint8_t b2 = (uint8_t) ((ck8 >> 2U) & 0x01U);
    const uint8_t b3 = (uint8_t) ((ck8 >> 3U) & 0x01U);
    const uint8_t b4 = (uint8_t) ((ck8 >> 4U) & 0x01U);
    const uint8_t b5 = (uint8_t) ((ck8 >> 5U) & 0x01U);
    const uint8_t b6 = (uint8_t) ((ck8 >> 6U) & 0x01U);
    const uint8_t b7 = (uint8_t) ((ck8 >> 7U) & 0x01U);

    uint8_t ck6 = 0U;
    ck6 |= (uint8_t) ((b7 ^ b5 ^ b3 ^ b1) << 5U);
    ck6 |= (uint8_t) ((b6 ^ b4 ^ b2 ^ b0) << 4U);
    ck6 |= (uint8_t) ((b7 ^ b6) << 3U);
    ck6 |= (uint8_t) ((b5 ^ b4) << 2U);
    ck6 |= (uint8_t) ((b3 ^ b2) << 1U);
    ck6 |= (uint8_t) (b1 ^ b0);

    return ck6;
}
