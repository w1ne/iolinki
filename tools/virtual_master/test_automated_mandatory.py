#!/usr/bin/env python3
import sys
import time
import subprocess
import os
import signal

# Add current dir to path for imports
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from virtual_master.master import VirtualMaster

def run_test():
    print("=" * 60)
    print("Automated Mandatory ISDU Indices Integration Test")
    print("=" * 60)

    # 1. Start Virtual Master
    master = VirtualMaster()
    device_tty = master.get_device_tty()
    print(f"[INFO] Virtual Master started, Device TTY: {device_tty}")

    # 2. Start Device (host_demo)
    demo_bin = os.environ.get("IOLINK_DEVICE_PATH", "./build/examples/host_demo/host_demo")
    if not os.path.exists(demo_bin):
        # Try finding in other possible locations or build it
        print(f"[ERROR] {demo_bin} not found. Build the project first.")
        master.close()
        return 1

    print(f"[INFO] Starting Device: {demo_bin}")
    device_proc = subprocess.Popen([demo_bin, device_tty], 
                                  stdout=sys.stdout, 
                                  stderr=sys.stderr,
                                  preexec_fn=os.setsid)

    def cleanup():
        print("[INFO] Cleaning up...")
        os.killpg(os.getpgid(device_proc.pid), signal.SIGTERM)
        master.close()

    try:
        # 3. Wait for Device to initialize
        time.sleep(1)

        # 4. Startup Sequence
        print("-" * 60)
        print("PHASE 1: Startup")
        if not master.run_startup_sequence():
            print("❌ Startup failed")
            cleanup()
            return 1
        print("✅ Startup successful")

        # 5. Test Mandatory Indices
        print("-" * 60)
        print("PHASE 2: Mandatory Indices READ")
        
        indices = [
            (0x0010, "Vendor Name"),
            (0x0011, "Vendor Text"),
            (0x0012, "Product Name"),
            (0x0013, "Product ID"),
            (0x0014, "Product Text"),
            (0x0015, "Serial Number"),
            (0x0016, "Hardware Revision"),
            (0x0017, "Firmware Revision"),
            (0x0018, "Application Tag"),
            (0x001E, "Revision ID"),
            (0x0024, "Min Cycle Time"),
        ]

        results = {}
        for idx, name in indices:
            print(f"Reading 0x{idx:04X} ({name})...", end=' ')
            data = master.read_isdu(index=idx)
            if data:
                try:
                    val = data.decode('ascii', errors='ignore').strip('\x00')
                    print(f"✅ '{val}'")
                    results[idx] = val
                except:
                    print(f"✅ (hex) {data.hex()}")
                    results[idx] = data.hex()
            else:
                print("❌ FAILED")
                results[idx] = None

        # 6. Test Write (Application Tag)
        print("-" * 60)
        print("PHASE 3: Mandatory Indices WRITE")
        new_tag = "NewAppTag123"
        print(f"Writing 0x0018 (Application Tag) = '{new_tag}'...")
        if master.write_isdu(index=0x0018, subindex=0, data=new_tag.encode()):
            print("✅ Write sent")
            # Verify write
            time.sleep(0.5)
            print("Verifying write...", end=' ')
            read_back = master.read_isdu(index=0x0018)
            if read_back and read_back.decode('ascii', errors='ignore').strip('\x00') == new_tag:
                 print(f"✅ Match: '{read_back.decode()}'")
            else:
                 print(f"❌ Mismatch or failed: {read_back}")
        else:
            print("❌ Write failed")

        print("-" * 60)
        print("TEST SUMMARY")
        passed = sum(1 for v in results.values() if v is not None)
        total = len(indices)
        print(f"Passed: {passed}/{total}")
        
        if passed == total:
            print("\n[SUCCESS] All mandatory indices verified!")
            cleanup()
            return 0
        else:
            print(f"\n[FAILURE] {total - passed} indices failed verification.")
            cleanup()
            return 1

    except Exception as e:
        print(f"\n[ERROR] Test crashed: {e}")
        import traceback
        traceback.print_exc()
        cleanup()
        return 1

if __name__ == "__main__":
    sys.exit(run_test())
