#!/usr/bin/env python3
import tempfile
import unittest
from pathlib import Path
from check_iar_project import check

class ProjectPaths(unittest.TestCase):
    def test_valid_then_missing_source(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "main.c"
            source.write_text("int main(void) {return 0;}")
            project = root / "test.ewp"
            project.write_text('<project><group><file><name>$PROJ_DIR$\\main.c</name></file></group></project>')
            self.assertEqual(check(project, root, root), 1)
            source.unlink()
            with self.assertRaises(ValueError): check(project, root, root)
    def test_unresolved_variable_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            project = root / "test.ewp"
            project.write_text('<project><group><file><name>$UNKNOWN$\\main.c</name></file></group></project>')
            with self.assertRaises(ValueError): check(project, root, root)
if __name__ == "__main__": unittest.main()
