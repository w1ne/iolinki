#!/usr/bin/env python3
"""
Test Virtual Master basic functionality.
"""

import sys
sys.path.insert(0, '.')

from virtual_master.master import VirtualMaster
from virtual_master.protocol import MSequenceGenerator

def test_virtual_master():
    print("=== Testing Virtual Master ===\n")
    
    # Test M-sequence generation
    print("1. Testing M-sequence generation...")
    gen = MSequenceGenerator()
    
    wakeup = gen.generate_wakeup()
    print(f"   Wake-up frame: {wakeup.hex()}")
    assert len(wakeup) == 3, "Wake-up should be 3 bytes"
    assert wakeup[0] == 0x95, "Wake-up MC should be 0x95"
    print("   ✓ Wake-up frame correct")
    
    idle = gen.generate_idle()
    print(f"   Idle frame: {idle.hex()}")
    assert len(idle) == 3, "Idle should be 3 bytes"
    assert idle[0] == 0x00, "Idle MC should be 0x00"
    print("   ✓ Idle frame correct")
    
    # Test Virtual Master creation
    print("\n2. Testing Virtual Master creation...")
    master = VirtualMaster()
    tty = master.get_device_tty()
    print(f"   Device TTY: {tty}")
    assert tty.startswith('/dev/pts/') or tty.startswith('/dev/tty'), f"Invalid TTY: {tty}"
    print("   ✓ Virtual UART created")
    
    # Test frame sending (no device connected, just verify no crash)
    print("\n3. Testing frame transmission...")
    try:
        master.send_wakeup()
        print("   ✓ Wake-up sent successfully")
    except Exception as e:
        print(f"   ✗ Failed to send wake-up: {e}")
        return 1
    
    master.close()
    print("\n[SUCCESS] All Virtual Master tests passed!")
    print(f"\nTo test with real Device, run:")
    print(f"  ./build/examples/host_demo/host_demo {tty}")
    
    return 0

if __name__ == "__main__":
    sys.exit(test_virtual_master())
