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
    """Decode a Type-0 read reply [OD][CKS] and verify the A.1.6 checksum."""
    od = 0x11
    ck = checksum6(bytes([od, 0x00]))
    resp = DeviceResponse(bytes([od, ck]), od_len=1)
    assert resp.checksum_ok is True
    assert resp.od == od
    assert resp.has_event() is False
    assert resp.pd_valid is True


def test_type0_response_cks_flags():
    """The Event and PD-invalid flags live in the CKS octet (A.1.5)."""
    od = 0x11
    ck = 0x80 | checksum6(bytes([od, 0x00]))
    assert DeviceResponse(bytes([od, ck]), od_len=1).has_event() is True
    ck = 0x40 | checksum6(bytes([od, 0x00]))
    assert DeviceResponse(bytes([od, ck]), od_len=1).pd_valid is False


def test_type0_response_checksum_invalid():
    """A tampered checksum must be reported invalid."""
    od = 0x11
    ck = (checksum6(bytes([od, 0x00])) + 1) & 0x3F
    resp = DeviceResponse(bytes([od, ck]), od_len=1)
    assert resp.checksum_ok is False


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
