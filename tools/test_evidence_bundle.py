#!/usr/bin/env python3
"""Tests for evidence_bundle.py."""

import hashlib
import json
import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from evidence_bundle import (  # noqa: E402
    CRA_SENTENCE,
    GAPS,
    SUPPORT_STATEMENT_12,
    build_evidence,
)

TOOL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "evidence_bundle.py")
FW = "ab" * 32


class TestBuildEvidence(unittest.TestCase):
    def test_twelve_month_bundle_uses_the_fixed_statement(self):
        doc = build_evidence(
            version="1.2.0",
            firmware_sha256=FW,
            support_months=12,
            support_statement=None,
            conformance_command="ctest --test-dir build",
            conformance_exit_code=0,
            log_sha256="cd" * 32,
        )
        self.assertEqual(doc["schema"], "iolinki.evidence.v1")
        self.assertEqual(doc["stack_version"], "1.2.0")
        self.assertEqual(doc["firmware_sha256"], FW)
        self.assertEqual(doc["support_months"], 12)
        self.assertEqual(doc["support_statement"], SUPPORT_STATEMENT_12)
        self.assertEqual(doc["cra"], CRA_SENTENCE)
        self.assertEqual(doc["gaps"], list(GAPS))
        self.assertEqual(doc["conformance"]["exit_code"], 0)
        self.assertEqual(doc["conformance"]["log_sha256"], "cd" * 32)

    def test_other_support_months_require_an_explicit_statement(self):
        with self.assertRaises(ValueError):
            build_evidence(
                version="1.2.0",
                firmware_sha256=FW,
                support_months=60,
                support_statement=None,
                conformance_command="ctest",
                conformance_exit_code=0,
                log_sha256="cd" * 32,
            )

    def test_bad_firmware_hash_is_rejected(self):
        with self.assertRaises(ValueError):
            build_evidence(
                version="1.2.0",
                firmware_sha256="ABCD",
                support_months=12,
                support_statement=None,
                conformance_command="ctest",
                conformance_exit_code=0,
                log_sha256="cd" * 32,
            )


class TestCli(unittest.TestCase):
    def test_cli_writes_sboms_and_hashes_the_log(self):
        with tempfile.TemporaryDirectory() as tmp:
            log_path = os.path.join(tmp, "conformance.log")
            with open(log_path, "w", encoding="utf-8") as handle:
                handle.write("49 passed\n")
            out = os.path.join(tmp, "out")
            proc = subprocess.run(
                [
                    sys.executable,
                    TOOL,
                    "--version",
                    "1.2.0",
                    "--firmware-sha256",
                    FW,
                    "--conformance-command",
                    "ctest --test-dir build",
                    "--conformance-exit-code",
                    "0",
                    "--conformance-log",
                    log_path,
                    "--output-dir",
                    out,
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(proc.returncode, 0, proc.stderr)
            with open(os.path.join(out, "evidence.json"), encoding="utf-8") as handle:
                doc = json.load(handle)
            expect = hashlib.sha256(b"49 passed\n").hexdigest()
            self.assertEqual(doc["conformance"]["log_sha256"], expect)
            self.assertTrue(os.path.isfile(os.path.join(out, "iolinki-1.2.0.cdx.json")))
            self.assertTrue(os.path.isfile(os.path.join(out, "iolinki-1.2.0.spdx.json")))
            with open(os.path.join(out, "iolinki-1.2.0.cdx.json"), encoding="utf-8") as handle:
                cdx = json.load(handle)
            self.assertEqual(cdx["metadata"]["component"]["version"], "1.2.0")


if __name__ == "__main__":
    unittest.main()
