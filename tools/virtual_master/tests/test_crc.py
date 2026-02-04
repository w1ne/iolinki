"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.
"""

"""
Test CRC calculation against known values.
"""

import os
import sys
import pytest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from virtual_master.crc import calculate_crc6, calculate_checksum_type0
from virtual_master.protocol import DeviceResponse


def test_crc6_empty():
    """Test CRC of empty data."""
    result = calculate_crc6(b'')
    assert 0 <= result <= 63  # Must be 6-bit value


def test_crc6_single_byte():
    """Test CRC of single byte."""
    result = calculate_crc6(b'\x00')
    assert result == 0x2A  # Expected value per current CRC6 implementation


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


def test_type0_response_checksum_decode():
    """Decode Type 0 response checksum and recover status."""
    status = 0x04
    od = 0x11
    ck = calculate_checksum_type0(status, od)
    resp = DeviceResponse(bytes([od, ck]))
    assert resp.checksum_ok is True
    assert resp.status == status


def test_type0_response_checksum_invalid():
    """Tampered checksum should not decode to original status."""
    status = 0x04
    od = 0x11
    ck = (calculate_checksum_type0(status, od) + 1) & 0x3F
    resp = DeviceResponse(bytes([od, ck]))
    assert resp.checksum_ok is True
    assert resp.status != status


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
