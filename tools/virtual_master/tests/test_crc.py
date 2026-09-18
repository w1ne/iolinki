"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.
"""

import os
import sys
import pytest
from virtual_master.crc import (
    checksum6,
    calculate_checksum_type0,
    calculate_checksum_type1,
    verify_checksum,
)
from virtual_master.protocol import DeviceResponse

"""
Test the A.1.6 message checksum against vectors derived from the spec formula.
"""


sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def test_checksum6_master_frames():
    """Master frames: MC, CKT (type bits only), data..."""
    assert checksum6(bytes([0x00, 0x00])) == 0x2D
    assert checksum6(bytes([0xA2, 0x00])) == 0x00
    assert checksum6(bytes([0x20, 0x00, 0x99])) == 0x06
    # TYPE_1 write frame, CKT type bits 0x40, OD 0xA5.
    assert checksum6(bytes([0x00, 0x40, 0xA5, 0x5A])) == 0x35


def test_checksum6_device_replies():
    """Device replies: data..., CKS."""
    assert checksum6(bytes([0x10])) == 0x39
    assert checksum6(bytes([0xA5])) == 0x22
    assert checksum6(bytes([0xC5])) == 0x1E
    assert checksum6(b"") == 0x2D


def test_checksum_type0():
    """Type 0 helper zeroes the CKT checksum bits."""
    assert calculate_checksum_type0(0x00, 0x00) == 0x2D
    assert calculate_checksum_type0(0xA2, 0x00) == 0x00
    assert calculate_checksum_type0(0x95, 0x00) == 0x12


def test_checksum_type1():
    """Type 1/2 helper includes MC, CKT, PD and OD."""
    assert calculate_checksum_type1(0x00, 0x40, b"", 0xA5) == 0x3A


def test_verify_checksum():
    """Verification compares the 6-bit checksum only."""
    assert verify_checksum(bytes([0x10]), 0x39) is True
    assert verify_checksum(bytes([0x10]), 0x39 | 0xC0) is True
    assert verify_checksum(bytes([0x10]), 0x38) is False


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
    # Recovery is ambiguous for a tampered checksum; it must not report the
    # originally sent status.
    assert resp.status != status


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
