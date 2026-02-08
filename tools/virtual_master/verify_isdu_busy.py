#!/usr/bin/env python3
import sys
import time
import os
import subprocess

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from virtual_master.master import VirtualMaster


def verify_isdu_flow_control():
    master = VirtualMaster()
    device_tty = master.get_device_tty()
    demo_bin = os.environ.get(
        "IOLINK_DEVICE_PATH", "./build/examples/host_demo/host_demo"
    )

    print("=== Task 4 Verification: ISDU Flow Control ===")

    log_file = open("device.log", "w")
    # Start device
    process = subprocess.Popen(
        [demo_bin, device_tty, "0", "0"], stdout=log_file, stderr=log_file, text=True
    )
    time.sleep(0.5)

    try:
        if not master.run_startup_sequence():
            print("❌ Startup failed")
            return
        print("✅ Startup successful")

        # Test 1: Interrupted Segmented Write
        # We will start writing a large blob to Application Tag (Index 0x18)
        # But interrupt it with a Read of Vendor Name (Index 0x10)

        print("\n--- Test 1: Interrupted Segmented Write ---")

        # 1. Start Write 0x18 (App Tag), Length 20
        # Headers: Write(0.5B)+Len(0.5B)=0xA0? No, Len>15 means ExtLen.
        # Write Service = 0xA.
        # Let's use `write_isdu` but manually to interrupt.

        data = b"X" * 20

        # Generate raw ISDU bytes
        # [Service(A)|Len(0?)] [ExtLen(20)] [IdxH] [IdxL] [Sub] [Data...]
        # 0xA0 (Write, ExtLen)
        # 0x14 (20)
        # 0x00
        # 0x18
        # 0x00
        # ...

        req_bytes = [0xA0, 0x14, 0x00, 0x18, 0x00] + list(data)

        # Send first few bytes (Header)
        # ISDU is interleaved in V1.1.

        # Helper to send single ISDU byte
        def send_isdu_byte(byte_val, start=False, last=False, seq=0):
            # 1. Send Control
            ctrl = seq & 0x3F
            if start:
                ctrl |= 0x80
            if last:
                ctrl |= 0x40

            resp_ctrl = master.run_cycle(od_req=ctrl)
            if not resp_ctrl or not resp_ctrl.valid:
                return None

            # 2. Send Data
            resp_data = master.run_cycle(od_req=byte_val)
            if not resp_data or not resp_data.valid:
                return None

            print(f"   Saved Control=0x{ctrl:02X} -> RX=0x{resp_ctrl.od:02X}")
            print(f"   Saved Data=0x{byte_val:02X}    -> RX=0x{resp_data.od:02X}")
            return resp_data.od

        print("Sending ISDU Write Header (interrupted)...")
        # Send Header bytes (0-4)
        for i in range(5):
            send_isdu_byte(req_bytes[i], start=(i == 0), seq=i)

        print("Interrupting with New Read Request (Index 0x10)...")

        # 1. Start (0x80)
        resp1 = master.run_cycle(od_req=0x80)
        print(f"   Sent Start (0x80) -> RX=0x{resp1.od:02X}")

        # 2. Data (0x90)
        resp2 = master.run_cycle(od_req=0x90)
        print(f"   Sent Service(0x90) -> RX=0x{resp2.od:02X}")

        # 3. Rest of header
        send_isdu_byte(0x00, start=False, seq=1)  # IdxH
        send_isdu_byte(0x10, start=False, seq=2)  # IdxL
        last_rx = send_isdu_byte(0x00, start=False, seq=3, last=True)  # Sub

        print(f"   Last Byte RX: 0x{last_rx:02X}")

        # Now collect response for Read 0x10
        print("Collecting response...")

        # If last_rx was NOT 0x00, it might be the Start Control Byte
        if last_rx != 0:
            print("   (Captured response in last cycle)")
            # Assuming interleaved: last cycle was Data cycle. Result is Ctrl?
            # Yes, response starts with Ctrl.
            # If next is Data.

        collected = bytearray()

        for i in range(20):
            resp = master.run_cycle(od_req=0x00)
            val = resp.od
            print(f"   RX Cycle {i}: 0x{val:02X}")

            if val != 0:
                # Simple heuristic: ignore 0s, collect likely chars
                collected.append(val)

        print(f"Raw Collected: {collected.hex()}")
        name = collected.decode("ascii", errors="ignore")
        print(f"Read Result: '{name}'")

        if "iolinki" in name:
            print("✅ Interrupted Write -> New Read SUCCESS")
        else:
            print("❌ Failed.")

    finally:
        process.terminate()
        process.wait()
        master.close()


if __name__ == "__main__":
    verify_isdu_flow_control()
