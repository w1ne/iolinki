#!/usr/bin/env python3
"""Package a usable native IAR project preserving repository-relative paths."""
import argparse
from pathlib import Path
import zipfile
from check_iar_project import check
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("output", type=Path)
parser.add_argument("--device-root", type=Path, required=True)
parser.add_argument("--core-root", type=Path, required=True)
args = parser.parse_args()
repo = Path(__file__).resolve().parents[1]
check(repo / "examples/stm32g0_tiol112/iar/reference-device.ewp", args.device_root, args.core_root)
args.output.parent.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(args.output, "w", zipfile.ZIP_DEFLATED) as archive:
    for directory in ("src", "include", "examples/reference_device", "examples/stm32g0_tiol112", "docs/hardware"):
        for file in sorted((repo / directory).rglob("*")):
            if file.is_file() and "build" not in file.relative_to(repo).parts:
                archive.write(file, file.relative_to(repo))
    for name in ("LICENSE", "LICENSE.COMMERCIAL", "tools/check_iar_project.py"):
        archive.write(repo / name, name)
    for file in sorted((args.device_root / "Include").rglob("*")):
        if file.is_file(): archive.write(file, "vendor/cmsis-device-g0/" + str(file.relative_to(args.device_root)))
    for name in ("Source/Templates/system_stm32g0xx.c", "Source/Templates/iar/startup_stm32g0b1xx.s"):
        archive.write(args.device_root / name, "vendor/cmsis-device-g0/" + name)
    for file in sorted((args.core_root / "Include").rglob("*")):
        if file.is_file(): archive.write(file, "vendor/CMSIS/Core/" + str(file.relative_to(args.core_root)))
    for base, prefix in ((args.device_root, "vendor/cmsis-device-g0"), (args.core_root.parents[1], "vendor/CMSIS")):
        for file in base.iterdir():
            if file.is_file() and file.name.lower().startswith("license"):
                archive.write(file, prefix + "/" + file.name)
    archive.writestr("OPEN-IN-IAR.txt", """Open examples/stm32g0_tiol112/iar/reference-device.ewp in IAR EWARM.
Tools > Configure Custom Argument Variables:
STM32_CMSIS_ROOT = <extracted folder>\\vendor\\cmsis-device-g0
CMSIS_CORE_ROOT = <extracted folder>\\vendor\\CMSIS\\Core
Choose Debug configuration, confirm STM32G0B1RE and your probe, then Build.
Included ST device revision f576c24e123edf3332988ecd49512c0f35f85186;
CMSIS 5.9.0 revision 61e36449f53c25ef7825c40f7dd93685736f457f.
Project source/include/linker paths are structurally checked in CI.
EWARM compilation is not yet verified; please report EWARM version and compiler log.
See examples/stm32g0_tiol112/README.md for wiring, GCC and flashing instructions.
""")
print(args.output)
