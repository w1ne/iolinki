#!/usr/bin/env python3
"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.

IO-Link V1.1.5 Conformance Test: SystemCommand (ISDU index 0x0002, Table B.9)

Expected replies against examples/host_demo (no reset handler registered):

  0x01-0x06  Block Parameterization: Write Response (+) in the correct state,
             0x8036 FUNC_UNAVAILTEMP for End without Start (Table 99).
  0x80       Device reset:        0x8035 FUNC_NOTAVAIL (no handler; 10.7.2).
  0x81       Application reset:   0x8035 FUNC_NOTAVAIL (no handler; 10.7.3).
  0x82       Restore factory:     Write Response (+), tags reset (10.7.4).
  0x83       Back-to-box:         0x8035 FUNC_NOTAVAIL (no handler; 10.7.5).
  0x00, 0x07-0x3F, 0x40-0x7F, 0x84-0x9F, 0xA0-0xFF: 0x8035 FUNC_NOTAVAIL.

The former values 0x95/0x96/0x97 (old ParamUpload/Download/Break), 0x84
(old "set communication mode") and the old 0x83 meaning ("restore application
defaults") are not V1.1.5 SystemCommands. A device that registers a reset
handler answers 0x80/0x81/0x83 positively; that path is covered by the C test
tests/test_integration_ds_reset.c, not by this script.
"""

import sys
import time
import os
import unittest
import subprocess

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from virtual_master.master import VirtualMaster

IDX_SYSTEM_COMMAND = 0x0002
IDX_DATA_STORAGE = 0x0003
IDX_APPLICATION_TAG = 0x0018
DS_SUBIDX_STATE_PROPERTY = 0x02

FUNC_NOTAVAIL = 0x8035
FUNC_UNAVAILTEMP = 0x8036
VAL_LENOVRRUN = 0x8033

PARAM_UPLOAD_START = 0x01
PARAM_UPLOAD_END = 0x02
PARAM_DOWNLOAD_START = 0x03
PARAM_DOWNLOAD_END = 0x04
PARAM_DOWNLOAD_STORE = 0x05
PARAM_BREAK = 0x06
DEVICE_RESET = 0x80
APPLICATION_RESET = 0x81
RESTORE_FACTORY_SETTINGS = 0x82
BACK_TO_BOX = 0x83


class TestSystemCommands(unittest.TestCase):
    """IO-Link V1.1.5 SystemCommand conformance tests (Table B.9)."""

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
        self.assertTrue(self.master.run_startup_sequence(), "startup must succeed")

    def tearDown(self):
        if hasattr(self, "process") and self.process:
            self.process.terminate()
            self.process.wait()
        self.master.close()

    # ---- helpers ---------------------------------------------------------

    def command(self, value: int) -> bool:
        return self.master.write_isdu(
            index=IDX_SYSTEM_COMMAND, subindex=0x00, data=bytes([value])
        )

    def assert_positive(self, value: int) -> None:
        self.assertTrue(
            self.command(value),
            f"SystemCommand 0x{value:02X} must answer Write Response (+), "
            f"got {self._err()}",
        )

    def assert_negative(self, value: int, error_type: int) -> None:
        self.assertFalse(
            self.command(value), f"SystemCommand 0x{value:02X} must be refused"
        )
        self.assertEqual(
            self.master.last_isdu_error,
            error_type,
            f"SystemCommand 0x{value:02X}: expected ErrorType 0x{error_type:04X}, "
            f"got {self._err()}",
        )

    def _err(self) -> str:
        err = self.master.last_isdu_error
        return "no negative response" if err is None else f"0x{err:04X}"

    def write_tag(self, value: bytes) -> None:
        self.assertTrue(
            self.master.write_isdu(index=IDX_APPLICATION_TAG, subindex=0x00, data=value),
            "ApplicationTag write must succeed",
        )

    def read_tag(self) -> bytes:
        value = self.master.read_isdu(index=IDX_APPLICATION_TAG, subindex=0x00)
        self.assertIsNotNone(value, "ApplicationTag read must succeed")
        return value

    # ---- 0x80-0x83 -------------------------------------------------------

    def test_01_device_reset_0x80_without_handler(self):
        """0x80 Device reset: refused, because host_demo cannot execute it."""
        self.assert_negative(DEVICE_RESET, FUNC_NOTAVAIL)

    def test_02_application_reset_0x81_without_handler(self):
        """0x81 Application reset: refused, because host_demo cannot execute it."""
        self.assert_negative(APPLICATION_RESET, FUNC_NOTAVAIL)

    def test_03_factory_restore_0x82(self):
        """0x82 Restore factory settings: positive after the tags are reset."""
        test_tag = b"CustomTag123"
        self.write_tag(test_tag)
        self.assertEqual(self.read_tag(), test_tag)

        self.assert_positive(RESTORE_FACTORY_SETTINGS)

        self.assertEqual(self.read_tag(), b"", "ApplicationTag must be empty")

    def test_04_back_to_box_0x83_without_handler(self):
        """0x83 Back-to-box (not "restore application defaults"): refused
        without a handler, and nothing is reset."""
        self.write_tag(b"Installed")
        self.assert_negative(BACK_TO_BOX, FUNC_NOTAVAIL)
        self.assertEqual(self.read_tag(), b"Installed")

    def test_05_reserved_values_refused(self):
        """Former 0x84 and 0x95-0x97 and other reserved values: FUNC_NOTAVAIL."""
        for value in (0x00, 0x07, 0x3F, 0x40, 0x7F, 0x84, 0x95, 0x96, 0x97, 0x9F, 0xA0):
            with self.subTest(value=f"0x{value:02X}"):
                self.assert_negative(value, FUNC_NOTAVAIL)

    # ---- 0x01-0x06 Block Parameterization ---------------------------------

    def test_06_param_upload_block_0x01_0x02(self):
        """ParamUploadStart 0x01 / ParamUploadEnd 0x02."""
        self.assert_negative(PARAM_UPLOAD_END, FUNC_UNAVAILTEMP)
        self.assert_positive(PARAM_UPLOAD_START)
        self.assert_negative(PARAM_UPLOAD_START, FUNC_UNAVAILTEMP)
        self.assertIsNotNone(self.master.read_isdu(index=IDX_APPLICATION_TAG))
        self.assert_positive(PARAM_UPLOAD_END)

    def test_07_param_download_block_0x03_0x04(self):
        """ParamDownloadStart 0x03 stages writes; ParamDownloadEnd 0x04 commits."""
        self.write_tag(b"Old")
        self.assert_negative(PARAM_DOWNLOAD_END, FUNC_UNAVAILTEMP)
        self.assert_positive(PARAM_DOWNLOAD_START)
        self.write_tag(b"New")
        self.assertEqual(self.read_tag(), b"Old", "staged value must not be active")
        self.assert_positive(PARAM_DOWNLOAD_END)
        self.assertEqual(self.read_tag(), b"New")

    def test_08_param_break_0x06(self):
        """ParamBreak 0x06 discards the staged block."""
        self.write_tag(b"Keep")
        self.assert_positive(PARAM_DOWNLOAD_START)
        self.write_tag(b"Drop")
        self.assert_positive(PARAM_BREAK)
        self.assertEqual(self.read_tag(), b"Keep")

    def test_09_param_download_store_0x05(self):
        """ParamDownloadStore 0x05 commits and sets DS_UPLOAD_FLAG (Table B.10)."""
        self.assert_positive(PARAM_DOWNLOAD_START)
        self.write_tag(b"Stored")
        self.assert_positive(PARAM_DOWNLOAD_STORE)
        self.assertEqual(self.read_tag(), b"Stored")
        prop = self.master.read_isdu(
            index=IDX_DATA_STORAGE, subindex=DS_SUBIDX_STATE_PROPERTY
        )
        self.assertIsNotNone(prop, "State_Property read must succeed")
        self.assertEqual(prop[0] & 0x80, 0x80, "DS_UPLOAD_FLAG must be set")

    def test_10_invalid_command_0xff(self):
        """0xFF (vendor range, not implemented): FUNC_NOTAVAIL."""
        self.assert_negative(0xFF, FUNC_NOTAVAIL)

    def test_11_length_overrun(self):
        """SystemCommand is one octet: two octets are VAL_LENOVRRUN."""
        self.assertFalse(
            self.master.write_isdu(
                index=IDX_SYSTEM_COMMAND, subindex=0x00, data=bytes([0x82, 0x00])
            )
        )
        self.assertEqual(self.master.last_isdu_error, VAL_LENOVRRUN)


if __name__ == "__main__":
    print("=" * 70)
    print("IO-Link V1.1.5 Conformance Test Suite: SystemCommand (Table B.9)")
    print("=" * 70)
    unittest.main(verbosity=2)
