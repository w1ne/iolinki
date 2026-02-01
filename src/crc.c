#include "iolinki/crc.h"

/*
 * IO-Link CRC6 lookup table or calculation logic.
 * Polynomial x^6 + x^4 + x^3 + x^2 + 1 (0x1D)
 * Seed: 0x15
 */
uint8_t iolink_crc6(const uint8_t *data, uint8_t len)
{
    uint8_t crc = 0x15; /* Initial value for V1.1 */

    for (uint8_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ (0x1D << 2); /* Shifted polynomial */
            } else {
                crc <<= 1;
            }
        }
    }

    return (crc >> 2) & 0x3F; /* 6-bit result */
}

uint8_t iolink_checksum_ck(uint8_t mc, uint8_t ckt)
{
    /* Simple XOR sum of MC and CKT as per some V1.1.5 patterns, 
       but actually the spec uses the CRC6 for CK. 
       Let's implement the standard CK calculation. */
    uint8_t buf[2] = {mc, ckt};
    return iolink_crc6(buf, 2);
}
