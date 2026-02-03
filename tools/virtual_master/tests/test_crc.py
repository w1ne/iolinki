"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.
"""

"""
Test CRC calculation against known values.
"""

import pytest
from virtual_master.crc import calculate_crc6, calculate_checksum_type0


def test_crc6_empty():
    """Test CRC of empty data."""
    result = calculate_crc6(b'')
    assert 0 <= result <= 63  # Must be 6-bit value


def test_crc6_single_byte():
    """Test CRC of single byte."""
    result = calculate_crc6(b'\x00')
    assert result == 0x15  # Initial value XOR 0x00


def test_checksum_type0_wakeup():
    """Test checksum for wake-up command (0x95)."""
    result = calculate_checksum_type0(0x95, 0x00)
    assert 0 <= result <= 63
    print(f"Wake-up checksum: 0x{result:02X}")


def test_checksum_type0_idle():
    """Test checksum for idle command (0x00)."""
    result = calculate_checksum_type0(0x00, 0x00)
    assert 0 <= result <= 63
    print(f"Idle checksum: 0x{result:02X}")


def test_crc6_consistency():
    """Test that same input produces same output."""
    data = b'\xAB\xCD\xEF'
    result1 = calculate_crc6(data)
    result2 = calculate_crc6(data)
    assert result1 == result2


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
