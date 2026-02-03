#!/usr/bin/env python3
"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.

IO-Link V1.1.5 Conformance Test: Performance & Stress Testing
Tests throughput, sustained operation, and resource usage.
"""

import sys
import time
import os
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from virtual_master.master import VirtualMaster


class TestPerformanceConformance(unittest.TestCase):
    """IO-Link V1.1.5 Performance & Stress Tests"""

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

    def test_01_sustained_pd_exchange(self):
        """
        Test Case: Sustained Process Data Exchange
        Requirement: IO-Link V1.1.5 Section 9.1 - PD Reliability
        
        Validates:
        - Device can sustain PD exchange for extended period
        - No degradation or failures
        """
        print("\n[TEST] Sustained PD Exchange (100 cycles)")
        
        self.master.set_pd_length(input_len=2, output_len=2)
        time.sleep(0.1)
        
        success_count = 0
        failure_count = 0
        
        for i in range(100):
            pd_data = self.master.read_pd()
            if pd_data:
                success_count += 1
            else:
                failure_count += 1
            time.sleep(0.01)
        
        success_rate = (success_count / 100) * 100
        print(f"[INFO] Success rate: {success_rate:.1f}% ({success_count}/100)")
        
        # Require at least 90% success rate
        self.assertGreaterEqual(success_rate, 90, "PD success rate should be >= 90%")
        print(f"[PASS] Sustained PD exchange successful")

    def test_02_high_frequency_isdu_access(self):
        """
        Test Case: High-Frequency ISDU Access
        Requirement: IO-Link V1.1.5 Section 8.1 - ISDU Throughput
        
        Validates:
        - Device handles rapid ISDU requests
        - No resource exhaustion
        """
        print("\n[TEST] High-Frequency ISDU Access (20 reads)")
        
        start_time = time.time()
        success_count = 0
        
        for i in range(20):
            response = self.master.read_isdu(index=0x0012, subindex=0x00)
            if response:
                success_count += 1
        
        elapsed = time.time() - start_time
        throughput = success_count / elapsed if elapsed > 0 else 0
        
        print(f"[INFO] ISDU throughput: {throughput:.1f} reads/sec")
        print(f"[INFO] Success: {success_count}/20")
        
        self.assertGreater(success_count, 15, "At least 15/20 ISDU reads should succeed")
        print(f"[PASS] High-frequency ISDU access successful")

    def test_03_mixed_pd_and_isdu_load(self):
        """
        Test Case: Mixed PD and ISDU Load
        Requirement: IO-Link V1.1.5 Section 8.1.3 - Concurrent Operations
        
        Validates:
        - Device handles simultaneous PD and ISDU traffic
        - Both operations remain functional
        """
        print("\n[TEST] Mixed PD and ISDU Load")
        
        self.master.set_pd_length(input_len=2, output_len=2)
        time.sleep(0.1)
        
        pd_count = 0
        isdu_count = 0
        
        # Alternate between PD and ISDU
        for i in range(20):
            if i % 2 == 0:
                pd_data = self.master.read_pd()
                if pd_data:
                    pd_count += 1
            else:
                isdu_data = self.master.read_isdu(index=0x0012, subindex=0x00)
                if isdu_data:
                    isdu_count += 1
            time.sleep(0.02)
        
        print(f"[INFO] PD success: {pd_count}/10, ISDU success: {isdu_count}/10")
        
        self.assertGreater(pd_count, 7, "At least 7/10 PD reads should succeed")
        self.assertGreater(isdu_count, 7, "At least 7/10 ISDU reads should succeed")
        print(f"[PASS] Mixed load handled successfully")

    def test_04_rapid_state_cycling(self):
        """
        Test Case: Rapid State Cycling Stress Test
        Requirement: IO-Link V1.1.5 Section 7.3 - State Machine Robustness
        
        Validates:
        - Device handles rapid PREOPERATE ↔ OPERATE transitions
        - No memory leaks or resource exhaustion
        """
        print("\n[TEST] Rapid State Cycling (10 cycles)")
        
        success_count = 0
        
        for i in range(10):
            # Wake up (PREOPERATE)
            self.master.send_wakeup()
            time.sleep(0.02)
            
            # Enter OPERATE
            self.master.set_pd_length(input_len=2, output_len=2)
            time.sleep(0.02)
            
            # Verify PD works
            pd_data = self.master.read_pd()
            if pd_data:
                success_count += 1
            
            # Reset for next cycle
            self.master.reset()
            time.sleep(0.02)
        
        print(f"[INFO] Successful cycles: {success_count}/10")
        
        self.assertGreater(success_count, 7, "At least 7/10 cycles should succeed")
        print(f"[PASS] Rapid state cycling successful")

    def test_05_long_duration_stability(self):
        """
        Test Case: Long-Duration Stability Test
        Requirement: IO-Link V1.1.5 - General Reliability
        
        Validates:
        - Device remains stable over extended operation
        - No degradation or failures
        """
        print("\n[TEST] Long-Duration Stability (30 seconds)")
        
        self.master.set_pd_length(input_len=2, output_len=2)
        time.sleep(0.1)
        
        start_time = time.time()
        total_operations = 0
        successful_operations = 0
        
        while (time.time() - start_time) < 30:
            # Mix of PD and ISDU operations
            if total_operations % 10 == 0:
                # Every 10th operation is ISDU
                response = self.master.read_isdu(index=0x0012, subindex=0x00)
                if response:
                    successful_operations += 1
            else:
                # Regular PD exchange
                pd_data = self.master.read_pd()
                if pd_data:
                    successful_operations += 1
            
            total_operations += 1
            time.sleep(0.05)
        
        success_rate = (successful_operations / total_operations) * 100 if total_operations > 0 else 0
        print(f"[INFO] Operations: {total_operations}, Success rate: {success_rate:.1f}%")
        
        self.assertGreaterEqual(success_rate, 85, "Success rate should be >= 85%")
        print(f"[PASS] Long-duration stability test passed")


if __name__ == '__main__':
    print("=" * 70)
    print("IO-Link V1.1.5 Conformance Test Suite: Performance & Stress Testing")
    print("=" * 70)
    unittest.main(verbosity=2)
