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
from virtual_master.protocol import MSequenceType
from virtual_master.real_uart import RealUART


def main():
    parser = argparse.ArgumentParser(description="Run IO-Link Master on Real Hardware")
    parser.add_argument("port", help="Serial port (e.g., /dev/ttyACM0)")
    parser.add_argument(
        "--baud", type=int, default=38400, help="Baudrate (default: 38400)"
    )
    parser.add_argument("--parity", choices=("even", "none"), default="even",
                        help="UART framing; IO-Link uses 8E1 (default: even)")
    parser.add_argument("--pd-in-len", type=int, default=0, help="Device-to-master process data in bytes")
    parser.add_argument("--pd-out-len", type=int, default=0, help="Master-to-device process data in bytes")
    parser.add_argument("--m-seq-type", choices=("0", "1_1", "1_2", "1_v", "2_1", "2_2", "2_v"),
                        default="0", help="OPERATE M-sequence type (default: 0)")
    parser.add_argument("--od-len", type=int, choices=(1, 2), default=None,
                        help="TYPE_2_V on-request data width; e.g. 1 for 4-byte PDin + ISDU")
    parser.add_argument("--cycles", type=int, default=50, help="Number of OPERATE cycles")
    parser.add_argument("--cycle-ms", type=float, default=10.0,
                        help="Best-effort host cycle period in ms; not a real-time timing guarantee")
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
    types = {
        "0": MSequenceType.TYPE_0, "1_1": MSequenceType.TYPE_1_1,
        "1_2": MSequenceType.TYPE_1_2, "1_v": MSequenceType.TYPE_1_V,
        "2_1": MSequenceType.TYPE_2_1, "2_2": MSequenceType.TYPE_2_2,
        "2_v": MSequenceType.TYPE_2_V,
    }
    if not (0 <= args.pd_in_len <= 32 and 0 <= args.pd_out_len <= 32):
        parser.error("PD lengths must be between 0 and 32 bytes")
    if args.cycles < 1 or args.cycle_ms <= 0:
        parser.error("--cycles and --cycle-ms must be positive")
    if args.od_len is not None and args.m_seq_type != "2_v":
        parser.error("--od-len override is supported only with --m-seq-type 2_v")

    print("=== IO-Link Master - Real Hardware Test ===")
    print(f"Connecting to {args.port} at {args.baud} baud...")

    try:
        uart = RealUART(args.port, args.baud, parity=args.parity)
    except Exception as e:
        print(f"Error opening serial port: {e}")
        return 1

    if args.hold_lines:
        uart.ser.dtr = False
        uart.ser.rts = False
        uart.flush()

    with VirtualMaster(uart=uart, m_seq_type=types[args.m_seq_type],
                       pd_in_len=args.pd_in_len, pd_out_len=args.pd_out_len,
                       od_len=args.od_len) as master:
        print("Waiting 2 seconds for stability...")
        time.sleep(2)

        if master.run_startup_sequence(send_wakeup=not args.no_wakeup):
            print()
            print("[SUCCESS] Startup complete! Device is in PREOPERATE.")
            
            # Transition to OPERATE
            if master.go_to_operate():
                 print("[SUCCESS] Transitioned to OPERATE.")
            
            print()
            print(f"Running {args.cycles} best-effort communication cycles...")
            next_start = time.monotonic()
            for i in range(args.cycles):
                response = master.run_cycle()
                
                if response.valid:
                     # Simulate some meaningful output occasionally
                     if i % 10 == 0:
                         print(f"  Cycle {i}: PD_In={response.pd.hex()}")
                else:
                     print(f"  Cycle {i}: No response")

                if response.has_event():
                    print(f"  Cycle {i}: Device has pending event!")
                    for _qualifier, event_code in master.read_event_memory():
                        print(f"  Event code: 0x{event_code:04X}")
                    master.ack_events()

                next_start += args.cycle_ms / 1000.0
                time.sleep(max(0.0, next_start - time.monotonic()))

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
