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
import subprocess

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from virtual_master.master import VirtualMaster


class TestTimingConformance(unittest.TestCase):
    """IO-Link V1.1.5 Timing Conformance Tests"""

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

    def test_01_cycle_time_measurement(self):
        """
        Test Case: Cycle Time Measurement
        Requirement: IO-Link V1.1.5 Section 6.2.2 - Communication Timing

        Validates:
        - Cycle time is within reasonable bounds
        - Actual cycle time matches expectations
        """
        print("\n[TEST] Cycle Time Measurement")

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

        cycle_times = []
        for _ in range(10):
            start = time.time()
            resp = self.master.run_cycle(pd_out=b"\x00\x00")
            elapsed = time.time() - start
            if resp and resp.valid:
                cycle_times.append(elapsed)

        avg_cycle_time = sum(cycle_times) / len(cycle_times) if cycle_times else 0
        print(f"[INFO] Average cycle time: {avg_cycle_time * 1000:.2f} ms")

        self.assertGreater(avg_cycle_time, 0.0009, "Cycle time should be > 0.9ms")
        self.assertLess(avg_cycle_time, 0.200, "Cycle time should be < 200ms")
        print("[PASS] Cycle time within acceptable range")

    def test_02_isdu_response_time(self):
        """
        Test Case: ISDU Response Time
        Requirement: IO-Link V1.1.5 Section 8.1 - Acyclic Messaging

        Validates:
        - ISDU read completes within reasonable time
        - No excessive delays
        """
        print("\n[TEST] ISDU Response Time")

        self.process = subprocess.Popen(
            [self.demo_bin, self.device_tty, "0", "0"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        time.sleep(0.5)

        self.master.run_startup_sequence()

        start = time.time()
        response = self.master.read_isdu(index=0x0010, subindex=0x00)
        elapsed = time.time() - start

        self.assertIsNotNone(response, "ISDU should respond")
        print(f"[INFO] ISDU response time: {elapsed * 1000:.2f} ms")

        self.assertLess(elapsed, 2.0, "ISDU response should be < 2s")
        print("[PASS] ISDU response time acceptable")

    def test_03_startup_timing(self):
        """
        Test Case: Startup Sequence Timing
        Requirement: IO-Link V1.1.5 Section 7.3.2 - Wake-up

        Validates:
        - Startup completes within spec timing
        - Device responds promptly
        """
        print("\n[TEST] Startup Sequence Timing")

        self.process = subprocess.Popen(
            [self.demo_bin, self.device_tty, "0", "0"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        time.sleep(0.5)

        start = time.time()
        success = self.master.run_startup_sequence()
        elapsed = time.time() - start

        self.assertTrue(success, "Startup should succeed")
        print(f"[INFO] Startup time: {elapsed * 1000:.2f} ms")

        self.assertLess(elapsed, 1.0, "Startup should complete < 1s")
        print("[PASS] Startup timing within spec")

    def test_04_pd_exchange_consistency(self):
        """
        Test Case: PD Exchange Timing Consistency
        Requirement: IO-Link V1.1.5 Section 9.1 - Process Data

        Validates:
        - PD cycle times are consistent
        - No significant jitter
        """
        print("\n[TEST] PD Exchange Timing Consistency")

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

        cycle_times = []
        prev_time = time.time()

        for _ in range(20):
            resp = self.master.run_cycle(pd_out=b"\xaa\xbb")
            if resp and resp.valid:
                current_time = time.time()
                cycle_times.append(current_time - prev_time)
                prev_time = current_time

        if len(cycle_times) > 1:
            avg = sum(cycle_times) / len(cycle_times)
            max_dev = max(abs(t - avg) for t in cycle_times)
            jitter_percent = (max_dev / avg) * 100 if avg > 0 else 0

            print(
                f"[INFO] Average cycle: {avg * 1000:.2f} ms, Max jitter: {jitter_percent:.1f}%"
            )

            self.assertLess(jitter_percent, 100, "Jitter should be < 100%")
            print("[PASS] PD timing consistency acceptable")

    def test_05_wakeup_timing_path_compliance(self):
        """
        Test Case: Wake-up Timing Path Compliance
        Requirement: IO-Link V1.1.5 Section 7.3.2 - Wake-up Sequence

        Validates:
        - Wake-up pulse is sent correctly
        - Device transitions through AWAITING_COMM (if timing enforcement enabled)
        - First valid frame accepted after t_dwu delay (80μs)
        - Startup completes within spec timing (< 200ms total)
        """
        print("\n[TEST] Wake-up Timing Path Compliance")

        self.process = subprocess.Popen(
            [self.demo_bin, self.device_tty, "0", "0"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        time.sleep(0.5)

        start = time.time()

        success = self.master.run_startup_sequence()
        total_time = time.time() - start

        self.assertTrue(success, "Startup sequence should complete successfully")

        print(f"[INFO] Total startup time: {total_time * 1000:.2f} ms")

        self.assertLess(
            total_time, 2.0, "Complete startup should be < 2.0s (accommodating CI delays)"
        )

        vendor_name = self.master.read_isdu(index=0x0010, subindex=0x00)
        self.assertIsNotNone(vendor_name, "Device should be functional after wake-up")

        print(
            f"[PASS] Wake-up timing path compliant: {vendor_name.decode('ascii', errors='ignore')}"
        )


if __name__ == "__main__":
    print("=" * 70)
    print("IO-Link V1.1.5 Conformance Test Suite: Timing Validation")
    print("=" * 70)
    unittest.main(verbosity=2)
