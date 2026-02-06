#!/usr/bin/env python3
"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.
"""

"""
Test Virtual Master basic functionality.
"""

import sys

sys.path.insert(0, ".")

from virtual_master.master import VirtualMaster
from virtual_master.protocol import MSequenceGenerator


def test_virtual_master():
    print("=== Testing Virtual Master ===")
    print()

    print("1. Testing M-sequence generation...")
    gen = MSequenceGenerator()

    wakeup = gen.generate_wakeup()
    print(f"   Wake-up frame: {wakeup.hex()}")
    assert len(wakeup) == 2, "Wake-up should be 2 bytes"
    assert wakeup[0] == 0x95, "Wake-up MC should be 0x95"
    print("   ✓ Wake-up frame correct")

    idle = gen.generate_idle()
    print(f"   Idle frame: {idle.hex()}")
    assert len(idle) == 2, "Idle should be 2 bytes"
    assert idle[0] == 0x00, "Idle MC should be 0x00"
    print("   ✓ Idle frame correct")

    print()
    print("2. Testing Virtual Master creation...")
    master = VirtualMaster()
    tty = master.get_device_tty()
    print(f"   Device TTY: {tty}")
    assert tty.startswith("/dev/pts/") or tty.startswith("/dev/tty"), (
        f"Invalid TTY: {tty}"
    )
    print("   ✓ Virtual UART created")

    print()
    print("3. Testing frame transmission...")
    try:
        master.send_wakeup()
        print("   ✓ Wake-up sent successfully")
    except Exception as e:
        print(f"   ✗ Failed to send wake-up: {e}")
        return 1

    master.close()
    print()
    print("[SUCCESS] All Virtual Master tests passed!")
    print("To test with real Device, run:")
    print(f"  ./build/examples/host_demo/host_demo {tty}")

    return 0


if __name__ == "__main__":
    sys.exit(test_virtual_master())
