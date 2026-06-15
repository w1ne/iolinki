#!/usr/bin/env python3
"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.
"""

import sys
import os
import time
import argparse

# Add parent directory to path to find virtual_master package
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from virtual_master.master import VirtualMaster
from virtual_master.real_uart import RealUART


def main():
    parser = argparse.ArgumentParser(description="Run IO-Link Master on Real Hardware")
    parser.add_argument("port", help="Serial port (e.g., /dev/ttyACM0)")
    parser.add_argument(
        "--baud", type=int, default=38400, help="Baudrate (default: 38400)"
    )
    parser.add_argument(
        "--no-wakeup",
        action="store_true",
        help="Skip the 0x55 wake-up byte. Use for an already-established-COM "
        "link (a plain UART behind a transceiver front-end), where the byte "
        "would be spurious and desynchronise framing.",
    )
    parser.add_argument(
        "--hold-lines",
        action="store_true",
        help="Hold DTR/RTS low. Required for boards whose USB-serial resets on "
        "control-line changes (e.g. the ESP32-C3 native USB-Serial-JTAG).",
    )
    args = parser.parse_args()

    print(f"=== IO-Link Master - Real Hardware Test ===")
    print(f"Connecting to {args.port} at {args.baud} baud...")

    try:
        uart = RealUART(args.port, args.baud)
    except Exception as e:
        print(f"Error opening serial port: {e}")
        return 1

    if args.hold_lines:
        uart.ser.dtr = False
        uart.ser.rts = False
        uart.flush()

    with VirtualMaster(uart=uart) as master:
        print("Waiting 2 seconds for stability...")
        time.sleep(2)

        if master.run_startup_sequence(send_wakeup=not args.no_wakeup):
            print()
            print("[SUCCESS] Startup complete! Device is in PREOPERATE.")
            
            # Transition to OPERATE
            if master.go_to_operate():
                 print("[SUCCESS] Transitioned to OPERATE.")
            
            print()
            print("Running 50 communication cycles...")
            for i in range(50):
                response = master.run_cycle()
                
                if response.valid:
                     # Simulate some meaningful output occasionally
                     if i % 10 == 0:
                         print(f"  Cycle {i}: PD_In={response.payload.hex()}")
                else:
                     print(f"  Cycle {i}: No response")

                if response.has_event():
                    print(f"  Cycle {i}: Device has pending event!")
                    event_code = master.request_event()
                    if event_code:
                        print(f"  Event code: 0x{event_code:04X}")

                time.sleep(0.01)  # 10ms cycle time

            print()
            print("[DONE] Communication test complete")
        else:
            print()
            print("[FAILED] Startup sequence failed")
            return 1

    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\nInterrupted by user")
        sys.exit(0)
