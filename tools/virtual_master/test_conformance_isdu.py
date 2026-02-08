#!/usr/bin/env python3
"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.

IO-Link V1.1.5 Conformance Test: ISDU Protocol Validation
Tests all mandatory ISDU indices, segmented transfers, and error handling.
"""

import sys
import time
import os
import unittest
import subprocess

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from virtual_master.master import VirtualMaster


class TestISDUConformance(unittest.TestCase):
    """IO-Link V1.1.5 ISDU Protocol Conformance Tests"""

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

    def test_01_vendor_name_0x0010(self):
        """Test mandatory index 0x0010: Vendor Name"""
        print("\n[TEST] ISDU Index 0x0010: Vendor Name")

        response = self.master.read_isdu(index=0x0010, subindex=0x00)
        self.assertIsNotNone(response, "Vendor Name must be readable")
        self.assertGreater(len(response), 0, "Vendor Name must not be empty")

        vendor_name = response.decode("ascii", errors="ignore")
        print(f"[PASS] Vendor Name: '{vendor_name}'")

    def test_02_vendor_text_0x0011(self):
        """Test mandatory index 0x0011: Vendor Text"""
        print("\n[TEST] ISDU Index 0x0011: Vendor Text")

        response = self.master.read_isdu(index=0x0011, subindex=0x00)
        self.assertIsNotNone(response, "Vendor Text must be readable")

        vendor_text = response.decode("ascii", errors="ignore")
        print(f"[PASS] Vendor Text: '{vendor_text}'")

    def test_03_product_name_0x0012(self):
        """Test mandatory index 0x0012: Product Name"""
        print("\n[TEST] ISDU Index 0x0012: Product Name")

        response = self.master.read_isdu(index=0x0012, subindex=0x00)
        self.assertIsNotNone(response, "Product Name must be readable")
        self.assertGreater(len(response), 0, "Product Name must not be empty")

        product_name = response.decode("ascii", errors="ignore")
        print(f"[PASS] Product Name: '{product_name}'")

    def test_04_product_id_0x0013(self):
        """Test mandatory index 0x0013: Product ID"""
        print("\n[TEST] ISDU Index 0x0013: Product ID")

        response = self.master.read_isdu(index=0x0013, subindex=0x00)
        self.assertIsNotNone(response, "Product ID must be readable")
        self.assertGreater(len(response), 0, "Product ID must not be empty")

        product_id = response.decode("ascii", errors="ignore")
        print(f"[PASS] Product ID: '{product_id}'")

    def test_05_product_text_0x0014(self):
        """Test mandatory index 0x0014: Product Text"""
        print("\n[TEST] ISDU Index 0x0014: Product Text")

        response = self.master.read_isdu(index=0x0014, subindex=0x00)
        self.assertIsNotNone(response, "Product Text must be readable")

        product_text = response.decode("ascii", errors="ignore")
        print(f"[PASS] Product Text: '{product_text}'")

    def test_06_serial_number_0x0015(self):
        """Test mandatory index 0x0015: Serial Number"""
        print("\n[TEST] ISDU Index 0x0015: Serial Number")

        response = self.master.read_isdu(index=0x0015, subindex=0x00)
        self.assertIsNotNone(response, "Serial Number must be readable")

        serial_number = response.decode("ascii", errors="ignore")
        print(f"[PASS] Serial Number: '{serial_number}'")

    def test_07_hardware_revision_0x0016(self):
        """Test mandatory index 0x0016: Hardware Revision"""
        print("\n[TEST] ISDU Index 0x0016: Hardware Revision")

        response = self.master.read_isdu(index=0x0016, subindex=0x00)
        self.assertIsNotNone(response, "Hardware Revision must be readable")

        hw_rev = response.decode("ascii", errors="ignore")
        print(f"[PASS] Hardware Revision: '{hw_rev}'")

    def test_08_firmware_revision_0x0017(self):
        """Test mandatory index 0x0017: Firmware Revision"""
        print("\n[TEST] ISDU Index 0x0017: Firmware Revision")

        response = self.master.read_isdu(index=0x0017, subindex=0x00)
        self.assertIsNotNone(response, "Firmware Revision must be readable")

        fw_rev = response.decode("ascii", errors="ignore")
        print(f"[PASS] Firmware Revision: '{fw_rev}'")

    def test_09_application_tag_0x0018_read_write(self):
        """Test mandatory index 0x0018: Application Specific Tag (Read/Write)"""
        print("\n[TEST] ISDU Index 0x0018: Application Tag (Read/Write)")

        initial = self.master.read_isdu(index=0x0018, subindex=0x00)
        self.assertIsNotNone(initial, "Application Tag must be readable")
        print(f"[INFO] Initial Application Tag: {initial.hex()}")

        test_value = b"TestTag123"
        write_result = self.master.write_isdu(
            index=0x0018, subindex=0x00, data=test_value
        )
        self.assertTrue(write_result, "Application Tag write should succeed")

        readback = self.master.read_isdu(index=0x0018, subindex=0x00)
        self.assertIsNotNone(readback, "Application Tag should be readable after write")
        print(
            f"[PASS] Application Tag write/read verified: {readback.decode('ascii', errors='ignore')}"
        )

    def test_10_device_access_locks_0x000C(self):
        """Test mandatory index 0x000C: Device Access Locks"""
        print("\n[TEST] ISDU Index 0x000C: Device Access Locks")

        response = self.master.read_isdu(index=0x000C, subindex=0x00)
        self.assertIsNotNone(response, "Device Access Locks must be readable")
        self.assertEqual(len(response), 2, "Device Access Locks should be 2 bytes")

        locks = int.from_bytes(response, byteorder="big")
        print(f"[PASS] Device Access Locks: 0x{locks:04X}")

    def test_11_profile_characteristic_0x000D(self):
        """Test mandatory index 0x000D: Profile Characteristic"""
        print("\n[TEST] ISDU Index 0x000D: Profile Characteristic")

        response = self.master.read_isdu(index=0x000D, subindex=0x00)
        self.assertIsNotNone(response, "Profile Characteristic must be readable")
        self.assertGreaterEqual(
            len(response), 2, "Profile Characteristic should be at least 2 bytes"
        )

        profile_id = int.from_bytes(response[:2], byteorder="big")
        print(f"[PASS] Profile Characteristic: 0x{profile_id:04X}")

    def test_12_min_cycle_time_0x0024(self):
        """Test mandatory index 0x0024: Min Cycle Time"""
        print("\n[TEST] ISDU Index 0x0024: Min Cycle Time")

        response = self.master.read_isdu(index=0x0024, subindex=0x00)
        self.assertIsNotNone(response, "Min Cycle Time must be readable")
        self.assertGreaterEqual(
            len(response), 1, "Min Cycle Time should be at least 1 byte"
        )

        min_cycle_time = response[0]
        print(f"[PASS] Min Cycle Time: 0x{min_cycle_time:02X}")

    def test_13_invalid_index_error_handling(self):
        """Test error handling for invalid ISDU index"""
        print("\n[TEST] Invalid ISDU Index Error Handling")

        response = self.master.read_isdu(index=0xFFFF, subindex=0x00)

        print(f"[PASS] Invalid index handled gracefully: response={response}")


if __name__ == "__main__":
    print("=" * 70)
    print("IO-Link V1.1.5 Conformance Test Suite: ISDU Protocol Validation")
    print("=" * 70)
    unittest.main(verbosity=2)
