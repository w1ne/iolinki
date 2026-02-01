#!/usr/bin/env python3
"""
Manual test of CRC calculation without pytest.
"""

import sys
sys.path.insert(0, '.')

from virtual_master.crc import calculate_crc6, calculate_checksum_type0

def test_crc():
    print("=== Testing CRC Calculation ===\n")
    
    # Test wake-up checksum
    wakeup_ck = calculate_checksum_type0(0x95, 0x00)
    print(f"✓ Wake-up (0x95, 0x00) CRC: 0x{wakeup_ck:02X}")
    
    # Test idle checksum
    idle_ck = calculate_checksum_type0(0x00, 0x00)
    print(f"✓ Idle (0x00, 0x00) CRC: 0x{idle_ck:02X}")
    
    # Test consistency
    ck1 = calculate_crc6(b'\xAB\xCD\xEF')
    ck2 = calculate_crc6(b'\xAB\xCD\xEF')
    assert ck1 == ck2, "CRC not consistent!"
    print(f"✓ Consistency test passed: 0x{ck1:02X}")
    
    # Test 6-bit range
    assert 0 <= wakeup_ck <= 63, "CRC out of 6-bit range!"
    assert 0 <= idle_ck <= 63, "CRC out of 6-bit range!"
    print(f"✓ All CRCs in valid 6-bit range (0-63)")
    
    print("\n[SUCCESS] All CRC tests passed!")
    return 0

if __name__ == "__main__":
    sys.exit(test_crc())
