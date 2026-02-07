#!/usr/bin/env python3
"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.
"""

"""
Test Type 1 M-sequence communication.
"""

import sys
import time
import subprocess
import os

sys.path.insert(0, ".")

from virtual_master.master import VirtualMaster


def run_device_in_background(tty_path, m_seq_type=1, pd_len=2):
    """Start Device process in background with Type 1 config."""
    device_path = os.environ.get(
        "IOLINK_DEVICE_PATH", "build/examples/host_demo/host_demo"
    )

    if not os.path.exists(device_path):
        print(f"[ERROR] Device not found: {device_path}")
        return None

    try:
        proc = subprocess.Popen([device_path, tty_path, str(m_seq_type), str(pd_len)])
        print(
            f"[INFO] Device started (PID: {proc.pid}, Type={m_seq_type}, PD={pd_len})"
        )
        time.sleep(0.5)
        return proc
    except Exception as e:
        print(f"[ERROR] Failed to start Device: {e}")
        return None


def test_type1_communication():
    """Verify Type 1 cyclic PD exchange."""
    print("=" * 60)
    print("Testing IO-Link Type 1 M-Sequence Communication")
    print("=" * 60)

    device_proc = None
    pd_len = 2

    try:
        master = VirtualMaster(m_seq_type=1, pd_in_len=pd_len, pd_out_len=pd_len)
        device_tty = master.get_device_tty()

        device_proc = run_device_in_background(device_tty, m_seq_type=1, pd_len=pd_len)
        if not device_proc:
            return 1

        print()
        print("[STEP 1] Establishing Communication")
        if not master.run_startup_sequence():
            print("❌ Startup failed")
            return 1
        print("✅ Preoperate reached")

        master.go_to_operate()
        print("✅ Transition sent")
        time.sleep(0.1)  # Give CI a bit of time, but not too much (device timeout)

        print()
        print("[STEP 2] Cyclic PD Exchange (Loopback Test)")

        test_data = [bytes([0x12, 0x34]), bytes([0xAA, 0xBB]), bytes([0x55, 0x66])]

        prev_expected = None
        for i, out_val in enumerate(test_data):
            print(f"   Cycle {i + 1}: Sending PD_OUT={out_val.hex()}")
            
            # Retry first cycle a bit as device might still be transitioning
            response = None
            for retry in range(5 if i == 0 else 1):
                response = master.run_cycle(pd_out=out_val)
                if response.valid:
                    break
                print(f"   ⚠️ Cycle {i + 1} timeout (retry {retry + 1})")
                time.sleep(0.1)

            if not response or not response.valid:
                print(f"   ❌ No valid response in cycle {i + 1}")
                return 1

            if prev_expected and response.payload == prev_expected:
                print(
                    f"   ✅ Received PD_IN={response.payload.hex()} (matches previous cycle + 1)"
                )
            elif prev_expected:
                print(
                    f"   ⚠️ Data mismatch! Expected {prev_expected.hex()}, got {response.payload.hex()}"
                )

            prev_expected = bytes([(b + 1) & 0xFF for b in out_val])
            time.sleep(0.02)

        print()
        print("[STEP 3] OD Channel Check (ISDU over Type 1)")
        vendor_name_bytes = master.read_isdu(0x10, 0)

        if vendor_name_bytes:
            vendor_name = vendor_name_bytes.decode("utf-8", errors="ignore")
            print(f"   ✅ Received Vendor Name: '{vendor_name}'")
            if (
                "iolinki" in vendor_name or "Acme" in vendor_name
            ):  # Default might be Acme or iolinki
                print("   ✅ Vendor Name verified")
            else:
                print(f"   ⚠️ Vendor Name content unexpected: {vendor_name}")
        else:
            print("   ❌ ISDU Read Failed")
            return 1

        print()
        print("[STEP 4] ISDU Write Test (Application Tag 0x18)")
        test_tag = b"TestTag"
        if master.write_isdu(0x18, 0, test_tag):
            print("   ✅ ISDU Write command sent")

            time.sleep(0.1)
            read_back = master.read_isdu(0x18, 0)
            if read_back == test_tag:
                print(f"   ✅ Read back verified: {read_back}")
            else:
                print(f"   ❌ Read back failed: Expected {test_tag}, got {read_back}")
        else:
            print("   ❌ ISDU Write Failed")
            return 1

        print()
        print("[STEP 5] Robustness Test (CRC Error Injection)")
        print("   Sending Type 1 frame with CORRUPTED CRC...")
        bad_response = master.run_cycle_bad_crc(pd_out=b"\xde\xad")

        if not bad_response.valid:
            print("   ✅ Device ignored bad frame (Timeout as expected)")
        else:
            print(f"   ❌ Device responded to bad frame! {bad_response}")
            return 1

        print()
        print("=" * 60)
        print("✅ TYPE 1 COMMUNICATION TEST PASSED")
        print("=" * 60)

        master.close()
        return 0

    except Exception as e:
        print()
        print(f"❌ Test failed: {e}")
        return 1
    finally:
        if device_proc:
            device_proc.terminate()


if __name__ == "__main__":
    sys.exit(test_type1_communication())
