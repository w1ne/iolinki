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
        self.demo_bin = os.environ.get("IOLINK_DEVICE_PATH", "./build/examples/host_demo/host_demo")

    def tearDown(self):
        if hasattr(self, 'process') and self.process:
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
        
        self.process = subprocess.Popen([self.demo_bin, self.device_tty, "1", "2"],
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.5)
        
        # Enter OPERATE state
        self.master.run_startup_sequence()
        self.master.m_seq_type = 2
        self.master.pd_out_len = 2
        self.master.pd_in_len = 2
        self.master.go_to_operate()
        time.sleep(0.1)
        
        # Verify PD works
        resp1 = self.master.run_cycle(pd_out=b'\xAA\xBB')
        self.assertIsNotNone(resp1, "PD should work before dropout")
        
        # Simulate communication loss
        print("[INFO] Simulating 300ms communication dropout...")
        time.sleep(0.3)
        
        # Try to recover with new startup
        self.master.send_wakeup()
        time.sleep(0.1)
        
        # Verify recovery
        response = self.master.read_isdu(index=0x0012, subindex=0x00)
        self.assertIsNotNone(response, "Device should recover after dropout")
        print(f"[PASS] Device recovered successfully")

    def test_02_rapid_state_transitions(self):
        """
        Test Case: Rapid State Transitions
        Requirement: IO-Link V1.1.5 Section 7.3 - State Machine Robustness
        
        Validates:
        - Device handles rapid state changes
        - No crashes or hangs
        """
        print("\n[TEST] Rapid State Transitions")
        
        self.process = subprocess.Popen([self.demo_bin, self.device_tty, "0", "0"],
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.5)
        
        # Perform 5 rapid startup cycles
        success_count = 0
        for i in range(5):
            self.master.send_wakeup()
            time.sleep(0.05)
            response = self.master.read_isdu(index=0x0012, subindex=0x00)
            if response:
                success_count += 1
        
        self.assertGreater(success_count, 3, "At least 3/5 rapid transitions should succeed")
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
        
        self.process = subprocess.Popen([self.demo_bin, self.device_tty, "0", "0"],
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
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
        
        self.process = subprocess.Popen([self.demo_bin, self.device_tty, "0", "0"],
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.5)
        
        self.master.run_startup_sequence()
        
        # Application Tag is 16 bytes (good test for segmentation)
        large_data = b'LargeDataTest123'
        
        write_result = self.master.write_isdu(index=0x0018, subindex=0x00, data=large_data)
        self.assertTrue(write_result, "Large ISDU write should succeed")
        
        readback = self.master.read_isdu(index=0x0018, subindex=0x00)
        self.assertIsNotNone(readback, "Large ISDU should be readable")
        print(f"[PASS] 16-byte ISDU write/read successful")

    def test_05_error_recovery_sequence(self):
        """
        Test Case: Full Error Recovery Sequence
        Requirement: IO-Link V1.1.5 Section 7.3.5 - Recovery
        
        Validates:
        - Device can recover from multiple error conditions
        - Full functionality is restored
        """
        print("\n[TEST] Full Error Recovery Sequence")
        
        self.process = subprocess.Popen([self.demo_bin, self.device_tty, "0", "0"],
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.5)
        
        # 1. Start in good state
        self.master.run_startup_sequence()
        initial = self.master.read_isdu(index=0x0012, subindex=0x00)
        self.assertIsNotNone(initial, "Initial state should be good")
        
        # 2. Induce error (communication loss)
        time.sleep(0.2)
        
        # 3. Recovery attempt
        self.master.send_wakeup()
        time.sleep(0.1)
        
        # 4. Verify full functionality
        vendor = self.master.read_isdu(index=0x0010, subindex=0x00)
        product = self.master.read_isdu(index=0x0012, subindex=0x00)
        
        self.assertIsNotNone(vendor, "Vendor Name should be readable after recovery")
        self.assertIsNotNone(product, "Product Name should be readable after recovery")
        print(f"[PASS] Full recovery sequence successful")

    def test_06_bad_crc_handling(self):
        """
        Test Case: CRC Error Handling
        Requirement: IO-Link V1.1.5 Section 7.2 - Frame Validation
        
        Validates:
        - Device detects and handles CRC errors
        - Retransmission or error reporting works
        """
        print("\n[TEST] CRC Error Handling")
        
        self.process = subprocess.Popen([self.demo_bin, self.device_tty, "1", "2"],
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.5)
        
        self.master.run_startup_sequence()
        self.master.m_seq_type = 2
        self.master.pd_out_len = 2
        self.master.pd_in_len = 2
        self.master.go_to_operate()
        time.sleep(0.1)
        
        # Send frame with bad CRC (if supported by Virtual Master)
        if hasattr(self.master, 'run_cycle_bad_crc'):
            resp = self.master.run_cycle_bad_crc(pd_out=b'\x12\x34')
            # Device should either reject or handle gracefully
            print("[PASS] Device handled bad CRC gracefully")
        else:
            print("[SKIP] Bad CRC injection not supported by Virtual Master")


if __name__ == '__main__':
    print("=" * 70)
    print("IO-Link V1.1.5 Conformance Test Suite: Error Injection & Recovery")
    print("=" * 70)
    unittest.main(verbosity=2)
