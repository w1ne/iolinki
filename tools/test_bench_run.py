#!/usr/bin/env python3
"""Tests for bench_run.py. A pseudo-terminal stands in for the serial port."""

import json
import os
import pty
import subprocess
import sys
import tempfile
import threading
import time
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import bench_run  # noqa: E402

TOOL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "bench_run.py")


def git_repo(path):
    """Create a committed git repository standing in for an iolinki-master checkout."""
    env = dict(
        os.environ,
        GIT_AUTHOR_NAME="bench",
        GIT_AUTHOR_EMAIL="bench@example.invalid",
        GIT_COMMITTER_NAME="bench",
        GIT_COMMITTER_EMAIL="bench@example.invalid",
    )
    subprocess.run(["git", "init", "-q", path], check=True, env=env)
    with open(os.path.join(path, "README"), "w", encoding="utf-8") as handle:
        handle.write("master\n")
    subprocess.run(["git", "-C", path, "add", "README"], check=True, env=env)
    subprocess.run(
        [
            "git",
            "-C",
            path,
            "-c",
            "core.hooksPath=/dev/null",
            "commit",
            "-q",
            "-m",
            "init",
        ],
        check=True,
        env=env,
    )


class FakeConsole:
    """Write @p text to a pty master after @p delay seconds."""

    def __init__(self, text, delay=0.5):
        self.master, self.slave = pty.openpty()
        self.path = os.ttyname(self.slave)
        self.text = text
        self.delay = delay
        self.thread = threading.Thread(target=self._write, daemon=True)

    def _write(self):
        time.sleep(self.delay)
        if self.text:
            os.write(self.master, self.text.encode("utf-8"))

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *exc):
        self.thread.join()
        os.close(self.master)
        os.close(self.slave)


class TestHelpers(unittest.TestCase):
    def test_reference_identity_reads_the_built_ids(self):
        # examples/reference_device/reference_device.c: vendor_id = 1234, device_id = 5678.
        self.assertEqual(bench_run.reference_identity(), ("04d2", "0000162e"))

    def test_mentions_identity_accepts_hex_with_or_without_prefix(self):
        self.assertTrue(
            bench_run.mentions_identity("VID 0x04D2 DID 0x0000162E", "04d2", "0000162e")
        )
        self.assertTrue(
            bench_run.mentions_identity("vendor=4d2 device=162e", "04d2", "0000162e")
        )

    def test_mentions_identity_rejects_a_log_without_both_ids(self):
        self.assertFalse(
            bench_run.mentions_identity("VID 0x04D2 only", "04d2", "0000162e")
        )
        self.assertFalse(
            bench_run.mentions_identity("VID 0x104D2 DID 162e", "04d2", "0000162e")
        )

    def test_matching_gateway_line_ignores_another_identity(self):
        text = (
            "x\niolinki-gw/1 0 abcd 00045678 aa\n[I] iolinki-gw/1 0 0123 00045678 -\n"
        )
        line, seen = bench_run.matching_gateway_line(text, "0123", "00045678")
        self.assertEqual(line, "iolinki-gw/1 0 0123 00045678 -")
        self.assertEqual(len(seen), 2)

    def test_store_capture_refuses_empty_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaises(bench_run.BenchError):
                bench_run.store_capture(tmp, "  \n", {})
            self.assertFalse(os.path.exists(os.path.join(tmp, "capture.log")))


class TestSensorBench(unittest.TestCase):
    def run_tool(self, console_text, *extra):
        tmp = tempfile.mkdtemp()
        master = os.path.join(tmp, "iolinki-master")
        git_repo(master)
        out = os.path.join(tmp, "out")
        with FakeConsole(console_text) as console:
            proc = subprocess.run(
                [
                    sys.executable,
                    TOOL,
                    "commercial-sensor-on-our-master",
                    "--master-dir",
                    master,
                    "--build-cmd",
                    "true",
                    "--flash-cmd",
                    "true",
                    "--console-serial",
                    console.path,
                    "--counterpart",
                    "ifm IG6215",
                    "--vendor-id",
                    "0123",
                    "--device-id",
                    "00045678",
                    "--notes",
                    "port 0 reached OPERATE with PD",
                    "--timeout",
                    "3",
                    "--out-dir",
                    out,
                    *extra,
                ],
                capture_output=True,
                text=True,
                check=False,
            )
        records = []
        if os.path.isdir(out):
            for run in os.listdir(out):
                path = os.path.join(out, run, "record.json")
                if os.path.exists(path):
                    with open(path, encoding="utf-8") as handle:
                        records.append(json.load(handle))
        return proc, records

    def test_gateway_line_for_the_sensor_records_a_pass(self):
        proc, records = self.run_tool("boot\r\niolinki-gw/1 0 0123 00045678 aabb\r\n")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(len(records), 1)
        self.assertEqual(records[0]["result"], "pass")
        self.assertEqual(
            records[0]["gateway_line"], "iolinki-gw/1 0 0123 00045678 aabb"
        )
        self.assertEqual(len(records[0]["commit"]), 40)

    def test_silent_console_records_nothing(self):
        proc, records = self.run_tool("", "--record-failure")
        self.assertEqual(proc.returncode, 2)
        self.assertIn("no usable serial output", proc.stderr)
        self.assertEqual(records, [])

    def test_line_for_another_device_is_not_a_pass(self):
        proc, records = self.run_tool("iolinki-gw/1 0 abcd 00045678 aabb\n")
        self.assertEqual(proc.returncode, 2)
        self.assertIn("another identity", proc.stderr)
        self.assertEqual(records, [])

    def test_failure_is_recorded_only_on_request_and_keeps_the_log(self):
        proc, records = self.run_tool("startup timeout on port 0\n", "--record-failure")
        self.assertEqual(proc.returncode, 0, proc.stderr)
        self.assertEqual(records[0]["result"], "fail")
        self.assertIn("startup timeout", records[0]["master_log"])

    def test_dirty_master_checkout_is_refused(self):
        tmp = tempfile.mkdtemp()
        git_repo(tmp)
        with open(os.path.join(tmp, "README"), "a", encoding="utf-8") as handle:
            handle.write("edit\n")
        with self.assertRaises(bench_run.BenchError):
            bench_run.clean_commit(tmp)


if __name__ == "__main__":
    unittest.main()
