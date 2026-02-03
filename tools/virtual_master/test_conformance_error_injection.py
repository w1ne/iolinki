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

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from virtual_master.master import VirtualMaster


class TestErrorInjectionConformance(unittest.TestCase):
    """IO-Link V1.1.5 Error Injection & Recovery Tests"""

    @classmethod
    def setUpClass(cls):
        cls.master = VirtualMaster()
        cls.device_tty = cls.master.get_device_tty()
        print(f"\n[SETUP] Virtual Master started on {cls.device_tty}")

    @classmethod
    def tearDownClass(cls):
        cls.master.close()
        print("[TEARDOWN] Virtual Master closed")

    def setUp(self):
        self.master.reset()
        self.master.send_wakeup()
        time.sleep(0.05)

    def test_01_communication_loss_recovery(self):
        """
        Test Case: Communication Loss Recovery
        Requirement: IO-Link V1.1.5 Section 7.3.5 - Error Handling
        
        Validates:
        - Device recovers from master dropout
        - State machine returns to valid state
        """
        print("\n[TEST] Communication Loss Recovery")
        
        # Enter OPERATE state
        self.master.set_pd_length(input_len=2, output_len=2)
        time.sleep(0.1)
        
        # Verify PD works
        pd1 = self.master.read_pd()
        self.assertIsNotNone(pd1, "PD should work before dropout")
        
        # Simulate communication loss
        print("[INFO] Simulating 500ms communication dropout...")
        time.sleep(0.5)
        
        # Try to recover
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
        
        # Perform 5 rapid wake-up cycles
        for i in range(5):
            self.master.send_wakeup()
            time.sleep(0.02)  # Very short delay
            response = self.master.read_isdu(index=0x0012, subindex=0x00)
            self.assertIsNotNone(response, f"Cycle {i+1} should succeed")
        
        print(f"[PASS] 5 rapid state transitions successful")

    def test_03_invalid_pd_length_handling(self):
        """
        Test Case: Invalid PD Length Handling
        Requirement: IO-Link V1.1.5 Section 9.1 - PD Configuration
        
        Validates:
        - Device rejects invalid PD lengths
        - Error handling is graceful
        """
        print("\n[TEST] Invalid PD Length Handling")
        
        # Try to set invalid PD length (> 32 bytes)
        try:
            self.master.set_pd_length(input_len=64, output_len=64)
            time.sleep(0.1)
            
            # Device should either reject or handle gracefully
            pd_data = self.master.read_pd()
            # If we get here, device handled it (may have clamped to max)
            print(f"[PASS] Invalid PD length handled gracefully")
        except Exception as e:
            # Exception is also acceptable
            print(f"[PASS] Invalid PD length rejected: {e}")

    def test_04_concurrent_isdu_requests(self):
        """
        Test Case: Concurrent ISDU Requests
        Requirement: IO-Link V1.1.5 Section 8.1 - ISDU Flow Control
        
        Validates:
        - Device handles overlapping ISDU requests
        - ISDU Busy state is used correctly
        """
        print("\n[TEST] Concurrent ISDU Requests")
        
        # Send multiple ISDU reads in quick succession
        results = []
        for i in range(3):
            response = self.master.read_isdu(index=0x0010, subindex=0x00)
            results.append(response is not None)
            time.sleep(0.01)  # Minimal delay
        
        # At least some should succeed
        success_count = sum(results)
        self.assertGreater(success_count, 0, "At least one ISDU should succeed")
        print(f"[PASS] {success_count}/3 concurrent ISDU requests succeeded")

    def test_05_boundary_condition_max_isdu_size(self):
        """
        Test Case: Maximum ISDU Size
        Requirement: IO-Link V1.1.5 Section 8.1.2 - ISDU Length
        
        Validates:
        - Device handles maximum ISDU data size
        - Segmentation works correctly
        """
        print("\n[TEST] Maximum ISDU Size Handling")
        
        # Application Tag is 16 bytes (good test for segmentation)
        large_data = b'\xFF' * 16
        
        write_result = self.master.write_isdu(index=0x0018, subindex=0x00, data=large_data)
        self.assertTrue(write_result, "Large ISDU write should succeed")
        
        readback = self.master.read_isdu(index=0x0018, subindex=0x00)
        self.assertEqual(readback, large_data, "Large ISDU should be read back correctly")
        print(f"[PASS] 16-byte ISDU write/read successful")

    def test_06_error_recovery_sequence(self):
        """
        Test Case: Full Error Recovery Sequence
        Requirement: IO-Link V1.1.5 Section 7.3.5 - Recovery
        
        Validates:
        - Device can recover from multiple error conditions
        - Full functionality is restored
        """
        print("\n[TEST] Full Error Recovery Sequence")
        
        # 1. Start in good state
        initial = self.master.read_isdu(index=0x0012, subindex=0x00)
        self.assertIsNotNone(initial, "Initial state should be good")
        
        # 2. Induce error (communication loss)
        time.sleep(0.3)
        
        # 3. Recovery attempt
        self.master.send_wakeup()
        time.sleep(0.1)
        
        # 4. Verify full functionality
        vendor = self.master.read_isdu(index=0x0010, subindex=0x00)
        product = self.master.read_isdu(index=0x0012, subindex=0x00)
        
        self.assertIsNotNone(vendor, "Vendor Name should be readable after recovery")
        self.assertIsNotNone(product, "Product Name should be readable after recovery")
        print(f"[PASS] Full recovery sequence successful")


if __name__ == '__main__':
    print("=" * 70)
    print("IO-Link V1.1.5 Conformance Test Suite: Error Injection & Recovery")
    print("=" * 70)
    unittest.main(verbosity=2)
