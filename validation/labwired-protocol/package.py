#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Package the current stack and hosted fixture as a LabWired source request."""

import argparse
import json
import re
from pathlib import Path


def hosted_header(text):
    """Remove documentation only; keep source lines and license notices intact."""
    def strip(match):
        comment = match.group(0)
        if "SPDX-License-Identifier" in comment or "Copyright" in comment:
            return comment
        return "\n" * comment.count("\n") or " "

    return re.sub(r"/\*\*.*?\*/", strip, text, flags=re.DOTALL)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path, help="Output source-request JSON path")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--full", action="store_true", help="Keep all headers and documentation")
    mode.add_argument("--hosted", action="store_false", dest="full",
                      help="Fit the hosted source limit (default)")
    parser.set_defaults(full=False)
    args = parser.parse_args()
    fixture = Path(__file__).resolve().parent
    root = fixture.parents[1]
    unused_drivers = {"phy_generic.h", "phy_l6362a.h", "phy_tiol112.h", "phy_virtual.h"}
    files = []
    for path in sorted((root / "include/iolinki").glob("*.h")):
        if not args.full and path.name in unused_drivers:
            continue
        content = path.read_text()
        files.append({"path": str(path.relative_to(root)),
                      "content": content if args.full else hosted_header(content)})
    for name in [
        "device", "dll", "crc", "frame", "isdu", "events", "params",
        "device_info", "data_storage", "platform",
    ]:
        files.append({
            "path": f"src/iolinki_{name}.c",
            "content": (root / f"src/{name}.c").read_text(),
        })
    if args.full:
        files.append({"path": "src/dll_internal.h",
                      "content": (root / "src/dll_internal.h").read_text()})
    files.extend([
        {"path": "include/rig_tests.h", "content": (fixture / "rig_tests.h").read_text()},
        {"path": "src/rig_tests.c", "content": (fixture / "rig_tests.c").read_text()},
    ])
    request = {
        "entryPath": "src/main.ino",
        "source": (fixture / "main.ino").read_text(),
        "files": files,
    }
    source_bytes = len(request["source"].encode()) + sum(
        len(item["content"].encode()) for item in files
    )
    if not args.full and source_bytes > 262144:
        parser.error(f"Hosted source exceeds 262144 bytes: {source_bytes}")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(request, indent=2) + "\n")
    print(f"Wrote {args.output}: entry sketch and {len(files)} companion files, "
          f"{source_bytes} source bytes")


if __name__ == "__main__":
    main()
