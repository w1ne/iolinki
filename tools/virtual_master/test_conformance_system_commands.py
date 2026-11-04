#!/usr/bin/env python3
"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.

IO-Link V1.1.5 Conformance Test: System Command Validation
Tests the Table B.9 system commands at ISDU index 0x0002.
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

    def test_04_back_to_box_0x83(self):
        """Test system command 0x83: Back-to-box"""
        print("\n[TEST] System Command 0x83: Back-to-box")

        result = self.master.write_isdu(index=0x0002, subindex=0x00, data=bytes([0x83]))
        self.assertTrue(result, "Back-to-box command should succeed")

        print("[PASS] Back-to-box command accepted")

    def test_05_param_upload_start_0x01(self):
        """Test system command 0x01: ParamUploadStart (Table B.9)"""
        print("\n[TEST] System Command 0x01: ParamUploadStart")

        result = self.master.write_isdu(index=0x0002, subindex=0x00, data=bytes([0x01]))
        self.assertTrue(result, "ParamUploadStart should succeed")

        print("[PASS] ParamUploadStart accepted")

    def test_06_param_upload_end_0x02(self):
        """Test system command 0x02: ParamUploadEnd (Table B.9)"""
        print("\n[TEST] System Command 0x02: ParamUploadEnd")

        result = self.master.write_isdu(index=0x0002, subindex=0x00, data=bytes([0x02]))
        self.assertTrue(result, "ParamUploadEnd should succeed")

        print("[PASS] ParamUploadEnd accepted")

    def test_07_param_download_start_0x03(self):
        """Test system command 0x03: ParamDownloadStart (Table B.9)"""
        print("\n[TEST] System Command 0x03: ParamDownloadStart")

        result = self.master.write_isdu(index=0x0002, subindex=0x00, data=bytes([0x03]))
        self.assertTrue(result, "ParamDownloadStart should succeed")

        print("[PASS] ParamDownloadStart accepted")

    def test_08_param_download_end_0x04(self):
        """Test system command 0x04: ParamDownloadEnd (Table B.9)"""
        print("\n[TEST] System Command 0x04: ParamDownloadEnd")

        result = self.master.write_isdu(index=0x0002, subindex=0x00, data=bytes([0x04]))
        self.assertTrue(result, "ParamDownloadEnd should succeed")

        print("[PASS] ParamDownloadEnd accepted")

    def test_09_param_download_store_0x05(self):
        """Test system command 0x05: ParamDownloadStore (Table B.9)"""
        print("\n[TEST] System Command 0x05: ParamDownloadStore")

        result = self.master.write_isdu(index=0x0002, subindex=0x00, data=bytes([0x05]))
        self.assertTrue(result, "ParamDownloadStore should succeed")

        print("[PASS] ParamDownloadStore accepted")

    def test_10_param_break_0x06(self):
        """Test system command 0x06: ParamBreak (Table B.9)"""
        print("\n[TEST] System Command 0x06: ParamBreak")

        result = self.master.write_isdu(index=0x0002, subindex=0x00, data=bytes([0x06]))
        self.assertTrue(result, "ParamBreak should succeed")

        print("[PASS] ParamBreak accepted")

    def test_11_reserved_command_0x84(self):
        """Test reserved system command 0x84 (Table B.9) is rejected"""
        print("\n[TEST] Reserved System Command 0x84")

        result = self.master.write_isdu(index=0x0002, subindex=0x00, data=bytes([0x84]))
        self.assertFalse(result, "Reserved command 0x84 should be rejected")

        print("[PASS] Reserved command 0x84 rejected")

    def test_12_legacy_param_upload_0x95(self):
        """Test legacy system command 0x95 is rejected"""
        print("\n[TEST] Legacy System Command 0x95")

        result = self.master.write_isdu(index=0x0002, subindex=0x00, data=bytes([0x95]))
        self.assertFalse(result, "Legacy command 0x95 should be rejected")

        print("[PASS] Legacy command 0x95 rejected")

    def test_13_invalid_command(self):
        """Test invalid system command error handling"""
        print("\n[TEST] Invalid System Command Error Handling")

        result = self.master.write_isdu(index=0x0002, subindex=0x00, data=bytes([0xFF]))

        self.assertFalse(result, "Invalid command should be rejected")

        print("[PASS] Invalid command rejected")


if __name__ == "__main__":
    print("=" * 70)
    print("IO-Link V1.1.5 Conformance Test Suite: System Commands")
    print("=" * 70)
    unittest.main(verbosity=2)
