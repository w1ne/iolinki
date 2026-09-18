"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.
"""

"""
Message checksum for IO-Link M-sequences.

Implements the A.1.6 message checksum (XOR with seed 0x52, compressed from
8 to 6 bits by equations (A.1)). Mirrors iolinki/src/crc.c.
"""

M_SEQUENCE_TYPE_MASK = 0xC0
CHECKSUM_MASK = 0x3F
CHECKSUM_SEED = 0x52


def checksum6(octets: bytes) -> int:
    """
    Calculate the IO-Link A.1.6 message checksum.

    Every octet is XOR processed with a seed of 0x52; the 8-bit result is
    compressed to 6 bits using the equations in (A.1). The checksum/type octet
    must be passed with its checksum bits (0-5) set to zero.

    Args:
        octets: Message octets with the checksum field zeroed.

    Returns:
        6-bit checksum value (0-63)
    """
    ck8 = CHECKSUM_SEED
    for octet in octets:
        ck8 ^= octet

    b = [(ck8 >> i) & 1 for i in range(8)]
    return (
        ((b[7] ^ b[5] ^ b[3] ^ b[1]) << 5)
        | ((b[6] ^ b[4] ^ b[2] ^ b[0]) << 4)
        | ((b[7] ^ b[6]) << 3)
        | ((b[5] ^ b[4]) << 2)
        | ((b[3] ^ b[2]) << 1)
        | (b[1] ^ b[0])
    )


def calculate_checksum_type0(mc: int, ckt: int = 0x00) -> int:
    """
    Calculate checksum for a Type 0 M-sequence (MC + CKT).

    Args:
        mc: Master Command byte
        ckt: Command/Key/Type byte; its checksum bits are zeroed

    Returns:
        6-bit checksum
    """
    return checksum6(bytes([mc, ckt & M_SEQUENCE_TYPE_MASK]))


def calculate_checksum_type1(mc: int, ckt: int, pd: bytes, od: int, od2: int = None) -> int:
    """
    Calculate checksum for a Type 1/2 M-sequence (MC + CKT + PD + OD).

    Args:
        mc: Master Command byte
        ckt: Command/Key/Type byte; its checksum bits are zeroed
        pd: Process Data bytes
        od: On-request Data byte (first byte)
        od2: Second OD byte (for Type 2 only, optional)

    Returns:
        6-bit checksum
    """
    data = bytes([mc, ckt & M_SEQUENCE_TYPE_MASK]) + pd + bytes([od])
    if od2 is not None:
        data += bytes([od2])
    return checksum6(data)


def verify_checksum(frame: bytes, expected_ck: int) -> bool:
    """
    Verify the checksum of a received frame.

    Args:
        frame: Frame data without the CK byte; the CKT/CKS octet (if present)
            must already have its checksum bits zeroed
        expected_ck: Received checksum octet (only bits 0-5 are compared)

    Returns:
        True if the checksum matches
    """
    return checksum6(frame) == (expected_ck & CHECKSUM_MASK)
