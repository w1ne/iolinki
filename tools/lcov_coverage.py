#!/usr/bin/env python3
"""Calculate line coverage from an LCOV tracefile's executable-line records."""
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path


INTEGER = re.compile(r"^[0-9]+$")
ANCILLARY_RECORDS = {
    "TN", "VER", "FN", "FNDA", "FNF", "FNH", "BRDA", "BRF", "BRH",
    "MCDC", "MCDCF", "MCDCH", "KEY", "TESTNAME",
}


@dataclass
class SourceRecord:
    source: str
    lines: dict[int, int] = field(default_factory=dict)
    reported_found: int | None = None
    reported_hit: int | None = None


def _count(value: str, name: str, line_number: int) -> int:
    if not INTEGER.fullmatch(value):
        raise ValueError(f"line {line_number}: invalid {name} count {value!r}")
    return int(value)


def parse_lcov(text: str) -> tuple[int, int]:
    """Return (hit_lines, total_lines), checking any LCOV per-file totals."""
    records: list[SourceRecord] = []
    source_paths: set[str] = set()
    current: SourceRecord | None = None

    for line_number, raw_line in enumerate(text.splitlines(), start=1):
        line = raw_line.strip()
        if not line:
            continue

        if line.startswith("SF:"):
            if current is not None:
                raise ValueError(
                    f"line {line_number}: nested SF record before end_of_record"
                )
            source = line[3:]
            if not source:
                raise ValueError(f"line {line_number}: source path is empty")
            if source in source_paths:
                raise ValueError(
                    f"line {line_number}: duplicate source record {source!r}"
                )
            source_paths.add(source)
            current = SourceRecord(source)
            continue

        if line == "end_of_record":
            if current is None:
                raise ValueError(
                    f"line {line_number}: end_of_record without an SF record"
                )
            if (current.reported_found is None) != (current.reported_hit is None):
                raise ValueError(
                    f"{current.source}: LF and LH summaries must appear together"
                )
            if current.reported_found is not None:
                actual_found = len(current.lines)
                actual_hit = sum(count > 0 for count in current.lines.values())
                if (
                    current.reported_found != actual_found
                    or current.reported_hit != actual_hit
                ):
                    raise ValueError(
                        f"{current.source}: LCOV LF/LH totals do not match DA records "
                        f"({current.reported_hit}/{current.reported_found} reported, "
                        f"{actual_hit}/{actual_found} calculated)"
                    )
            records.append(current)
            current = None
            continue

        record_type, separator, value = line.partition(":")
        if not separator:
            raise ValueError(f"line {line_number}: invalid LCOV record {line!r}")
        if record_type in {"TN", "VER"}:
            continue
        if current is None:
            raise ValueError(
                f"line {line_number}: {record_type} record without an SF record"
            )

        if record_type == "DA":
            parts = value.split(",")
            if len(parts) not in (2, 3):
                raise ValueError(
                    f"line {line_number}: DA must contain line,count[,checksum]"
                )
            source_line = _count(parts[0], "DA line", line_number)
            hit_count = _count(parts[1], "DA hit", line_number)
            if source_line == 0:
                raise ValueError(
                    f"line {line_number}: DA line numbers must be positive"
                )
            if len(parts) == 3 and not parts[2]:
                raise ValueError(f"line {line_number}: DA checksum must not be empty")
            if source_line in current.lines:
                raise ValueError(
                    f"line {line_number}: duplicate DA line {source_line} "
                    f"in {current.source}"
                )
            current.lines[source_line] = hit_count
            continue

        if record_type in {"LF", "LH"}:
            count = _count(value, record_type, line_number)
            attribute = "reported_found" if record_type == "LF" else "reported_hit"
            if getattr(current, attribute) is not None:
                raise ValueError(f"line {line_number}: duplicate {record_type} summary")
            setattr(current, attribute, count)
            continue

        if record_type in ANCILLARY_RECORDS:
            continue
        raise ValueError(f"line {line_number}: unsupported LCOV record {record_type!r}")

    if current is not None:
        raise ValueError(f"{current.source}: missing end_of_record")
    if not records:
        raise ValueError("LCOV tracefile contains no source records")

    total_lines = sum(len(record.lines) for record in records)
    hit_lines = sum(
        sum(count > 0 for count in record.lines.values()) for record in records
    )
    if total_lines == 0:
        raise ValueError("LCOV tracefile contains no executable line data")
    return hit_lines, total_lines


def format_coverage(hit_lines: int, total_lines: int) -> str:
    if total_lines <= 0 or hit_lines < 0 or hit_lines > total_lines:
        raise ValueError("coverage counts must satisfy 0 <= hit_lines <= total_lines")
    percent = hit_lines * 100 / total_lines
    return f"{percent:.1f}% ({hit_lines}/{total_lines} lines)"


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print("Usage: lcov_coverage.py <filtered_lcov_tracefile>", file=sys.stderr)
        return 2
    try:
        text = Path(argv[1]).read_text(encoding="utf-8")
        print(format_coverage(*parse_lcov(text)))
    except (OSError, ValueError) as exc:
        print(f"lcov coverage error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
