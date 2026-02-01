#!/usr/bin/env python3
"""
Automated integration test for Docker environment.

Runs Virtual Master and Device in background, performs tests, then cleans up.
"""

import sys
import time
import subprocess
import signal
import os

sys.path.insert(0, '.')

from virtual_master.master import VirtualMaster


def run_device_in_background(tty_path):
    """Start Device process in background."""
    device_path = "../../build/examples/host_demo/host_demo"
    
    if not os.path.exists(device_path):
        print(f"[ERROR] Device not found: {device_path}")
        print("[HINT] Build the project first: cmake -B build && cmake --build build")
        return None
    
    try:
        # Start Device process
        proc = subprocess.Popen(
            [device_path, tty_path],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True
        )
        print(f"[INFO] Device started (PID: {proc.pid})")
        time.sleep(0.5)  # Give Device time to initialize
        return proc
    except Exception as e:
        print(f"[ERROR] Failed to start Device: {e}")
        return None


def test_automated():
    """Automated test for CI/CD."""
    print("=" * 60)
    print("Automated Virtual Master + Device Integration Test")
    print("=" * 60)
    
    device_proc = None
    
    try:
        # Create Virtual Master
        master = VirtualMaster()
        device_tty = master.get_device_tty()
        print(f"\n[INFO] Virtual Master ready (TTY: {device_tty})")
        
        # Start Device in background
        device_proc = run_device_in_background(device_tty)
        if not device_proc:
            return 1
        
        # Test 1: Startup
        print("\n[TEST 1] Startup Sequence")
        if not master.run_startup_sequence():
            print("❌ Startup failed")
            return 1
        print("✅ Startup successful")
        
        # Test 2: Communication cycles
        print("\n[TEST 2] Communication Cycles")
        success_count = 0
        for i in range(5):
            response = master.run_cycle()
            if response.valid:
                success_count += 1
            time.sleep(0.01)
        
        if success_count >= 3:
            print(f"✅ Communication cycles: {success_count}/5 successful")
        else:
            print(f"❌ Communication cycles: only {success_count}/5 successful")
            return 1
        
        # Test 3: CRC validation
        print("\n[TEST 3] CRC Validation")
        from virtual_master.crc import calculate_checksum_type0
        wakeup_ck = calculate_checksum_type0(0x95, 0x00)
        if wakeup_ck == 0x1D:
            print(f"✅ CRC calculation correct (0x{wakeup_ck:02X})")
        else:
            print(f"❌ CRC calculation wrong (expected 0x1D, got 0x{wakeup_ck:02X})")
            return 1
        
        print("\n" + "=" * 60)
        print("✅ ALL TESTS PASSED")
        print("=" * 60)
        
        master.close()
        return 0
        
    except Exception as e:
        print(f"\n❌ Test failed with exception: {e}")
        import traceback
        traceback.print_exc()
        return 1
        
    finally:
        # Clean up Device process
        if device_proc:
            print(f"\n[INFO] Stopping Device (PID: {device_proc.pid})")
            device_proc.terminate()
            try:
                device_proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                device_proc.kill()


if __name__ == "__main__":
    sys.exit(test_automated())
