#!/usr/bin/env python3
"""Record one IO-Link bench result. Refuse an incomplete pass."""

import argparse
import json
import re
import sys

SCHEMA = "iolinki.bench.v1"
ROLES = ("device-on-commercial-master", "commercial-sensor-on-our-master")
RESULTS = ("pass", "fail")
_COMMIT = re.compile(r"^[0-9a-f]{40}$")
_VENDOR = re.compile(r"^[0-9a-f]{4}$")
_DEVICE = re.compile(r"^[0-9a-f]{8}$")
_GATEWAY = re.compile(r"^iolinki-gw/1 [0-9]+ [0-9a-f]{4} [0-9a-f]{8} ([0-9a-f]+|-)$")
_LEGACY = ("0x0f", "bare 0x00", "0x00 probe")


def build_record(fields):
    role = fields.get("role")
    result = fields.get("result")
    commit = fields.get("commit", "")
    counterpart = fields.get("counterpart", "")
    vendor_id = fields.get("vendor_id", "")
    device_id = fields.get("device_id", "")
    notes = fields.get("notes", "")
    if role not in ROLES:
        raise ValueError("unknown role")
    if result not in RESULTS:
        raise ValueError("result must be pass or fail")
    if _COMMIT.fullmatch(commit) is None:
        raise ValueError("commit must be 40 lowercase hex characters")
    if len(counterpart) < 3:
        raise ValueError("counterpart must name the commercial master or sensor")
    if _VENDOR.fullmatch(vendor_id) is None or _DEVICE.fullmatch(device_id) is None:
        raise ValueError("vendor_id is 4 hex digits and device_id is 8 hex digits")
    if len(notes) < 10:
        raise ValueError("notes must describe what was observed")
    lowered = notes.lower()
    if result == "pass" and any(token in lowered for token in _LEGACY):
        raise ValueError("a pass cannot describe the legacy startup")
    gateway_line = fields.get("gateway_line")
    master_log = fields.get("master_log")
    if result == "pass" and role == "commercial-sensor-on-our-master":
        if gateway_line is None or _GATEWAY.fullmatch(gateway_line) is None:
            raise ValueError("a sensor pass requires a gateway line")
    if result == "pass" and role == "device-on-commercial-master":
        has_line = gateway_line is not None and _GATEWAY.fullmatch(gateway_line) is not None
        has_log = isinstance(master_log, str) and len(master_log) >= 10
        if not has_line and not has_log:
            raise ValueError("a device pass requires a gateway line or a master log")
    doc = {
        "schema": SCHEMA,
        "role": role,
        "result": result,
        "commit": commit,
        "counterpart": counterpart,
        "vendor_id": vendor_id,
        "device_id": device_id,
        "notes": notes,
    }
    if gateway_line is not None:
        doc["gateway_line"] = gateway_line
    if master_log is not None:
        doc["master_log"] = master_log
    return doc


def main(argv):
    parser = argparse.ArgumentParser(description="Record one IO-Link bench result")
    parser.add_argument("--role", required=True)
    parser.add_argument("--result", required=True)
    parser.add_argument("--commit", required=True)
    parser.add_argument("--counterpart", required=True)
    parser.add_argument("--vendor-id", required=True)
    parser.add_argument("--device-id", required=True)
    parser.add_argument("--notes", required=True)
    parser.add_argument("--gateway-line", default=None)
    parser.add_argument("--master-log", default=None)
    parser.add_argument("--output", required=True)
    args = parser.parse_args(argv)
    try:
        doc = build_record(
            {
                "role": args.role,
                "result": args.result,
                "commit": args.commit,
                "counterpart": args.counterpart,
                "vendor_id": args.vendor_id,
                "device_id": args.device_id,
                "notes": args.notes,
                "gateway_line": args.gateway_line,
                "master_log": args.master_log,
            }
        )
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    with open(args.output, "w", encoding="utf-8") as handle:
        json.dump(doc, handle, indent=2)
        handle.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
