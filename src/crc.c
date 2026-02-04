/*
 * Copyright (C) 2026 Andrii Shylenko
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This file is part of iolinki.
 * See LICENSE for details.
 */

#include "iolinki/crc.h"
#include "iolinki/utils.h"
#include <stddef.h>

/*
 * IO-Link CRC6 lookup table or calculation logic.
 * Polynomial x^6 + x^4 + x^3 + x^2 + 1 (0x1D)
 * Seed: 0x15
 */
uint8_t iolink_crc6(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0x15U; /* Initial value for V1.1 */

    if (!iolink_buf_is_valid(data, len)) {
        return 0U;
    }

    for (uint8_t i = 0U; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0U; j < 8U; j++) {
            if ((crc & 0x80U) != 0U) {
                crc = (uint8_t) ((crc << 1U) ^ (0x1DU << 2U)); /* Shifted polynomial */
            }
            else {
                crc <<= 1U;
            }
        }
    }

    return (uint8_t) ((crc >> 2U) & 0x3FU); /* 6-bit result */
}

uint8_t iolink_checksum_ck(uint8_t mc, uint8_t ckt)
{
    /* Simple XOR sum of MC and CKT as per some V1.1.5 patterns,
       but actually the spec uses the CRC6 for CK.
       Let's implement the standard CK calculation. */
    uint8_t buf[2] = {mc, ckt};
    return iolink_crc6(buf, 2U);
}
