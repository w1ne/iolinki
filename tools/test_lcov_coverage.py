#!/usr/bin/env python3
"""Checks for common LCOV tracefile variants and invalid coverage input."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from lcov_coverage import format_coverage, parse_lcov


SCRIPT = Path(__file__).with_name("lcov_coverage.py")


class LcovCoverageTests(unittest.TestCase):
    def test_legacy_tracefile_records_with_optional_test_name_and_branches(self):
        trace = """TN:
SF:src/one.c
FN:1,work
FNDA:2,work
FNF:1
FNH:1
DA:1,2
DA:2,0
BRDA:3,0,0,1
BRF:1
BRH:1
LF:2
LH:1
end_of_record
SF:src/two.c
DA:4,1
LF:1
LH:1
end_of_record
"""
        self.assertEqual(parse_lcov(trace), (2, 3))
        self.assertEqual(format_coverage(*parse_lcov(trace)), "66.7% (2/3 lines)")

    def test_tracefile_with_da_checksums_and_newer_metadata(self):
        trace = """VER:1.2
TN:release
SF:/workspace/src/device.c
FN:10,device_init
FNDA:1,device_init
FNF:1
FNH:1
DA:10,3,8b5c9a
DA:11,0,4f
MCDC:12,0,0,1,0
MCDCF:1
MCDCH:0
LF:2
LH:1
end_of_record
"""
        self.assertEqual(parse_lcov(trace), (1, 2))

    def test_actual_zero_hit_lines_are_reported_as_zero_coverage(self):
        trace = """SF:src/uncovered.c
DA:1,0
DA:2,0
LF:2
LH:0
end_of_record
"""
        self.assertEqual(format_coverage(*parse_lcov(trace)), "0.0% (0/2 lines)")

    def test_empty_and_summary_only_inputs_are_rejected(self):
        for trace in ("", "\n", "Total:|92.5% 3800|97.6% 328\n"):
            with self.subTest(trace=trace), self.assertRaisesRegex(
                ValueError, "LCOV|record"
            ):
                parse_lcov(trace)

    def test_no_executable_line_data_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "no executable line data"):
            parse_lcov("SF:src/empty.c\nLF:0\nLH:0\nend_of_record\n")

    def test_malformed_da_counts_and_orphan_records_are_rejected(self):
        invalid = (
            "SF:src/file.c\nDA:1,-1\nend_of_record\n",
            "SF:src/file.c\nDA:0,1\nend_of_record\n",
            "SF:src/file.c\nDA:one,1\nend_of_record\n",
            "DA:1,1\nend_of_record\n",
            "SF:src/file.c\nDA:1,1,\nend_of_record\n",
        )
        for trace in invalid:
            with self.subTest(trace=trace), self.assertRaises(ValueError):
                parse_lcov(trace)

    def test_duplicate_lines_inconsistent_totals_and_incomplete_records_fail(self):
        duplicate_source = (
            "SF:src/file.c\nDA:1,1\nend_of_record\n"
            "SF:src/file.c\nDA:1,1\nend_of_record\n"
        )
        invalid = (
            "SF:src/file.c\nDA:1,1\nDA:1,0\nend_of_record\n",
            "SF:src/file.c\nDA:1,1\nLF:2\nLH:1\nend_of_record\n",
            "SF:src/file.c\nDA:1,1\nLF:1\nLH:0\nend_of_record\n",
            "SF:src/file.c\nDA:1,1\nLF:1\nend_of_record\n",
            "SF:src/file.c\nDA:1,1\n",
            duplicate_source,
        )
        for trace in invalid:
            with self.subTest(trace=trace), self.assertRaises(ValueError):
                parse_lcov(trace)

    def test_cli_returns_error_for_bad_tracefile_without_coverage_output(self):
        with tempfile.TemporaryDirectory() as directory:
            tracefile = Path(directory) / "coverage.info"
            tracefile.write_text("", encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(SCRIPT), str(tracefile)],
                capture_output=True,
                text=True,
                check=False,
            )
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertIn("lcov coverage error:", result.stderr)

    def test_cli_emits_validated_coverage_summary(self):
        with tempfile.TemporaryDirectory() as directory:
            tracefile = Path(directory) / "coverage.info"
            tracefile.write_text(
                "SF:src/file.c\nDA:1,1\nDA:2,0\nLF:2\nLH:1\nend_of_record\n",
                encoding="utf-8",
            )
            result = subprocess.run(
                [sys.executable, str(SCRIPT), str(tracefile)],
                capture_output=True,
                text=True,
                check=False,
            )
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "50.0% (1/2 lines)\n")
        self.assertEqual(result.stderr, "")


if __name__ == "__main__":
    unittest.main()
