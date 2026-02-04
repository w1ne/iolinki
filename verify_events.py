#!/usr/bin/env python3
"""
IO-Link Event Reporting Verification Script
Uses Virtual Master to inject errors and verify diagnostic event reporting.
"""

import sys
import os
import time

# Add tools/virtual_master to path
sys.path.append(os.path.join(os.path.dirname(__file__), 'tools'))
from virtual_master.master import VirtualMaster

def main():
    print("=" * 60)
    print("🚀 IO-Link Event Reporting Verification")
    print("=" * 60)

    master = VirtualMaster()
    demo_bin = os.environ.get("IOLINK_DEVICE_PATH", "./build_linux/examples/host_demo/host_demo")
    
    import subprocess
    process = subprocess.Popen([demo_bin, master.get_device_tty(), "1", "2"],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.5)

    try:
        # 1. Startup
        print("[1/4] Establishing communication...")
        master.run_startup_sequence()
        master.m_seq_type = 2
        master.go_to_operate()
        time.sleep(0.1)

        # 2. Inject CRC Errors (3 consecutive to trigger Fallback & Event)
        print("[2/4] Injecting 3 CRC errors to trigger fallback and events...")
        if hasattr(master, 'run_cycle_bad_crc'):
            for _ in range(3):
                master.run_cycle_bad_crc(pd_out=b'\x00\x00')
                time.sleep(0.02)
        else:
            print("   ⚠️  Virtual Master doesn't support CRC injection, skipping inject.")
            return

        # 3. Recover
        print("[3/4] Recovering via new startup...")
        time.sleep(0.2)
        master.m_seq_type = 0
        master.send_wakeup()
        time.sleep(0.1)

        # 4. Verify Events
        print("[4/4] Verifying event reporting...")
        
        # Check Device Status (Index 0x1B)
        status = master.read_isdu(index=0x001B, subindex=0x00)
        if status and status[0] > 0:
            print(f"   ✅ Device Status: {status[0]} (Event Present)")
        else:
            print(f"   ❌ Device Status: {status[0] if status else 'None'} (Expected > 0)")
            sys.exit(1)

        # Check Detailed Status (Index 0x1C)
        detailed = master.read_isdu(index=0x001C, subindex=0x00)
        if detailed:
            found_crc = False
            for i in range(0, len(detailed), 3):
                code = (detailed[i+1] << 8) | detailed[i+2]
                if code == 0x1801: # IOLINK_EVENT_COMM_CRC
                    found_crc = True
                    break
            
            if found_crc:
                print("   ✅ Found CRC Event (0x1801) in Detailed Device Status")
            else:
                print(f"   ❌ CRC Event (0x1801) not found in {detailed.hex()}")
                sys.exit(1)
        else:
            print("   ❌ Failed to read Detailed Device Status")
            sys.exit(1)

        print("\n✨ Event Reporting Verification PASSED")

    finally:
        process.terminate()
        process.wait()
        master.close()

if __name__ == "__main__":
    main()
