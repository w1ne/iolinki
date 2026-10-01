#!/usr/bin/env python3
"""Read CTest JUnit rather than parsing localized terminal summaries."""
import json
import sys
import xml.etree.ElementTree as ET
root = ET.parse(sys.argv[1]).getroot()
cases = list(root.iter("testcase"))
failed = sum(case.find("failure") is not None or case.find("error") is not None for case in cases)
skipped = sum(case.find("skipped") is not None for case in cases)
if not cases or failed or skipped:
    raise SystemExit("Release requires nonempty, passing, unskipped tests")
with open(sys.argv[2], "w", encoding="utf-8") as output:
    json.dump({"total": len(cases), "passed": len(cases)}, output)
