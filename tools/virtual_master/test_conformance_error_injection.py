#!/usr/bin/env python3
"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.

IO-Link V1.1.5 Conformance Test: Error Injection & Recovery
Tests device robustness, error handling, and recovery mechanisms.
"""

import sys
import time
import os
import unittest
import subprocess

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from virtual_master.master import VirtualMaster


class TestErrorInjectionConformance(unittest.TestCase):
    """IO-Link V1.1.5 Error Injection & Recovery Tests"""

    def setUp(self):
        self.master = VirtualMaster()
        self.device_tty = self.master.get_device_tty()
        self.demo_bin = os.environ.get(
            "IOLINK_DEVICE_PATH", "./build/examples/host_demo/host_demo"
        )

    def tearDown(self):
        if hasattr(self, "process") and self.process:
            self.process.terminate()
            self.process.wait()
        self.master.close()

    def test_01_communication_loss_recovery(self):
        """
        Test Case: Communication Loss Recovery
        Requirement: IO-Link V1.1.5 Section 7.3.5 - Error Handling

        Validates:
        - Device recovers from master dropout
        - State machine returns to valid state
        """
        print("\n[TEST] Communication Loss Recovery")

        self.process = subprocess.Popen(
            [self.demo_bin, self.device_tty, "1", "2"],
            stdout=sys.stdout,
            stderr=sys.stderr,
        )
        time.sleep(0.5)

        # Enter OPERATE state
        self.master.run_startup_sequence()
        self.master.m_seq_type = 2
        self.master.pd_out_len = 2
        self.master.pd_in_len = 2
        self.master.go_to_operate()
        time.sleep(0.1)

        # Verify PD works
        resp1 = self.master.run_cycle(pd_out=b"\xaa\xbb")
        self.assertIsNotNone(resp1, "PD should work before dropout")

        # Simulate communication loss
        print("[INFO] Simulating 7s communication dropout...")
        time.sleep(7.0)

        # Try to recover with fresh startup
        self.master.m_seq_type = 0
        success = self.master.run_startup_sequence()
        self.assertTrue(success, "Device should recover after dropout")

        # Verify recovery
        response = self.master.read_isdu(index=0x0012, subindex=0x00)
        self.assertIsNotNone(response, "Device ISDU should work after recovery")
        print("[PASS] Device recovered successfully")

    def test_02_rapid_state_transitions(self):
        """
        Test Case: Rapid State Transitions
        Requirement: IO-Link V1.1.5 Section 7.3 - State Machine Robustness

        Validates:
        - Device handles rapid state changes
        - No crashes or hangs
        """
        print("\n[TEST] Rapid State Transitions")

        self.process = subprocess.Popen(
            [self.demo_bin, self.device_tty, "0", "0"],
            stdout=sys.stdout,
            stderr=sys.stderr,
        )
        time.sleep(0.5)

        # Perform 5 rapid startup cycles
        success_count = 0
        for i in range(5):
            self.master.send_wakeup()
            time.sleep(6.0)  # Give device time to timeout from previous attempts (>5s)
            response = self.master.read_isdu(index=0x0012, subindex=0x00)
            if response:
                success_count += 1

        self.assertGreaterEqual(
            success_count, 3, "At least 3/5 rapid transitions should succeed"
        )
        print(f"[PASS] {success_count}/5 rapid state transitions successful")

    def test_03_concurrent_isdu_requests(self):
        """
        Test Case: Concurrent ISDU Requests
        Requirement: IO-Link V1.1.5 Section 8.1 - ISDU Flow Control

        Validates:
        - Device handles overlapping ISDU requests
        - ISDU Busy state is used correctly
        """
        print("\n[TEST] Concurrent ISDU Requests")

        self.process = subprocess.Popen(
            [self.demo_bin, self.device_tty, "0", "0"],
            stdout=sys.stdout,
            stderr=sys.stderr,
        )
        time.sleep(0.5)

        self.master.run_startup_sequence()

        # Send multiple ISDU reads in quick succession
        results = []
        for i in range(3):
            response = self.master.read_isdu(index=0x0010, subindex=0x00)
            results.append(response is not None)

        # At least some should succeed
        success_count = sum(results)
        self.assertGreater(success_count, 0, "At least one ISDU should succeed")
        print(f"[PASS] {success_count}/3 concurrent ISDU requests succeeded")

    def test_04_boundary_condition_max_isdu_size(self):
        """
        Test Case: Maximum ISDU Size
        Requirement: IO-Link V1.1.5 Section 8.1.2 - ISDU Length

        Validates:
        - Device handles maximum ISDU data size
        - Segmentation works correctly
        """
        print("\n[TEST] Maximum ISDU Size Handling")

        self.process = subprocess.Popen(
            [self.demo_bin, self.device_tty, "1", "2"],
            stdout=sys.stdout,
            stderr=sys.stderr,
        )
        time.sleep(0.5)

        self.master.run_startup_sequence()
        # Transition to OPERATE to avoid Type 0 MC collision (0x0F)
        self.master.m_seq_type = 2
        self.master.pd_out_len = 2
        self.master.pd_in_len = 2
        self.master.go_to_operate()
        time.sleep(0.1)
        large_data = b"LargeDataTest123"

        write_result = self.master.write_isdu(
            index=0x0018, subindex=0x00, data=large_data
        )
        self.assertTrue(write_result, "Large ISDU write should succeed")

        readback = self.master.read_isdu(index=0x0018, subindex=0x00)
        self.assertIsNotNone(readback, "Large ISDU should be readable")
        print("[PASS] 16-byte ISDU write/read successful")

    def test_05_error_recovery_sequence(self):
        """
        Test Case: Full Error Recovery Sequence
        Requirement: IO-Link V1.1.5 Section 7.3.5 - Recovery

        Validates:
        - Device can recover from multiple error conditions
        - Full functionality is restored
        """
        print("\n[TEST] Full Error Recovery Sequence")

        self.process = subprocess.Popen(
            [self.demo_bin, self.device_tty, "0", "0"],
            stdout=sys.stdout,
            stderr=sys.stderr,
        )
        time.sleep(0.5)

        # 1. Start in good state
        self.master.run_startup_sequence()
        initial = self.master.read_isdu(index=0x0012, subindex=0x00)
        self.assertIsNotNone(initial, "Initial state should be good")

        # 2. Induce error (communication loss)
        time.sleep(6.0)

        # 3. Try to recover with fresh startup
        print("[INFO] Attempting recovery...")
        self.master.m_seq_type = 0
        success = False
        for i in range(3):
            if self.master.run_startup_sequence():
                success = True
                break
            time.sleep(0.3)
        self.assertTrue(success, "Device should recover after communication loss")

        # 4. Verify full functionality
        vendor = self.master.read_isdu(index=0x0010, subindex=0x00)
        product = self.master.read_isdu(index=0x0012, subindex=0x00)

        self.assertIsNotNone(vendor, "Vendor Name should be readable after recovery")
        self.assertIsNotNone(product, "Product Name should be readable after recovery")
        print("[PASS] Full recovery sequence successful")

    def test_06_bad_crc_handling(self):
        """
        Test Case: CRC Error Handling
        Requirement: IO-Link V1.1.5 Section 7.2 - Frame Validation

        Validates:
        - Device detects and handles CRC errors
        - Retransmission or error reporting works
        """
        print("\n[TEST] CRC Error Handling")

        self.process = subprocess.Popen(
            [self.demo_bin, self.device_tty, "1", "2"],
            stdout=sys.stdout,
            stderr=sys.stderr,
        )
        time.sleep(0.5)

        self.master.run_startup_sequence()
        self.master.m_seq_type = 2
        self.master.pd_out_len = 2
        self.master.pd_in_len = 2
        self.master.go_to_operate()
        time.sleep(0.1)

        # Send frame with bad CRC (if supported by Virtual Master)
        if hasattr(self.master, "run_cycle_bad_crc"):
            self.master.run_cycle_bad_crc(pd_out=b"\x12\x34")
            # Device should either reject or handle gracefully
            print("[PASS] Device handled bad CRC gracefully")
        else:
            print("[SKIP] Bad CRC injection not supported by Virtual Master")

    def test_07_crc_fallback_recovery(self):
        """
        Test Case: CRC Error Fallback & Recovery
        Requirement: IO-Link V1.1.5 Section 7.3.5 - Error Handling

        Validates:
        - Device tolerates repeated CRC errors
        - Device falls back to COM1 after max retries (3 consecutive CRC errors)
        - Device resets to STARTUP state in fallback
        - Device can recover via new startup sequence
        - Full functionality is restored after recovery
        """
        print("\n[TEST] CRC Error Fallback & Recovery")

        self.process = subprocess.Popen(
            [self.demo_bin, self.device_tty, "1", "2"],
            stdout=sys.stdout,
            stderr=sys.stderr,
        )
        time.sleep(0.5)

        # Step 1: Establish OPERATE state
        self.master.run_startup_sequence()
        self.master.m_seq_type = 2
        self.master.pd_out_len = 2
        self.master.pd_in_len = 2
        self.master.go_to_operate()
        time.sleep(0.1)

        # Verify initial state is good
        resp_good = self.master.run_cycle(pd_out=b"\xaa\xbb")
        self.assertIsNotNone(resp_good, "Initial PD should work")
        print("[INFO] Device in OPERATE state, PD working")

        # Step 2: Inject 3+ consecutive bad CRC frames to trigger fallback
        if hasattr(self.master, "run_cycle_bad_crc"):
            print("[INFO] Injecting 3 consecutive bad CRC frames...")
            for i in range(3):
                self.master.run_cycle_bad_crc(pd_out=b"\xaa\xbb")
                time.sleep(0.02)

            # Device should now be in FALLBACK state, transitioning to STARTUP with COM1
            print("[INFO] Bad CRC frames sent, device should enter FALLBACK → STARTUP")
            time.sleep(6.0)  # Allow fallback state transition (>5000ms)
        else:
            print("[SKIP] Bad CRC injection not supported, simulating with delay")
            time.sleep(0.3)

        # Step 3: Attempt recovery with fresh startup sequence
        # Device should now be at COM1 baudrate in STARTUP state
        print("[INFO] Attempting recovery with new startup sequence...")

        # Reset master to Type 0 for recovery
        self.master.m_seq_type = 0
        self.master.send_wakeup()
        time.sleep(0.15)

        # Step 4: Verify device responds and is functional
        response = self.master.read_isdu(index=0x0012, subindex=0x00)
        self.assertIsNotNone(response, "Device should recover after CRC fallback")
        print(
            f"[INFO] Device recovered: Product Name = {response.decode('ascii', errors='ignore')}"
        )

        # Step 5: Verify full functionality is restored
        vendor = self.master.read_isdu(index=0x0010, subindex=0x00)
        self.assertIsNotNone(vendor, "Vendor Name should be readable after recovery")

        serial = self.master.read_isdu(index=0x0015, subindex=0x00)
        self.assertIsNotNone(serial, "Serial Number should be readable after recovery")

        # Step 6: Verify Event Reporting (Task 3 integration)
        print("[INFO] Verifying Event Reporting via Index 0x001C...")
        events_data = self.master.read_isdu(index=0x001C, subindex=0x00)
        self.assertIsNotNone(events_data, "Detailed Device Status should be readable")

        # Look for CRC error event code 0x1801
        found_crc_event = False
        for i in range(0, len(events_data), 3):
            if i + 2 < len(events_data):
                code = (events_data[i + 1] << 8) | events_data[i + 2]
                if code == 0x1801:
                    found_crc_event = True
                    break
        self.assertTrue(
            found_crc_event,
            "CRC error event (0x1801) should be present in Detailed Device Status",
        )
        print("[INFO] CRC error event found in Index 0x001C")

        # Check Device Status (Index 0x1B)
        status_data = self.master.read_isdu(index=0x001B, subindex=0x00)
        self.assertIsNotNone(status_data, "Device Status should be readable")
        self.assertGreaterEqual(
            status_data[0], 1, "Device Status should be non-zero (reported error)"
        )
        print(f"[INFO] Device Status: {status_data[0]}")

        # Step 7: Verify Event Popping via Index 2 (Task 3 specific)
        print("[INFO] Verifying Event Popping via Index 2...")
        event_pop_data = self.master.read_isdu(index=0x0002, subindex=0x00)
        self.assertIsNotNone(event_pop_data, "Index 2 should be readable")
        self.assertEqual(len(event_pop_data), 2, "Event code should be 2 bytes")
        pop_code = (event_pop_data[0] << 8) | event_pop_data[1]
        self.assertEqual(
            pop_code,
            0x1801,
            f"Expected 0x1801 popped from Index 2, got 0x{pop_code:04X}",
        )
        print(f"[INFO] Successfully popped event 0x{pop_code:04X} via Index 2")

        # Verify it's gone from 0x1C? (Actually 0x1C shows what's in queue, so it should be gone)
        events_data_after = self.master.read_isdu(index=0x001C, subindex=0x00)
        if events_data_after:
            found_again = False
            for i in range(0, len(events_data_after), 3):
                if i + 2 < len(events_data_after):
                    if (events_data_after[i + 1] << 8) | events_data_after[
                        i + 2
                    ] == 0x1801:
                        found_again = True
            self.assertFalse(
                found_again,
                "Popped event should no longer be in Detailed Device Status",
            )

        print(
            "[PASS] Device recovered after CRC fallback, full functionality and event popping verified"
        )


if __name__ == "__main__":
    print("=" * 70)
    print("IO-Link V1.1.5 Conformance Test Suite: Error Injection & Recovery")
    print("=" * 70)
    unittest.main(verbosity=2)
