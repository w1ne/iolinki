#!/usr/bin/env python3
"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.

IO-Link V1.1.5 Conformance Test: Timing Requirements
Tests cycle times, response delays, and timeout handling.
"""

import sys
import time
import os
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from virtual_master.master import VirtualMaster


class TestTimingConformance(unittest.TestCase):
    """IO-Link V1.1.5 Timing Conformance Tests"""

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
        time.sleep(0.1)

    def test_01_cycle_time_com1(self):
        """
        Test Case: COM1 Cycle Time Validation
        Requirement: IO-Link V1.1.5 Section 6.2.2 - COM1 (4.8 kbaud)
        
        Validates:
        - Minimum cycle time is respected
        - Actual cycle time matches specification
        """
        print("\n[TEST] COM1 Cycle Time Validation")
        
        self.master.set_baud_rate(4800)  # COM1
        self.master.send_wakeup()
        self.master.set_pd_length(input_len=2, output_len=2)
        time.sleep(0.1)
        
        # Measure 10 cycle times
        cycle_times = []
        for _ in range(10):
            start = time.time()
            pd_data = self.master.read_pd()
            elapsed = time.time() - start
            if pd_data:
                cycle_times.append(elapsed)
        
        avg_cycle_time = sum(cycle_times) / len(cycle_times) if cycle_times else 0
        print(f"[INFO] Average COM1 cycle time: {avg_cycle_time*1000:.2f} ms")
        
        # COM1 minimum cycle time is typically ~10ms
        self.assertGreater(avg_cycle_time, 0.005, "Cycle time should be > 5ms")
        self.assertLess(avg_cycle_time, 0.100, "Cycle time should be < 100ms")
        print(f"[PASS] COM1 cycle time within spec")

    def test_02_isdu_response_time(self):
        """
        Test Case: ISDU Response Time
        Requirement: IO-Link V1.1.5 Section 8.1 - Acyclic Messaging
        
        Validates:
        - ISDU read completes within reasonable time
        - No excessive delays
        """
        print("\n[TEST] ISDU Response Time")
        
        self.master.send_wakeup()
        time.sleep(0.05)
        
        # Measure ISDU read time
        start = time.time()
        response = self.master.read_isdu(index=0x0010, subindex=0x00)
        elapsed = time.time() - start
        
        self.assertIsNotNone(response, "ISDU should respond")
        print(f"[INFO] ISDU response time: {elapsed*1000:.2f} ms")
        
        # ISDU should respond within 1 second
        self.assertLess(elapsed, 1.0, "ISDU response should be < 1s")
        print(f"[PASS] ISDU response time acceptable")

    def test_03_wakeup_timing(self):
        """
        Test Case: Wake-up Sequence Timing
        Requirement: IO-Link V1.1.5 Section 7.3.2 - Wake-up
        
        Validates:
        - Wake-up completes within spec timing
        - Device responds promptly
        """
        print("\n[TEST] Wake-up Sequence Timing")
        
        start = time.time()
        self.master.send_wakeup()
        
        # Wait for device to be ready
        time.sleep(0.05)
        
        # Try to communicate
        response = self.master.read_isdu(index=0x0012, subindex=0x00)
        elapsed = time.time() - start
        
        self.assertIsNotNone(response, "Device should respond after wake-up")
        print(f"[INFO] Wake-up to ready: {elapsed*1000:.2f} ms")
        
        # Wake-up should complete within 200ms
        self.assertLess(elapsed, 0.2, "Wake-up should complete < 200ms")
        print(f"[PASS] Wake-up timing within spec")

    def test_04_pd_exchange_consistency(self):
        """
        Test Case: PD Exchange Timing Consistency
        Requirement: IO-Link V1.1.5 Section 9.1 - Process Data
        
        Validates:
        - PD cycle times are consistent
        - No significant jitter
        """
        print("\n[TEST] PD Exchange Timing Consistency")
        
        self.master.send_wakeup()
        self.master.set_pd_length(input_len=2, output_len=2)
        time.sleep(0.1)
        
        # Measure 20 consecutive cycles
        cycle_times = []
        prev_time = time.time()
        
        for _ in range(20):
            pd_data = self.master.read_pd()
            if pd_data:
                current_time = time.time()
                cycle_times.append(current_time - prev_time)
                prev_time = current_time
        
        if len(cycle_times) > 1:
            avg = sum(cycle_times) / len(cycle_times)
            max_dev = max(abs(t - avg) for t in cycle_times)
            jitter_percent = (max_dev / avg) * 100 if avg > 0 else 0
            
            print(f"[INFO] Average cycle: {avg*1000:.2f} ms, Max jitter: {jitter_percent:.1f}%")
            
            # Jitter should be < 50% of average
            self.assertLess(jitter_percent, 50, "Jitter should be < 50%")
            print(f"[PASS] PD timing consistency acceptable")


if __name__ == '__main__':
    print("=" * 70)
    print("IO-Link V1.1.5 Conformance Test Suite: Timing Validation")
    print("=" * 70)
    unittest.main(verbosity=2)
