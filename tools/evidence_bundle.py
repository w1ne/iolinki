#!/usr/bin/env python3
"""Write an iolinki evidence bundle for one customer firmware hash.

The bundle records the stack SBOM, a caller-supplied conformance log, and the
published 12-month update window. It does not execute the conformance command
and it does not mark a device CRA-compliant.
"""

import argparse
import hashlib
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from generate_sbom import build_cyclonedx, build_spdx  # noqa: E402

SCHEMA = "iolinki.evidence.v1"
SUPPORT_STATEMENT_12 = (
    "Updates for this stack release are included for 12 months from delivery. "
    "Renewal is optional. This bundle is stack evidence. It is not an EU "
    "declaration of conformity."
)
CRA_SENTENCE = (
    "The device manufacturer remains responsible for Annex I, the EU "
    "declaration of conformity, firmware update, and secure boot."
)
GAPS = (
    "no BLOB Transfer and Firmware Update profile",
    "no signed firmware update",
    "no secure boot",
)
_SHA256 = re.compile(r"^[0-9a-f]{64}$")


def build_evidence(
    version,
    firmware_sha256,
    support_months,
    support_statement,
    conformance_command,
    conformance_exit_code,
    log_sha256,
):
    if not isinstance(version, str) or version == "":
        raise ValueError("version is required")
    if not isinstance(firmware_sha256, str) or _SHA256.fullmatch(firmware_sha256) is None:
        raise ValueError("firmware_sha256 must be 64 lowercase hex characters")
    if not isinstance(log_sha256, str) or _SHA256.fullmatch(log_sha256) is None:
        raise ValueError("log_sha256 must be 64 lowercase hex characters")
    if not isinstance(support_months, int) or support_months < 1:
        raise ValueError("support_months must be a positive integer")
    if conformance_command == "":
        raise ValueError("conformance_command is required")
    if support_months == 12:
        statement = SUPPORT_STATEMENT_12
    elif support_statement is None or len(support_statement) < 20:
        raise ValueError("support_statement is required when support_months is not 12")
    else:
        statement = support_statement
    return {
        "schema": SCHEMA,
        "stack_name": "iolinki",
        "stack_version": version,
        "firmware_sha256": firmware_sha256,
        "support_months": support_months,
        "support_statement": statement,
        "conformance": {
            "command": conformance_command,
            "exit_code": conformance_exit_code,
            "log_sha256": log_sha256,
        },
        "sbom": {
            "cyclonedx": f"iolinki-{version}.cdx.json",
            "spdx": f"iolinki-{version}.spdx.json",
        },
        "cra": CRA_SENTENCE,
        "gaps": list(GAPS),
    }


def _sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for chunk in iter(lambda: handle.read(65536), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_bundle(output_dir, version, evidence):
    os.makedirs(output_dir, exist_ok=True)
    cdx_name = evidence["sbom"]["cyclonedx"]
    spdx_name = evidence["sbom"]["spdx"]
    with open(os.path.join(output_dir, cdx_name), "w", encoding="utf-8") as handle:
        json.dump(build_cyclonedx(version), handle, indent=2)
        handle.write("\n")
    with open(os.path.join(output_dir, spdx_name), "w", encoding="utf-8") as handle:
        json.dump(build_spdx(version), handle, indent=2)
        handle.write("\n")
    with open(os.path.join(output_dir, "evidence.json"), "w", encoding="utf-8") as handle:
        json.dump(evidence, handle, indent=2)
        handle.write("\n")


def main(argv):
    parser = argparse.ArgumentParser(description="Write an iolinki evidence bundle")
    parser.add_argument("--version", required=True)
    parser.add_argument("--firmware-sha256", required=True)
    parser.add_argument("--support-months", type=int, default=12)
    parser.add_argument("--support-statement", default=None)
    parser.add_argument("--conformance-command", required=True)
    parser.add_argument("--conformance-exit-code", required=True, type=int)
    parser.add_argument("--conformance-log", required=True)
    parser.add_argument("--output-dir", required=True)
    args = parser.parse_args(argv)
    if not os.path.isfile(args.conformance_log):
        print("conformance log does not exist", file=sys.stderr)
        return 2
    try:
        evidence = build_evidence(
            version=args.version,
            firmware_sha256=args.firmware_sha256,
            support_months=args.support_months,
            support_statement=args.support_statement,
            conformance_command=args.conformance_command,
            conformance_exit_code=args.conformance_exit_code,
            log_sha256=_sha256_file(args.conformance_log),
        )
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    write_bundle(args.output_dir, args.version, evidence)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
