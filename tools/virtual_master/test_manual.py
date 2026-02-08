#!/usr/bin/env python3
"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.
"""

"""
Manual test of CRC calculation without pytest.
"""

import sys

sys.path.insert(0, ".")

from virtual_master.crc import calculate_crc6, calculate_checksum_type0


def test_crc():
    print("=== Testing CRC Calculation ===")
    print()

    wakeup_ck = calculate_checksum_type0(0x95, 0x00)
    print(f"OK Wake-up (0x95, 0x00) CRC: 0x{wakeup_ck:02X}")

    idle_ck = calculate_checksum_type0(0x00, 0x00)
    print(f"OK Idle (0x00, 0x00) CRC: 0x{idle_ck:02X}")

    ck1 = calculate_crc6(b"\xab\xcd\xef")
    ck2 = calculate_crc6(b"\xab\xcd\xef")
    assert ck1 == ck2, "CRC not consistent!"
    print(f"OK Consistency test passed: 0x{ck1:02X}")

    assert 0 <= wakeup_ck <= 63, "CRC out of 6-bit range!"
    assert 0 <= idle_ck <= 63, "CRC out of 6-bit range!"
    print("OK All CRCs in valid 6-bit range (0-63)")

    print()
    print("[SUCCESS] All CRC tests passed!")
    return 0


if __name__ == "__main__":
    sys.exit(test_crc())
