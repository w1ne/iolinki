#!/usr/bin/env python3
"""Tests for bench_record.py."""

import json
import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from bench_record import build_record  # noqa: E402

TOOL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "bench_record.py")
COMMIT = "a" * 40


def sensor_pass(**overrides):
    doc = {
        "role": "commercial-sensor-on-our-master",
        "result": "pass",
        "commit": COMMIT,
        "counterpart": "ifm IG6215",
        "vendor_id": "0123",
        "device_id": "00045678",
        "notes": "OPERATE on port 0 with PD cycle",
        "gateway_line": "iolinki-gw/1 0 0123 00045678 aabb",
    }
    doc.update(overrides)
    return doc


class TestBuildRecord(unittest.TestCase):
    def test_sensor_pass_keeps_the_gateway_line(self):
        doc = build_record(sensor_pass())
        self.assertEqual(doc["schema"], "iolinki.bench.v1")
        self.assertEqual(doc["gateway_line"], "iolinki-gw/1 0 0123 00045678 aabb")

    def test_sensor_pass_accepts_the_c_line_newline_and_strips_it(self):
        doc = build_record(sensor_pass(gateway_line="iolinki-gw/1 0 0123 00045678 aabb\n"))
        self.assertEqual(doc["gateway_line"], "iolinki-gw/1 0 0123 00045678 aabb")

    def test_sensor_pass_rejects_a_line_for_a_different_device(self):
        with self.assertRaises(ValueError):
            build_record(sensor_pass(gateway_line="iolinki-gw/1 0 abcd 00045678 aabb"))

    def test_sensor_pass_without_gateway_line_is_rejected(self):
        fields = sensor_pass()
        del fields["gateway_line"]
        with self.assertRaises(ValueError):
            build_record(fields)

    def test_device_pass_accepts_a_master_log_without_a_gateway_line(self):
        doc = build_record(
            {
                "role": "device-on-commercial-master",
                "result": "pass",
                "commit": COMMIT,
                "counterpart": "Balluff BNI00HP",
                "vendor_id": "0123",
                "device_id": "00045678",
                "notes": "master reached OPERATE",
                "master_log": "OPERATE vendor 0123 device 00045678",
            }
        )
        self.assertEqual(doc["role"], "device-on-commercial-master")

    def test_legacy_startup_note_cannot_pass(self):
        fields = sensor_pass(notes="first frame was bare 0x00 probe")
        with self.assertRaises(ValueError):
            build_record(fields)


class TestCli(unittest.TestCase):
    def test_incomplete_pass_exits_nonzero_and_writes_nothing(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "bench.json")
            proc = subprocess.run(
                [
                    sys.executable,
                    TOOL,
                    "--role",
                    "commercial-sensor-on-our-master",
                    "--result",
                    "pass",
                    "--commit",
                    COMMIT,
                    "--counterpart",
                    "ifm IG6215",
                    "--vendor-id",
                    "0123",
                    "--device-id",
                    "00045678",
                    "--notes",
                    "too incomplete",
                    "--output",
                    out,
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(proc.returncode, 0)
            self.assertFalse(os.path.exists(out))

    def test_cli_pass_writes_json(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = os.path.join(tmp, "bench.json")
            proc = subprocess.run(
                [
                    sys.executable,
                    TOOL,
                    "--role",
                    "commercial-sensor-on-our-master",
                    "--result",
                    "pass",
                    "--commit",
                    COMMIT,
                    "--counterpart",
                    "ifm IG6215",
                    "--vendor-id",
                    "0123",
                    "--device-id",
                    "00045678",
                    "--notes",
                    "OPERATE on port 0 with PD cycle",
                    "--gateway-line",
                    "iolinki-gw/1 0 0123 00045678 aabb",
                    "--output",
                    out,
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(proc.returncode, 0, proc.stderr)
            with open(out, encoding="utf-8") as handle:
                doc = json.load(handle)
            self.assertEqual(doc["schema"], "iolinki.bench.v1")


if __name__ == "__main__":
    unittest.main()
