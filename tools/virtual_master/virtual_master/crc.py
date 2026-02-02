"""
CRC calculation for IO-Link frames.

Implements the 6-bit CRC used in IO-Link Type 0 M-sequences.
Algorithm matches the C implementation in iolinki/src/crc.c
"""

def calculate_crc6(data: bytes) -> int:
    """
    Calculate 6-bit CRC for IO-Link frame.
    
    Polynomial: x^6 + x^4 + x^3 + x^2 + 1 (0x1D)
    Initial value: 0x15
    
    Matches iolinki/src/crc.c implementation.
    
    Args:
        data: Frame data bytes (MC, PD, OD, etc.)
        
    Returns:
        6-bit CRC value (0-63)
    """
    crc = 0x15  # Initial value for V1.1
    
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = (crc << 1) ^ (0x1D << 2)  # Shifted polynomial
            else:
                crc = crc << 1
            crc &= 0xFF
    
    # Return top 6 bits
    return (crc >> 2) & 0x3F


def calculate_checksum_type0(mc: int, ckt: int = 0x00) -> int:
    """
    Calculate checksum for Type 0 M-sequence (MC + CKT).
    
    Args:
        mc: Master Command byte
        ckt: Checksum Type (usually 0x00)
        
    Returns:
        6-bit checksum
    """
    return calculate_crc6(bytes([mc, ckt]))


def calculate_checksum_type1(mc: int, ckt: int, pd: bytes, od: int) -> int:
    """
    Calculate checksum for Type 1 M-sequence (MC + CKT + PD + OD).
    
    Args:
        mc: Master Command byte
        ckt: Command/Key/Type byte
        pd: Process Data bytes
        od: On-request Data byte (8-bit)
        
    Returns:
        6-bit checksum
    """
    data = bytes([mc, ckt]) + pd + bytes([od])
    return calculate_crc6(data)


def verify_checksum(frame: bytes, expected_ck: int) -> bool:
    """
    Verify checksum of received frame.
    
    Args:
        frame: Frame data (without CK byte)
        expected_ck: Expected checksum value
        
    Returns:
        True if checksum matches
    """
    calculated = calculate_crc6(frame)
    return calculated == expected_ck
