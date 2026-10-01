#!/usr/bin/env python3
"""Resolve native IAR source/include/linker paths; this is not an EWARM build."""
import argparse
from pathlib import Path
import xml.etree.ElementTree as ET

def check(project, device_root, core_root):
    project = Path(project).resolve()
    roots = {"$PROJ_DIR$": project.parent, "$STM32_CMSIS_ROOT$": Path(device_root).resolve(), "$CMSIS_CORE_ROOT$": Path(core_root).resolve()}
    root = ET.parse(project).getroot()
    paths = [element.text for element in root.findall(".//file/name")]
    for option in root.findall(".//option"):
        if option.findtext("name") in ("CCIncludePath2", "AUserIncludes", "IlinkIcfFile"):
            paths.extend(state.text for state in option.findall("state"))
    if not paths: raise ValueError("No project inputs")
    for text in paths:
        if not text: raise ValueError("Empty project input")
        normalized = text.replace("\\", "/")
        for variable, location in roots.items(): normalized = normalized.replace(variable, str(location))
        if "$" in normalized: raise ValueError("Unresolved variable: " + text)
        if not Path(normalized).exists(): raise ValueError("Missing project input: " + normalized)
    return len(paths)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("project", type=Path)
    parser.add_argument("--device-root", type=Path, required=True)
    parser.add_argument("--core-root", type=Path, required=True)
    args = parser.parse_args()
    print(f"IAR structural paths PASS: {check(args.project, args.device_root, args.core_root)} inputs resolved; EWARM compilation is separate")
