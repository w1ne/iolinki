"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.
"""

import subprocess
import os
import time
import sys
import unittest
from virtual_master.master import VirtualMaster


class TestVariablePD(unittest.TestCase):
    def setUp(self):
        self.master = VirtualMaster()
        self.device_tty = self.master.get_device_tty()
        self.env = os.environ.copy()
        self.env["IOLINK_DEVICE_PATH"] = os.environ.get(
            "IOLINK_DEVICE_PATH", "./build/examples/host_demo/host_demo"
        )

    def tearDown(self):
        self.master.close()

    def test_type1_v_pd4(self):
        """Test Type 1_V with 4-byte PD."""
        print("\n[TEST] Type 1_V with PD Len 4")
        process = subprocess.Popen(
            [self.env["IOLINK_DEVICE_PATH"], self.device_tty, "3", "4"],
            stdout=sys.stdout,
            stderr=sys.stderr,
            env=self.env,
        )
        try:
            time.sleep(1)
            self.assertTrue(self.master.run_startup_sequence())
            self.master.m_seq_type = 3  # 1_V
            self.master.pd_out_len = 4
            self.master.pd_in_len = 4
            self.master.go_to_operate()

            test_data = b"\x10\x20\x30\x40"
            for _ in range(3):
                resp = self.master.run_cycle(pd_out=test_data)

            self.assertIsNotNone(resp)
            self.assertTrue(resp.valid)
            self.assertEqual(resp.payload, b"\x11\x21\x31\x41")
            print("   ✅ PD 4-byte exchange successful")

        finally:
            process.terminate()

    def test_type1_v_pd32(self):
        """Test Type 1_V with 32-byte PD (maximum)."""
        print("\n[TEST] Type 1_V with PD Len 32")
        process = subprocess.Popen(
            [self.env["IOLINK_DEVICE_PATH"], self.device_tty, "3", "32"],
            stdout=sys.stdout,
            stderr=sys.stderr,
            env=self.env,
        )
        try:
            time.sleep(1)
            self.assertTrue(self.master.run_startup_sequence())
            self.master.m_seq_type = 3
            self.master.pd_out_len = 32
            self.master.pd_in_len = 32
            self.master.go_to_operate()

            test_data = bytes(range(32))
            for _ in range(3):
                resp = self.master.run_cycle(pd_out=test_data)

            self.assertIsNotNone(resp)
            self.assertTrue(resp.valid)
            expected = bytes([(x + 1) & 0xFF for x in range(32)])
            self.assertEqual(resp.payload, expected)
            print("   ✅ PD 32-byte exchange successful")

        finally:
            process.terminate()

    def test_persistence_application_tag(self):
        """Test if Application Tag persists across reboots (simulated)."""
        print("\n[TEST] Application Tag Persistence")

        process = subprocess.Popen(
            [self.env["IOLINK_DEVICE_PATH"], self.device_tty, "1", "2"],
            stdout=sys.stdout,
            stderr=sys.stderr,
            env=self.env,
        )
        try:
            time.sleep(1)
            self.master.run_startup_sequence()
            self.master.m_seq_type = 2  # 1_2
            self.master.pd_out_len = 2
            self.master.pd_in_len = 2
            self.master.go_to_operate()

            new_tag = "PersistTest"
            self.assertTrue(self.master.write_isdu(0x0018, 0, new_tag.encode()))
            print(f"   ✅ Tag '{new_tag}' written")

            process.terminate()
            process.wait()

            process = subprocess.Popen(
                [self.env["IOLINK_DEVICE_PATH"], self.device_tty, "1", "2"],
                stdout=sys.stdout,
                stderr=sys.stderr,
                env=self.env,
            )
            time.sleep(1)
            self.master.run_startup_sequence()
            self.master.go_to_operate()

            read_tag = self.master.read_isdu(0x0018, 0)
            print(f"   🔍 Tag after reboot: {read_tag}")

            self.assertIsNotNone(read_tag)

        finally:
            process.terminate()


if __name__ == "__main__":
    unittest.main()
