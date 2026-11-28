#!/usr/bin/env python3
"""Focused checks for release-note content and candidate asset URLs."""
import json
import tempfile
import unittest
from pathlib import Path

from generate_release_notes import generate
from release_asset_notes import render


class ReleaseNotesTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        root = Path(self.temporary.name)
        self.stats = root / "test-stats.json"
        self.coverage = root / "coverage.txt"
        self.stats.write_text(json.dumps({"total": 3, "passed": 3}), encoding="utf-8")
        self.coverage.write_text("87.5% (7/8 lines)", encoding="utf-8")

    def test_major_notes_use_changelog_and_current_validation_counts(self):
        notes = generate(
            "3.0.0", "v2.2.0", self.stats, self.coverage, "release/3.0.0"
        )
        self.assertIn("typed reads omit OD", notes)
        self.assertIn("get_response_timing", notes)
        self.assertIn("do not establish physical-master", notes)
        self.assertIn("Missing PHY response timing samples are counted as unavailable", notes)
        self.assertIn("wake receive-enable budget", notes)
        self.assertIn("3 passed / 3 total", notes)
        self.assertIn("87.5%", notes)
        self.assertIn("/compare/v2.2.0...release/3.0.0", notes)
        self.assertIn("/blob/release/3.0.0/docs/MIGRATION_3.0.md", notes)

    def test_release_candidate_uses_stable_changelog_entry(self):
        notes = generate(
            "3.0.0-rc.1", "v2.2.0", self.stats, self.coverage, "release/3.0.0"
        )
        self.assertIn("# iolinki v3.0.0-rc.1", notes)
        self.assertIn("### Migration from 2.2.0", notes)

    def test_incomplete_test_summary_is_rejected(self):
        self.stats.write_text(json.dumps({"total": 3, "passed": 2, "failed": 1}))
        with self.assertRaisesRegex(ValueError, "passing, unskipped"):
            generate("3.0.0", "v2.2.0", self.stats, self.coverage, "release/3.0.0")

    def test_empty_or_unverifiable_coverage_summary_is_rejected(self):
        for summary in ("", "N/A", "87.5%", "87.5% (8/8 lines)", "100.0% (9/8 lines)"):
            with self.subTest(summary=summary):
                self.coverage.write_text(summary, encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "coverage"):
                    generate(
                        "3.0.0", "v2.2.0", self.stats, self.coverage,
                        "release/3.0.0",
                    )

    def test_candidate_assets_have_no_unpublished_download_urls(self):
        notes = render("3.0.0", "candidate", "release/3.0.0")
        self.assertIn("release-candidate-package", notes)
        self.assertIn("three-day", notes)
        self.assertIn("iolinki-3.0.0-firmware.tar.gz", notes)
        self.assertIn("iolinki-3.0.0.cdx.json", notes)
        self.assertNotIn("/releases/download/", notes)
        self.assertIn("GPL-only contributions", notes)
        self.assertIn("/blob/release/3.0.0/docs/COMMERCIAL_SOURCE_SCOPE.md", notes)

    def test_published_assets_link_to_packaged_filenames(self):
        notes = render("3.0.0", "release", "v3.0.0")
        self.assertIn("/releases/download/v3.0.0/iolinki-3.0.0-source.zip", notes)
        self.assertIn("/releases/download/v3.0.0/SHA256SUMS", notes)
        self.assertIn("not IO-Link certification", notes)


if __name__ == "__main__":
    unittest.main()
