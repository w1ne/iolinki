#!/usr/bin/env python3
"""Record exact compiler inputs and options; this is build, not runtime, evidence."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git_value(root, *args):
    try:
        return subprocess.check_output(
            ["git", "-C", str(root), *args], text=True, stderr=subprocess.DEVNULL
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--elf", type=Path, required=True)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--backend", type=int, choices=(0, 1, 2), required=True)
    parser.add_argument("--optimization", choices=("s", "2"), required=True)
    parser.add_argument("--clock", type=int, required=True)
    parser.add_argument("--com", type=int, choices=(2, 3), required=True)
    parser.add_argument("--timing", type=int, choices=(0, 1), required=True)
    parser.add_argument("--guard", type=int, required=True)
    parser.add_argument("--protocol-profile", action="store_true")
    parser.add_argument("--device-root", type=Path, required=True)
    parser.add_argument("--core-root", type=Path, required=True)
    parser.add_argument("--hal-root", type=Path)
    parser.add_argument("--input", type=Path, action="append", default=[])
    args = parser.parse_args()
    roots = [("repo", args.repo.resolve()), ("st_device", args.device_root.resolve()),
             ("cmsis_core", args.core_root.resolve())]
    if args.hal_root:
        roots.append(("st_hal", args.hal_root.resolve()))

    def label(path):
        # Vendor directories can be inside the repository checkout.
        for name, root in sorted(roots, key=lambda item: len(str(item[1])), reverse=True):
            try:
                return name + "/" + path.relative_to(root).as_posix()
            except ValueError:
                pass
        raise ValueError("Unclassified compiler input: " + str(path))

    inputs = {p.resolve() for p in args.input}
    for depfile in args.elf.parent.glob("*.d"):
        body = depfile.read_text().replace("\\\n", " ").split(":", 1)[1]
        inputs.update(Path(token).resolve() for token in shlex.split(body))
    manifest = {
        "scope": "Exact build inputs; no simulation, physical, IAR or conformance claim",
        "source_commit": git_value(args.repo, "rev-parse", "HEAD"),
        "source_tree": git_value(args.repo, "rev-parse", "HEAD^{tree}"),
        "tracked_source_diff": git_value(args.repo, "diff", "--stat", "HEAD"),
        "backend": args.backend,
        "backend_name": ("cmsis-irq", "cmsis-dma", "hal-uart-dma")[args.backend],
        "optimization": "-O" + args.optimization,
        "target": "STM32G0B1RE",
        "sysclk_hz": args.clock,
        "protocol_profile": args.protocol_profile,
        "com_mode": args.com if args.protocol_profile else None,
        "internal_dll_timing_enforcement": bool(args.timing) if args.protocol_profile else None,
        "enable_guard_us": args.guard,
        "compiler": subprocess.check_output([args.compiler, "--version"], text=True).strip(),
        "elf_sha256": digest(args.elf),
        "dependencies": {name: git_value(root, "rev-parse", "HEAD")
                         for name, root in roots if name != "repo"},
        "input_sha256": {label(path): digest(path) for path in sorted(inputs)},
    }
    output = args.elf.parent / "firmware-manifest.json"
    output.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    print(output, manifest["elf_sha256"])


if __name__ == "__main__":
    main()
