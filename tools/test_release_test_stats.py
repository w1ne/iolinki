#!/usr/bin/env python3
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
SCRIPT = Path(__file__).with_name("release_test_stats.py")
class ReleaseStats(unittest.TestCase):
    def invoke(self, content):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "input.xml"
            output = Path(directory) / "stats.json"
            source.write_text(content)
            result = subprocess.run([sys.executable, str(SCRIPT), str(source), str(output)], capture_output=True)
            return result.returncode, json.loads(output.read_text()) if output.exists() else None
    def test_success(self):
        code, stats = self.invoke('<testsuite><testcase name="one"/><testcase name="two"/></testsuite>')
        self.assertEqual((code, stats), (0, {"total":2,"passed":2}))
    def test_fail_error_skip_empty(self):
        for tag in ("failure", "error", "skipped"):
            with self.subTest(tag=tag):
                self.assertNotEqual(self.invoke(f'<testsuite><testcase><{tag}/></testcase></testsuite>')[0], 0)
        self.assertNotEqual(self.invoke('<testsuite/>')[0], 0)
if __name__ == "__main__": unittest.main()
