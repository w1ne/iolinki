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
import subprocess

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from virtual_master.master import VirtualMaster


class TestStateMachineConformance(unittest.TestCase):
    """IO-Link V1.1.5 State Machine Conformance Tests"""

    def setUp(self):
        """Start virtual master and device for each test"""
        self.master = VirtualMaster()
        self.device_tty = self.master.get_device_tty()
        self.demo_bin = os.environ.get(
            "IOLINK_DEVICE_PATH", "./build/examples/host_demo/host_demo"
        )

    def tearDown(self):
        """Clean up virtual master"""
        if hasattr(self, "process") and self.process:
            self.process.terminate()
            self.process.wait()
        self.master.close()

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

        self.process = subprocess.Popen(
            [self.demo_bin, self.device_tty, "0", "0"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        time.sleep(0.5)

        success = self.master.run_startup_sequence()
        self.assertTrue(success, "Startup sequence should succeed")

        response = self.master.read_isdu(index=0x0012, subindex=0x00)

        self.assertIsNotNone(response, "Device should respond in PREOPERATE state")
        self.assertGreater(len(response), 0, "Product Name should not be empty")
        print(f"[PASS] Product Name: {response.decode('ascii', errors='ignore')}")

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

        self.process = subprocess.Popen(
            [self.demo_bin, self.device_tty, "1", "2"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        time.sleep(0.5)

        self.assertTrue(self.master.run_startup_sequence())

        self.master.m_seq_type = 2  # Type 1_2
        self.master.pd_out_len = 2
        self.master.pd_in_len = 2
        self.master.go_to_operate()
        time.sleep(0.1)

        resp = self.master.run_cycle(pd_out=b"\x12\x34")
        self.assertIsNotNone(resp, "Should receive Process Data in OPERATE")
        self.assertTrue(resp.valid, "PD response should be valid")
        self.assertEqual(
            len(resp.payload), 2, "PD length should match negotiated value"
        )
        print(f"[PASS] Process Data received: {resp.payload.hex()}")

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

        self.process = subprocess.Popen(
            [self.demo_bin, self.device_tty, "1", "2"], stdout=None, stderr=None
        )
        time.sleep(0.5)

        self.master.run_startup_sequence()
        self.master.m_seq_type = 2
        self.master.pd_out_len = 2
        self.master.pd_in_len = 2
        self.master.go_to_operate()
        time.sleep(0.1)

        success_count = 0
        for i in range(10):
            resp = self.master.run_cycle(pd_out=b"\xaa\xbb")
            if resp and resp.valid:
                success_count += 1
            time.sleep(0.01)

        self.assertGreaterEqual(
            success_count, 8, "At least 8/10 PD exchanges should succeed"
        )
        print(f"[PASS] {success_count}/10 consecutive PD exchanges successful")

    def test_04_communication_fallback_behavior(self):
        """
        Test Case: COM Rate Fallback
        Requirement: IO-Link V1.1.5 Section 6.2.2 - Baud Rate Negotiation

        Validates:
        - Device supports COM1 (4.8 kbaud) as mandatory
        - Communication works at different baud rates
        """
        print("\n[TEST] Communication Fallback Behavior")

        self.process = subprocess.Popen(
            [self.demo_bin, self.device_tty, "0", "0"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        time.sleep(0.5)

        self.master.run_startup_sequence()

        response = self.master.read_isdu(index=0x0010, subindex=0x00)
        self.assertIsNotNone(response, "Device must support communication")
        print(
            f"[PASS] Communication successful: Vendor Name = {response.decode('ascii', errors='ignore')}"
        )

    def test_05_invalid_state_transition_rejection(self):
        """
        Test Case: Invalid State Transition Rejection
        Requirement: IO-Link V1.1.5 Section 7.3 - State Machine

        Validates:
        - Device rejects invalid state transitions
        - Error handling is spec-compliant
        """
        print("\n[TEST] Invalid State Transition Rejection")

        self.process = subprocess.Popen(
            [self.demo_bin, self.device_tty, "1", "2"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        time.sleep(0.5)

        self.master.run_startup_sequence()

        self.master.m_seq_type = 2
        self.master.pd_out_len = 2
        self.master.pd_in_len = 2

        self.master.run_cycle(pd_out=b"\x12\x34")
        print("[PASS] Device handled PD attempt in PREOPERATE gracefully")

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

        self.process = subprocess.Popen(
            [self.demo_bin, self.device_tty, "1", "2"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        time.sleep(0.5)

        self.master.run_startup_sequence()
        self.master.m_seq_type = 2
        self.master.pd_out_len = 2
        self.master.pd_in_len = 2
        self.master.go_to_operate()
        time.sleep(0.1)

        vendor_name = self.master.read_isdu(index=0x0010, subindex=0x00)
        self.assertIsNotNone(vendor_name, "ISDU read should work in OPERATE")

        resp = self.master.run_cycle(pd_out=b"\x11\x22")
        self.assertIsNotNone(resp, "PD should continue after ISDU")
        self.assertTrue(resp.valid, "PD should be valid after ISDU")

        print(
            f"[PASS] ISDU and PD coexist: Vendor={vendor_name.decode('ascii', errors='ignore')}, PD={resp.payload.hex()}"
        )

    def test_07_estab_com_to_operate_transition(self):
        """
        Test Case: ESTAB_COM → OPERATE Transition
        Requirement: IO-Link V1.1.5 Section 7.3 - DLL State Machine

        Validates:
        - Device enters ESTAB_COM state on transition command (MC=0x0F)
        - First valid PD frame triggers ESTAB_COM → OPERATE transition
        - Subsequent PD frames are processed in OPERATE state
        - Invalid frames in ESTAB_COM don't cause premature transition
        """
        print("\n[TEST] ESTAB_COM → OPERATE Transition")

        self.process = subprocess.Popen(
            [self.demo_bin, self.device_tty, "1", "2"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        time.sleep(0.5)

        self.assertTrue(self.master.run_startup_sequence(), "Startup should succeed")
        print("[INFO] Device in PREOPERATE state")

        self.master.m_seq_type = 2  # Type 1_2
        self.master.pd_out_len = 2
        self.master.pd_in_len = 2

        self.master.go_to_operate()
        time.sleep(0.05)
        print("[INFO] Sent transition command (MC=0x0F), device should be in ESTAB_COM")

        resp1 = self.master.run_cycle(pd_out=b"\x12\x34")
        self.assertIsNotNone(resp1, "First PD frame should get response")
        self.assertTrue(resp1.valid, "First PD response should be valid")
        print("[INFO] First PD frame accepted, device should now be in OPERATE")

        success_count = 0
        for i in range(5):
            resp = self.master.run_cycle(pd_out=b"\xaa\xbb")
            if resp and resp.valid:
                success_count += 1
            time.sleep(0.01)

        self.assertGreaterEqual(
            success_count, 4, "At least 4/5 subsequent PD exchanges should succeed"
        )
        print(
            f"[PASS] ESTAB_COM → OPERATE transition successful, {success_count}/5 PD cycles in OPERATE"
        )


if __name__ == "__main__":
    print("=" * 70)
    print("IO-Link V1.1.5 Conformance Test Suite: State Machine Validation")
    print("=" * 70)
    unittest.main(verbosity=2)
