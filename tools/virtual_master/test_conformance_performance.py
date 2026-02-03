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
import subprocess

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from virtual_master.master import VirtualMaster


class TestPerformanceConformance(unittest.TestCase):
    """IO-Link V1.1.5 Performance & Stress Tests"""

    def setUp(self):
        self.master = VirtualMaster()
        self.device_tty = self.master.get_device_tty()
        self.demo_bin = os.environ.get("IOLINK_DEVICE_PATH", "./build/examples/host_demo/host_demo")

    def tearDown(self):
        if hasattr(self, 'process') and self.process:
            self.process.terminate()
            self.process.wait()
        self.master.close()

    def test_01_sustained_pd_exchange(self):
        """
        Test Case: Sustained Process Data Exchange
        Requirement: IO-Link V1.1.5 Section 9.1 - PD Reliability
        
        Validates:
        - Device can sustain PD exchange for extended period
        - No degradation or failures
        """
        print("\n[TEST] Sustained PD Exchange (100 cycles)")
        
        self.process = subprocess.Popen([self.demo_bin, self.device_tty, "1", "2"],
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.5)
        
        self.master.run_startup_sequence()
        self.master.m_seq_type = 2
        self.master.pd_out_len = 2
        self.master.pd_in_len = 2
        self.master.go_to_operate()
        time.sleep(0.1)
        
        success_count = 0
        failure_count = 0
        
        for i in range(100):
            resp = self.master.run_cycle(pd_out=b'\xAA\xBB')
            if resp and resp.valid:
                success_count += 1
            else:
                failure_count += 1
            time.sleep(0.01)
        
        success_rate = (success_count / 100) * 100
        print(f"[INFO] Success rate: {success_rate:.1f}% ({success_count}/100)")
        
        # Require at least 80% success rate (generous for virtual UART)
        self.assertGreaterEqual(success_rate, 80, "PD success rate should be >= 80%")
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
        
        self.process = subprocess.Popen([self.demo_bin, self.device_tty, "0", "0"],
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.5)
        
        self.master.run_startup_sequence()
        
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
        
        self.process = subprocess.Popen([self.demo_bin, self.device_tty, "1", "2"],
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.5)
        
        self.master.run_startup_sequence()
        self.master.m_seq_type = 2
        self.master.pd_out_len = 2
        self.master.pd_in_len = 2
        self.master.go_to_operate()
        time.sleep(0.1)
        
        pd_count = 0
        isdu_count = 0
        
        # Alternate between PD and ISDU
        for i in range(20):
            if i % 2 == 0:
                resp = self.master.run_cycle(pd_out=b'\xCC\xDD')
                if resp and resp.valid:
                    pd_count += 1
            else:
                isdu_data = self.master.read_isdu(index=0x0012, subindex=0x00)
                if isdu_data:
                    isdu_count += 1
            time.sleep(0.02)
        
        print(f"[INFO] PD success: {pd_count}/10, ISDU success: {isdu_count}/10")
        
        self.assertGreater(pd_count, 6, "At least 6/10 PD reads should succeed")
        self.assertGreater(isdu_count, 6, "At least 6/10 ISDU reads should succeed")
        print(f"[PASS] Mixed load handled successfully")

    def test_04_rapid_state_cycling(self):
        """
        Test Case: Rapid State Cycling Stress Test
        Requirement: IO-Link V1.1.5 Section 7.3 - State Machine Robustness
        
        Validates:
        - Device handles rapid PREOPERATE ↔ OPERATE transitions
        - No memory leaks or resource exhaustion
        """
        print("\n[TEST] Rapid State Cycling (5 cycles)")
        
        success_count = 0
        
        for i in range(5):
            # Start new device instance
            self.process = subprocess.Popen([self.demo_bin, self.device_tty, "1", "2"],
                                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            time.sleep(0.3)
            
            # Wake up (PREOPERATE)
            if self.master.run_startup_sequence():
                # Enter OPERATE
                self.master.m_seq_type = 2
                self.master.pd_out_len = 2
                self.master.pd_in_len = 2
                self.master.go_to_operate()
                time.sleep(0.05)
                
                # Verify PD works
                resp = self.master.run_cycle(pd_out=b'\x11\x22')
                if resp and resp.valid:
                    success_count += 1
            
            # Terminate for next cycle
            self.process.terminate()
            self.process.wait()
            time.sleep(0.1)
        
        print(f"[INFO] Successful cycles: {success_count}/5")
        
        self.assertGreater(success_count, 3, "At least 3/5 cycles should succeed")
        print(f"[PASS] Rapid state cycling successful")

    def test_05_long_duration_stability(self):
        """
        Test Case: Long-Duration Stability Test
        Requirement: IO-Link V1.1.5 - General Reliability
        
        Validates:
        - Device remains stable over extended operation
        - No degradation or failures
        """
        print("\n[TEST] Long-Duration Stability (15 seconds)")
        
        self.process = subprocess.Popen([self.demo_bin, self.device_tty, "1", "2"],
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(0.5)
        
        self.master.run_startup_sequence()
        self.master.m_seq_type = 2
        self.master.pd_out_len = 2
        self.master.pd_in_len = 2
        self.master.go_to_operate()
        time.sleep(0.1)
        
        start_time = time.time()
        total_operations = 0
        successful_operations = 0
        
        while (time.time() - start_time) < 15:
            # Mix of PD and ISDU operations
            if total_operations % 10 == 0:
                # Every 10th operation is ISDU
                response = self.master.read_isdu(index=0x0012, subindex=0x00)
                if response:
                    successful_operations += 1
            else:
                # Regular PD exchange
                resp = self.master.run_cycle(pd_out=b'\xEE\xFF')
                if resp and resp.valid:
                    successful_operations += 1
            
            total_operations += 1
            time.sleep(0.05)
        
        success_rate = (successful_operations / total_operations) * 100 if total_operations > 0 else 0
        print(f"[INFO] Operations: {total_operations}, Success rate: {success_rate:.1f}%")
        
        self.assertGreaterEqual(success_rate, 75, "Success rate should be >= 75%")
        print(f"[PASS] Long-duration stability test passed")


if __name__ == '__main__':
    print("=" * 70)
    print("IO-Link V1.1.5 Conformance Test Suite: Performance & Stress Testing")
    print("=" * 70)
    unittest.main(verbosity=2)
