#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Package the current stack and hosted fixture as a LabWired source request."""

import argparse
import json
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="Output source-request JSON path")
    args = parser.parse_args()
    fixture = Path(__file__).resolve().parent
    root = fixture.parents[1]
    files = [
        {"path": str(path.relative_to(root)), "content": path.read_text()}
        for path in sorted((root / "include/iolinki").glob("*.h"))
    ]
    for name in [
        "device", "dll", "crc", "frame", "isdu", "events", "params",
        "device_info", "data_storage", "platform",
    ]:
        files.append({
            "path": f"src/iolinki_{name}.c",
            "content": (root / f"src/{name}.c").read_text(),
        })
    files.extend([
        {"path": "src/dll_internal.h", "content": (root / "src/dll_internal.h").read_text()},
        {"path": "include/rig_tests.h", "content": (fixture / "rig_tests.h").read_text()},
        {"path": "src/rig_tests.c", "content": (fixture / "rig_tests.c").read_text()},
    ])
    request = {
        "entryPath": "src/main.ino",
        "source": (fixture / "main.ino").read_text(),
        "files": files,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(request, indent=2) + "\n")
    print(f"Wrote {args.output}: entry sketch and {len(files)} companion files")


if __name__ == "__main__":
    main()
