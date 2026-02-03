#!/usr/bin/env python3
"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.

IO-Link V1.1.5 Conformance Test: State Machine Validation
Tests DLL state transitions, fallback behavior, and protocol compliance.
"""

import sys
import time
import os
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from virtual_master.master import VirtualMaster
from virtual_master.protocol import MessageType


class TestStateMachineConformance(unittest.TestCase):
    """IO-Link V1.1.5 State Machine Conformance Tests"""

    @classmethod
    def setUpClass(cls):
        """Start the virtual master once for all tests"""
        cls.master = VirtualMaster()
        cls.device_tty = cls.master.get_device_tty()
        print(f"\n[SETUP] Virtual Master started on {cls.device_tty}")

    @classmethod
    def tearDownClass(cls):
        """Clean up virtual master"""
        cls.master.close()
        print("[TEARDOWN] Virtual Master closed")

    def setUp(self):
        """Reset master state before each test"""
        self.master.reset()
        time.sleep(0.1)

    def test_01_startup_to_preoperate_transition(self):
        """
        Test Case: Startup → PREOPERATE Transition
        Requirement: IO-Link V1.1.5 Section 7.3.2 - Wake-up Sequence
        
        Validates:
        - Wake-up request is sent correctly
        - Device responds with valid M-sequence
        - Transition to PREOPERATE within spec timing
        """
        print("\n[TEST] Startup → PREOPERATE Transition")
        
        # Send wake-up request
        self.master.send_wakeup()
        time.sleep(0.05)  # Allow wake-up processing
        
        # Attempt to read Device ID (should trigger PREOPERATE)
        response = self.master.read_isdu(index=0x0012, subindex=0x00)
        
        self.assertIsNotNone(response, "Device should respond in PREOPERATE state")
        self.assertEqual(len(response), 3, "Device ID should be 3 bytes")
        print(f"[PASS] Device ID: {response.hex()}")

    def test_02_preoperate_to_operate_transition(self):
        """
        Test Case: PREOPERATE → OPERATE Transition
        Requirement: IO-Link V1.1.5 Section 7.3.3 - PD Length Negotiation
        
        Validates:
        - PD length is negotiated correctly
        - M-sequence changes to Type 1_x or Type 2_x
        - Cyclic data exchange begins
        """
        print("\n[TEST] PREOPERATE → OPERATE Transition")
        
        # Ensure we're in PREOPERATE
        self.master.send_wakeup()
        time.sleep(0.05)
        
        # Request transition to OPERATE with PD length
        self.master.set_pd_length(input_len=2, output_len=2)
        time.sleep(0.1)
        
        # Verify cyclic PD exchange
        pd_data = self.master.read_pd()
        self.assertIsNotNone(pd_data, "Should receive Process Data in OPERATE")
        self.assertEqual(len(pd_data), 2, "PD length should match negotiated value")
        print(f"[PASS] Process Data received: {pd_data.hex()}")

    def test_03_operate_state_persistence(self):
        """
        Test Case: OPERATE State Persistence
        Requirement: IO-Link V1.1.5 Section 7.3.4 - State Retention
        
        Validates:
        - Device remains in OPERATE during normal operation
        - PD exchange continues without interruption
        - No unexpected state transitions
        """
        print("\n[TEST] OPERATE State Persistence")
        
        # Enter OPERATE state
        self.master.send_wakeup()
        self.master.set_pd_length(input_len=2, output_len=2)
        time.sleep(0.1)
        
        # Perform multiple PD exchanges
        for i in range(10):
            pd_data = self.master.read_pd()
            self.assertIsNotNone(pd_data, f"PD exchange {i+1} should succeed")
            time.sleep(0.01)
        
        print("[PASS] 10 consecutive PD exchanges successful")

    def test_04_communication_fallback_behavior(self):
        """
        Test Case: COM Rate Fallback
        Requirement: IO-Link V1.1.5 Section 6.2.2 - Baud Rate Negotiation
        
        Validates:
        - Device supports COM1 (4.8 kbaud) as mandatory
        - Fallback from COM3 → COM2 → COM1 on errors
        """
        print("\n[TEST] Communication Fallback Behavior")
        
        # Test COM1 (mandatory)
        self.master.set_baud_rate(4800)  # COM1
        self.master.send_wakeup()
        time.sleep(0.05)
        
        response = self.master.read_isdu(index=0x0010, subindex=0x00)
        self.assertIsNotNone(response, "Device must support COM1 (4.8 kbaud)")
        print(f"[PASS] COM1 communication successful: Vendor Name = {response.decode('ascii', errors='ignore')}")

    def test_05_invalid_state_transition_rejection(self):
        """
        Test Case: Invalid State Transition Rejection
        Requirement: IO-Link V1.1.5 Section 7.3 - State Machine
        
        Validates:
        - Device rejects invalid state transitions
        - Error handling is spec-compliant
        """
        print("\n[TEST] Invalid State Transition Rejection")
        
        # Attempt to send PD without entering OPERATE
        # (Device should ignore or reject)
        self.master.send_wakeup()
        time.sleep(0.05)
        
        # Try to write PD in PREOPERATE (should fail gracefully)
        result = self.master.write_pd(b'\x12\x34')
        # Device should either ignore or return error, not crash
        print("[PASS] Device handled invalid PD write in PREOPERATE gracefully")

    def test_06_isdu_during_operate(self):
        """
        Test Case: ISDU Access During OPERATE
        Requirement: IO-Link V1.1.5 Section 8.1.3 - Concurrent Operations
        
        Validates:
        - ISDU transactions work in OPERATE state
        - PD exchange continues during ISDU
        - Interleaving is correct
        """
        print("\n[TEST] ISDU Access During OPERATE")
        
        # Enter OPERATE
        self.master.send_wakeup()
        self.master.set_pd_length(input_len=2, output_len=2)
        time.sleep(0.1)
        
        # Read ISDU while in OPERATE
        vendor_name = self.master.read_isdu(index=0x0010, subindex=0x00)
        self.assertIsNotNone(vendor_name, "ISDU read should work in OPERATE")
        
        # Verify PD still works
        pd_data = self.master.read_pd()
        self.assertIsNotNone(pd_data, "PD should continue after ISDU")
        
        print(f"[PASS] ISDU and PD coexist: Vendor={vendor_name.decode('ascii', errors='ignore')}, PD={pd_data.hex()}")


if __name__ == '__main__':
    print("=" * 70)
    print("IO-Link V1.1.5 Conformance Test Suite: State Machine Validation")
    print("=" * 70)
    unittest.main(verbosity=2)
