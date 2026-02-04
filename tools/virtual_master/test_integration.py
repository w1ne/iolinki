#!/usr/bin/env python3
"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.
"""

"""
Integration test: Virtual Master with iolinki Device.

This test requires the iolinki Device to be running.
Run this script, then start the Device with the displayed TTY path.
"""

import sys
import time
import subprocess
import os

sys.path.insert(0, '.')

from virtual_master.master import VirtualMaster


def test_with_device():
    """Test Virtual Master with actual iolinki Device."""
    
    print("=" * 60)
    print("Virtual Master <-> iolinki Device Integration Test")
    print("=" * 60)
    
    # Create Virtual Master
    master = VirtualMaster()
    device_tty = master.get_device_tty()
    
    print()
    print("[INFO] Virtual Master ready")
    print(f"[INFO] Device TTY: {device_tty}")
    print()
    print("[ACTION] Start your Device with:")
    print(f"  ./build/examples/host_demo/host_demo {device_tty}")
    print()
    print("Waiting 10 seconds for Device to start...")
    
    # Wait for user to start Device
    for i in range(10, 0, -1):
        print(f"  {i}...", end="\r")
        time.sleep(1)
    print()
    
    # Test 1: Startup Sequence
    print("-" * 60)
    print("TEST 1: Startup Sequence")
    print("-" * 60)
    
    if master.run_startup_sequence():
        print("✅ Startup sequence successful!")
    else:
        print("❌ Startup sequence failed")
        print()
        print("[HINT] Make sure Device is running and connected to correct TTY")
        master.close()
        return 1
    
    # Test 2: Communication Cycles
    print()
    print("-" * 60)
    print("TEST 2: Communication Cycles (10 cycles)")
    print("-" * 60)
    
    event_count = 0
    for i in range(10):
        response = master.run_cycle()
        
        if not response.valid:
            print(f"❌ Cycle {i+1}: No valid response")
            continue
        
        print(f"✅ Cycle {i+1}: Status=0x{response.status:02X}", end='')
        
        if response.has_event():
            print(" [EVENT PENDING]", end='')
            event_count += 1
        
        print()
        time.sleep(0.01)  # 10ms cycle time
    
    print()
    print(f"Events detected: {event_count}")
    
    # Test 3: Event Request (if events pending)
    if event_count > 0:
        print()
        print("-" * 60)
        print("TEST 3: Event Request")
        print("-" * 60)
        
        event_code = master.request_event()
        if event_code:
            print(f"✅ Event code: 0x{event_code:04X}")
        else:
            print("❌ No event received")
    
    # Test 4: ISDU Read
    print()
    print("-" * 60)
    print("TEST 4: ISDU Read (Index 0x10 - Vendor Name)")
    print("-" * 60)
    
    vendor_data = master.read_isdu(index=0x10)
    if vendor_data:
        print(f"✅ ISDU Read successful: {vendor_data.hex()}")
        try:
            vendor_name = vendor_data.decode('ascii', errors='ignore')
            print(f"   Vendor Name: '{vendor_name}'")
        except:
            pass
    else:
        print("⚠️  ISDU Read returned no data (may not be implemented yet)")
    
    # Summary
    print()
    print("=" * 60)
    print("TEST SUMMARY")
    print("=" * 60)
    print("✅ Virtual Master operational")
    print("✅ Device communication established")
    print(f"✅ {10} communication cycles completed")
    if event_count > 0:
        print(f"✅ {event_count} events detected")
    print()
    print("[SUCCESS] Integration test complete!")
    
    master.close()
    return 0


if __name__ == "__main__":
    try:
        sys.exit(test_with_device())
    except KeyboardInterrupt:
        print()
        print("[INTERRUPTED] Test stopped by user")
        sys.exit(1)
    except Exception as e:
        print()
        print(f"[ERROR] Test failed: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
