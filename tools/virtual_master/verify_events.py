#!/usr/bin/env python3
import sys
import time
import os
import subprocess

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from virtual_master.master import VirtualMaster


def verify_task3():
    master = VirtualMaster()
    device_tty = master.get_device_tty()
    demo_bin = os.environ.get(
        "IOLINK_DEVICE_PATH", "./build_linux/examples/host_demo/host_demo"
    )

    print("=== Task 3 Verification: Error Event Reporting ===")

    # Start device
    process = subprocess.Popen(
        [demo_bin, device_tty, "0", "0"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    time.sleep(0.5)

    try:
        if not master.run_startup_sequence():
            print("❌ Startup failed")
            return

        print("✅ Startup successful")

        # 1. Trigger CRC error by sending a bad frame
        # We'll use a direct UART write of junk to trigger a framing or CRC error
        print("Sending junk to trigger error...")
        master.uart.send_bytes(b"\xff\xff\xff\xff")
        time.sleep(0.2)

        # 2. Read Index 0x1C (Detailed Device Status)
        print("Reading Index 0x001C (Detailed Device Status)...")
        resp = master.read_isdu(index=0x001C, subindex=0x00)
        if resp:
            print(f"✅ Index 0x1C Response: {resp.hex()}")
            for i in range(0, len(resp), 3):
                if i + 2 < len(resp):
                    qualifier = resp[i]
                    code = (resp[i + 1] << 8) | resp[i + 2]
                    print(f"   Event: Qualifier=0x{qualifier:02X}, Code=0x{code:04X}")
        else:
            print("❌ Index 0x1C Read failed or empty")

        # 3. Read Index 0x001B (Device Status)
        print("Reading Index 0x001B (Device Status)...")
        resp = master.read_isdu(index=0x001B, subindex=0x00)
        if resp:
            print(f"✅ Index 0x001B Response: {resp[0]} (0=OK, 3=Failure)")
        else:
            print("❌ Index 0x001B Read failed")

    finally:
        process.terminate()
        process.wait()
        master.close()


if __name__ == "__main__":
    verify_task3()
