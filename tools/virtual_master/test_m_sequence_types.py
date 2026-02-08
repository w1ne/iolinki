#!/usr/bin/env python3
"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.
"""

"""
Test M-sequence types 1_1, 1_2, 2_1, 2_2 with the Virtual Master.
"""

import sys
import time
from virtual_master import VirtualMaster, MSequenceType


def test_type_1_1():
    """Test Type 1_1: PD only, 1-byte OD"""
    print("\n=== Testing M-Sequence Type 1_1 (PD only, 1-byte OD) ===")

    master = VirtualMaster(m_seq_type=MSequenceType.TYPE_1_1, pd_in_len=2, pd_out_len=2)

    print(f"Device TTY: {master.get_device_tty()}")
    print("Waiting for device connection...")
    time.sleep(2)

    if not master.run_startup_sequence():
        print("❌ Startup failed")
        return False

    master.go_to_operate()
    time.sleep(0.1)

    print("\nRunning cycles...")
    for i in range(5):
        pd_out = bytes([0x11 + i, 0x22 + i])
        resp = master.run_cycle(pd_out=pd_out, od_req=0x00)

        if resp.valid:
            print(
                f"Cycle {i + 1}: PD_Out={pd_out.hex()}, PD_In={resp.payload.hex()}, OD=0x{resp.od:02X}"
            )
        else:
            print(f"❌ Cycle {i + 1} failed")
            return False

    master.close()
    print("✅ Type 1_1 test passed")
    return True


def test_type_1_2():
    """Test Type 1_2: PD + ISDU, 1-byte OD"""
    print("\n=== Testing M-Sequence Type 1_2 (PD + ISDU, 1-byte OD) ===")

    master = VirtualMaster(m_seq_type=MSequenceType.TYPE_1_2, pd_in_len=1, pd_out_len=1)

    print(f"Device TTY: {master.get_device_tty()}")
    print("Waiting for device connection...")
    time.sleep(2)

    if not master.run_startup_sequence():
        print("❌ Startup failed")
        return False

    master.go_to_operate()
    time.sleep(0.1)

    print("\nReading ISDU Index 0x000A (Vendor ID)...")
    vendor_id_data = master.read_isdu(0x000A)

    if vendor_id_data:
        vendor_id = (vendor_id_data[0] << 8) | vendor_id_data[1]
        print(f"✅ Vendor ID: 0x{vendor_id:04X}")
    else:
        print("❌ ISDU read failed")
        return False

    master.close()
    print("✅ Type 1_2 test passed")
    return True


def test_type_2_1():
    """Test Type 2_1: PD only, 2-byte OD"""
    print("\n=== Testing M-Sequence Type 2_1 (PD only, 2-byte OD) ===")

    master = VirtualMaster(m_seq_type=MSequenceType.TYPE_2_1, pd_in_len=1, pd_out_len=1)

    print(f"Device TTY: {master.get_device_tty()}")
    print(f"OD Length: {master.od_len} bytes")
    print("Waiting for device connection...")
    time.sleep(2)

    if not master.run_startup_sequence():
        print("❌ Startup failed")
        return False

    master.go_to_operate()
    time.sleep(0.1)

    print("\nRunning cycles...")
    for i in range(5):
        pd_out = bytes([0x33 + i])
        resp = master.run_cycle(pd_out=pd_out, od_req=0x00)

        if resp.valid:
            od2_str = f", OD2=0x{resp.od2:02X}" if resp.od2 is not None else ""
            print(
                f"Cycle {i + 1}: PD_Out={pd_out.hex()}, PD_In={resp.payload.hex()}, OD=0x{resp.od:02X}{od2_str}"
            )
        else:
            print(f"❌ Cycle {i + 1} failed")
            return False

    master.close()
    print("✅ Type 2_1 test passed")
    return True


def test_type_2_2():
    """Test Type 2_2: PD + ISDU, 2-byte OD"""
    print("\n=== Testing M-Sequence Type 2_2 (PD + ISDU, 2-byte OD) ===")

    master = VirtualMaster(m_seq_type=MSequenceType.TYPE_2_2, pd_in_len=2, pd_out_len=2)

    print(f"Device TTY: {master.get_device_tty()}")
    print(f"OD Length: {master.od_len} bytes")
    print("Waiting for device connection...")
    time.sleep(2)

    if not master.run_startup_sequence():
        print("❌ Startup failed")
        return False

    master.go_to_operate()
    time.sleep(0.1)

    print("\nReading ISDU Index 0x000B (Device ID)...")
    device_id_data = master.read_isdu(0x000B)

    if device_id_data and len(device_id_data) >= 4:
        device_id = (
            (device_id_data[0] << 24)
            | (device_id_data[1] << 16)
            | (device_id_data[2] << 8)
            | device_id_data[3]
        )
        print(f"✅ Device ID: 0x{device_id:08X}")
    else:
        print("❌ ISDU read failed")
        return False

    master.close()
    print("✅ Type 2_2 test passed")
    return True


def main():
    """Run all M-sequence type tests"""
    print("=" * 60)
    print("M-Sequence Types Test Suite")
    print("=" * 60)

    tests = [
        ("Type 1_1", test_type_1_1),
        ("Type 1_2", test_type_1_2),
        ("Type 2_1", test_type_2_1),
        ("Type 2_2", test_type_2_2),
    ]

    results = {}
    for name, test_func in tests:
        try:
            results[name] = test_func()
        except Exception as e:
            print(f"\n❌ {name} test crashed: {e}")
            results[name] = False

        time.sleep(1)  # Pause between tests

    print("\n" + "=" * 60)
    print("Test Summary")
    print("=" * 60)
    for name, passed in results.items():
        status = "✅ PASS" if passed else "❌ FAIL"
        print(f"{name}: {status}")

    all_passed = all(results.values())
    print("\n" + ("=" * 60))
    if all_passed:
        print("✅ All tests passed!")
        return 0
    else:
        print("❌ Some tests failed")
        return 1


if __name__ == "__main__":
    sys.exit(main())
