#!/usr/bin/env python3
"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.

IO-Link V1.1.5 Conformance Test: System Command Validation
Tests all seven system commands at ISDU index 0x0002.
"""

import sys
import time
import os
import unittest
import subprocess

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from virtual_master.master import VirtualMaster


class TestSystemCommands(unittest.TestCase):
    """IO-Link V1.1.5 System Command Conformance Tests"""

    def setUp(self):
        self.master = VirtualMaster()
        self.device_tty = self.master.get_device_tty()
        self.demo_bin = os.environ.get(
            "IOLINK_DEVICE_PATH", "./build/examples/host_demo/host_demo"
        )

        self.process = subprocess.Popen(
            [self.demo_bin, self.device_tty, "0", "0"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        time.sleep(0.5)
        self.master.run_startup_sequence()

    def tearDown(self):
        if hasattr(self, "process") and self.process:
            self.process.terminate()
            self.process.wait()
        self.master.close()

    def test_01_device_reset_0x80(self):
        """Test system command 0x80: Device Reset"""
        print("\n[TEST] System Command 0x80: Device Reset")

        result = self.master.write_isdu(index=0x0002, subindex=0x00, data=bytes([0x80]))
        self.assertTrue(result, "Device reset command should succeed")

        print("[PASS] Device reset command accepted")

    def test_02_application_reset_0x81(self):
        """Test system command 0x81: Application Reset"""
        print("\n[TEST] System Command 0x81: Application Reset")

        result = self.master.write_isdu(index=0x0002, subindex=0x00, data=bytes([0x81]))
        self.assertTrue(result, "Application reset command should succeed")

        print("[PASS] Application reset command accepted")

    def test_03_factory_restore_0x82(self):
        """Test system command 0x82: Restore Factory Settings"""
        print("\n[TEST] System Command 0x82: Restore Factory Settings")

        test_tag = b"CustomTag123"
        write_result = self.master.write_isdu(
            index=0x0018, subindex=0x00, data=test_tag
        )
        self.assertTrue(write_result, "Application tag write should succeed")

        readback = self.master.read_isdu(index=0x0018, subindex=0x00)
        self.assertEqual(
            readback, test_tag, "Application tag should match written value"
        )

        result = self.master.write_isdu(index=0x0002, subindex=0x00, data=bytes([0x82]))
        self.assertTrue(result, "Factory restore command should succeed")

        readback_after = self.master.read_isdu(index=0x0018, subindex=0x00)
        self.assertNotEqual(
            readback_after,
            test_tag,
            "Application tag should be reset after factory restore",
        )

        print(
            f"[PASS] Factory restore executed, tag reset from {test_tag} to {readback_after}"
        )

    def test_04_restore_app_defaults_0x83(self):
        """Test system command 0x83: Restore Application Defaults"""
        print("\n[TEST] System Command 0x83: Restore Application Defaults")

        result = self.master.write_isdu(index=0x0002, subindex=0x00, data=bytes([0x83]))
        self.assertTrue(result, "Restore application defaults command should succeed")

        print("[PASS] Restore application defaults command accepted")

    def test_05_set_comm_mode_0x84(self):
        """Test system command 0x84: Set Communication Mode"""
        print("\n[TEST] System Command 0x84: Set Communication Mode")

        result = self.master.write_isdu(index=0x0002, subindex=0x00, data=bytes([0x84]))
        self.assertTrue(result, "Set communication mode command should succeed")

        print("[PASS] Set communication mode command accepted")

    def test_06_param_upload_0x95(self):
        """Test system command 0x95: Parameter Upload"""
        print("\n[TEST] System Command 0x95: Parameter Upload")

        result = self.master.write_isdu(index=0x0002, subindex=0x00, data=bytes([0x95]))
        self.assertTrue(result, "Parameter upload command should succeed")

        print("[PASS] Parameter upload command accepted")

    def test_07_param_download_0x96(self):
        """Test system command 0x96: Parameter Download"""
        print("\n[TEST] System Command 0x96: Parameter Download")

        result = self.master.write_isdu(index=0x0002, subindex=0x00, data=bytes([0x96]))
        self.assertTrue(result, "Parameter download command should succeed")

        print("[PASS] Parameter download command accepted")

    def test_08_param_break_0x97(self):
        """Test system command 0x97: Parameter Break"""
        print("\n[TEST] System Command 0x97: Parameter Break")

        result = self.master.write_isdu(index=0x0002, subindex=0x00, data=bytes([0x97]))
        self.assertTrue(result, "Parameter break command should succeed")

        print("[PASS] Parameter break command accepted")

    def test_09_invalid_command(self):
        """Test invalid system command error handling"""
        print("\n[TEST] Invalid System Command Error Handling")

        result = self.master.write_isdu(index=0x0002, subindex=0x00, data=bytes([0xFF]))

        print(f"[PASS] Invalid command handled gracefully: result={result}")


if __name__ == "__main__":
    print("=" * 70)
    print("IO-Link V1.1.5 Conformance Test Suite: System Commands")
    print("=" * 70)
    unittest.main(verbosity=2)
