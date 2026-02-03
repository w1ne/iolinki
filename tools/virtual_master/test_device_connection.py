#!/usr/bin/env python3
"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.
"""

"""
Device connection test - verify Virtual Master can connect to iolinki device.
"""

import sys
import time
import subprocess
from pathlib import Path
from virtual_master import VirtualMaster, MSequenceType


def find_device_binary():
    """Find the device binary (host_demo or simple_device)"""
    build_dir = Path(__file__).parent.parent.parent / "build"
    
    # Try host_demo first
    host_demo = build_dir / "examples" / "host_demo" / "host_demo"
    if host_demo.exists():
        return host_demo
    
    # Try simple_device
    simple_device = build_dir / "examples" / "simple_device" / "simple_device"
    if simple_device.exists():
        return simple_device
    
    return None


def test_basic_connection():
    """Test basic connection with Type 0"""
    print("\n=== Test 1: Basic Connection (Type 0) ===")
    
    master = VirtualMaster(m_seq_type=0)
    device_tty = master.get_device_tty()
    print(f"Device TTY: {device_tty}")
    
    # Find device binary
    device_bin = find_device_binary()
    if not device_bin:
        print("❌ Device binary not found. Please build the project first.")
        return False
    
    print(f"Starting device: {device_bin}")
    
    # Start device in background
    device_proc = subprocess.Popen(
        [str(device_bin), device_tty],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )
    
    # Give device time to start
    time.sleep(1)
    
    try:
        # Run startup sequence
        if master.run_startup_sequence():
            print("✅ Connection established")
            
            # Try reading ISDU
            vendor_name = master.read_isdu(0x0010)
            if vendor_name:
                print(f"✅ Read Vendor Name: {vendor_name.decode('ascii', errors='ignore')}")
                return True
            else:
                print("⚠️  ISDU read failed")
                return False
        else:
            print("❌ Startup failed")
            return False
            
    finally:
        # Clean up
        device_proc.terminate()
        device_proc.wait(timeout=2)
        master.close()


def test_type_1_2_connection():
    """Test connection with Type 1_2 (PD + ISDU)"""
    print("\n=== Test 2: Type 1_2 Connection (PD + ISDU, 1-byte OD) ===")
    
    master = VirtualMaster(
        m_seq_type=MSequenceType.TYPE_1_2,
        pd_in_len=2,
        pd_out_len=2
    )
    device_tty = master.get_device_tty()
    print(f"Device TTY: {device_tty}")
    print(f"OD Length: {master.od_len} byte(s)")
    
    device_bin = find_device_binary()
    if not device_bin:
        print("❌ Device binary not found")
        return False
    
    # Start device
    device_proc = subprocess.Popen(
        [str(device_bin), device_tty],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )
    
    time.sleep(1)
    
    try:
        # Startup
        if not master.run_startup_sequence():
            print("❌ Startup failed")
            return False
        
        print("✅ Startup successful")
        
        # Transition to OPERATE
        master.go_to_operate()
        time.sleep(0.1)
        
        # Run cycles
        print("\nRunning PD exchange cycles...")
        for i in range(3):
            pd_out = bytes([0x10 + i, 0x20 + i])
            resp = master.run_cycle(pd_out=pd_out, od_req=0x00)
            
            if resp.valid:
                print(f"  Cycle {i+1}: PD_Out={pd_out.hex()}, PD_In={resp.payload.hex()}, OD=0x{resp.od:02X}")
            else:
                print(f"  ❌ Cycle {i+1} failed")
                return False
        
        # Read ISDU
        print("\nReading ISDU (Vendor ID)...")
        vendor_id_data = master.read_isdu(0x000A)
        if vendor_id_data and len(vendor_id_data) >= 2:
            vendor_id = (vendor_id_data[0] << 8) | vendor_id_data[1]
            print(f"✅ Vendor ID: 0x{vendor_id:04X}")
            return True
        else:
            print("❌ ISDU read failed")
            return False
            
    finally:
        device_proc.terminate()
        device_proc.wait(timeout=2)
        master.close()


def test_type_2_2_connection():
    """Test connection with Type 2_2 (PD + ISDU, 2-byte OD)"""
    print("\n=== Test 3: Type 2_2 Connection (PD + ISDU, 2-byte OD) ===")
    
    master = VirtualMaster(
        m_seq_type=MSequenceType.TYPE_2_2,
        pd_in_len=1,
        pd_out_len=1
    )
    device_tty = master.get_device_tty()
    print(f"Device TTY: {device_tty}")
    print(f"OD Length: {master.od_len} byte(s)")
    
    device_bin = find_device_binary()
    if not device_bin:
        print("❌ Device binary not found")
        return False
    
    # Start device
    device_proc = subprocess.Popen(
        [str(device_bin), device_tty],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )
    
    time.sleep(1)
    
    try:
        # Startup
        if not master.run_startup_sequence():
            print("❌ Startup failed")
            return False
        
        print("✅ Startup successful")
        
        # Transition to OPERATE
        master.go_to_operate()
        time.sleep(0.1)
        
        # Run cycles
        print("\nRunning PD exchange cycles...")
        for i in range(3):
            pd_out = bytes([0x30 + i])
            resp = master.run_cycle(pd_out=pd_out, od_req=0x00)
            
            if resp.valid:
                od2_str = f", OD2=0x{resp.od2:02X}" if resp.od2 is not None else ""
                print(f"  Cycle {i+1}: PD_Out={pd_out.hex()}, PD_In={resp.payload.hex()}, OD=0x{resp.od:02X}{od2_str}")
            else:
                print(f"  ❌ Cycle {i+1} failed")
                return False
        
        # Read ISDU
        print("\nReading ISDU (Device ID)...")
        device_id_data = master.read_isdu(0x000B)
        if device_id_data and len(device_id_data) >= 4:
            device_id = (device_id_data[0] << 24) | (device_id_data[1] << 16) | (device_id_data[2] << 8) | device_id_data[3]
            print(f"✅ Device ID: 0x{device_id:08X}")
            return True
        else:
            print("❌ ISDU read failed")
            return False
            
    finally:
        device_proc.terminate()
        device_proc.wait(timeout=2)
        master.close()


def main():
    """Run all connection tests"""
    print("=" * 60)
    print("Virtual Master - Device Connection Tests")
    print("=" * 60)
    
    # Check if device binary exists
    device_bin = find_device_binary()
    if not device_bin:
        print("\n❌ ERROR: Device binary not found!")
        print("Please build the project first:")
        print("  cd /home/andrii/Projects/iolinki")
        print("  cmake -B build -DCMAKE_BUILD_TYPE=Release")
        print("  cmake --build build")
        return 1
    
    print(f"\nUsing device binary: {device_bin}")
    
    tests = [
        ("Basic Connection (Type 0)", test_basic_connection),
        ("Type 1_2 Connection", test_type_1_2_connection),
        ("Type 2_2 Connection", test_type_2_2_connection),
    ]
    
    results = {}
    for name, test_func in tests:
        try:
            results[name] = test_func()
        except Exception as e:
            print(f"\n❌ {name} crashed: {e}")
            import traceback
            traceback.print_exc()
            results[name] = False
        
        time.sleep(1)  # Pause between tests
    
    # Summary
    print("\n" + "=" * 60)
    print("Test Summary")
    print("=" * 60)
    for name, passed in results.items():
        status = "✅ PASS" if passed else "❌ FAIL"
        print(f"{name}: {status}")
    
    all_passed = all(results.values())
    print("\n" + "=" * 60)
    if all_passed:
        print("✅ All connection tests passed!")
        return 0
    else:
        print("❌ Some tests failed")
        return 1



if __name__ == "__main__":
    sys.exit(main())
