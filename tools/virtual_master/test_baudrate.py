#!/usr/bin/env python3
"""
Test Baudrate Switching functionality.
"""

import sys
import time
import subprocess
import os
from virtual_master import VirtualMaster, MSequenceType

def test_baudrate_switching():
    """Test switching between different baudrates"""
    print("\n=== Test: Baudrate Switching ===")
    
    master = VirtualMaster()
    device_tty = master.get_device_tty()
    
    device_path = os.environ.get("IOLINK_DEVICE_PATH", "./build/examples/host_demo/host_demo")
    process = subprocess.Popen([device_path, device_tty], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    
    try:
        time.sleep(1) # Wait for device to start
        
        print(f"Device TTY: {device_tty}")
        print(f"Initial Baudrate: {master.baudrate}")
        
        # Startup
        if not master.run_startup_sequence():
            print("❌ Startup failed")
            return False
            
        master.go_to_operate()
        time.sleep(0.1)
        
        # Test COM1 (4.8 kbit/s)
        print("\nSwitching to COM1...")
        master.set_baudrate("COM1")
        for i in range(2):
            resp = master.run_cycle(pd_out=bytes([0x11, 0x22]))
            if resp.valid:
                 print(f"  COM1 Cycle {i+1} OK")
            else:
                 print(f"  ❌ COM1 Cycle {i+1} failed")
                 return False
                 
        # Test COM3 (230.4 kbit/s)
        print("\nSwitching to COM3...")
        master.set_baudrate("COM3")
        for i in range(2):
            resp = master.run_cycle(pd_out=bytes([0x33, 0x44]))
            if resp.valid:
                 print(f"  COM3 Cycle {i+1} OK")
            else:
                 print(f"  ❌ COM3 Cycle {i+1} failed")
                 return False

        # Test back to COM2 (38.4 kbit/s)
        print("\nSwitching back to COM2...")
        master.set_baudrate("COM2")
        for i in range(2):
            resp = master.run_cycle(pd_out=bytes([0x55, 0x66]))
            if resp.valid:
                 print(f"  COM2 Cycle {i+1} OK")
            else:
                 print(f"  ❌ COM2 Cycle {i+1} failed")
                 return False

        print("✅ Baudrate switching test passed")
        return True
    finally:
        if 'process' in locals():
            process.terminate()
        master.close()

if __name__ == "__main__":
    if test_baudrate_switching():
        sys.exit(0)
    else:
        sys.exit(1)
