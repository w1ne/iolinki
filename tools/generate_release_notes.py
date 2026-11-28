#!/usr/bin/env python3
"""Generate release notes from the canonical changelog and CI evidence."""
import json
import re
import sys
from pathlib import Path
from urllib.parse import quote

ROOT = Path(__file__).resolve().parents[1]
CHANGELOG = ROOT / "CHANGELOG.md"
REPO_URL = "https://github.com/w1ne/iolinki"
VERSION_COMPONENT = r"(?:0|[1-9]\d*)"
PRERELEASE_ID = r"(?:0|[1-9]\d*|\d*[A-Za-z-][0-9A-Za-z-]*)"
SEMVER = re.compile(
    rf"^{VERSION_COMPONENT}\.{VERSION_COMPONENT}\.{VERSION_COMPONENT}"
    rf"(?:-{PRERELEASE_ID}(?:\.{PRERELEASE_ID})*)?$"
)


def release_section(changelog: str, version: str) -> str:
    """Return the Keep a Changelog section for this stable version."""
    stable_version = version.split("-", 1)[0]
    header = re.compile(rf"^## \[{re.escape(stable_version)}\](?:\s+- .*)?\s*$")
    lines = changelog.splitlines()
    start = next((i for i, line in enumerate(lines) if header.match(line)), None)
    if start is None:
        raise ValueError(f"CHANGELOG.md has no [{stable_version}] release entry")
    end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith("## ")), len(lines))
    body = "\n".join(lines[start + 1 : end]).strip()
    if not body:
        raise ValueError(f"CHANGELOG.md [{stable_version}] entry is empty")
    return body


def read_test_stats(path: Path) -> tuple[int, int]:
    data = json.loads(path.read_text(encoding="utf-8"))
    total = int(data.get("total", 0))
    passed = int(data.get("passed", 0))
    failed = int(data.get("failed", 0))
    skipped = int(data.get("skipped", 0))
    if total <= 0 or passed != total or failed or skipped:
        raise ValueError("release notes require a nonempty, passing, unskipped test summary")
    return passed, total


def compare_url(prev_tag: str, comparison_ref: str) -> str:
    prev = quote(prev_tag, safe="/.-")
    current = quote(comparison_ref, safe="/.-")
    return f"{REPO_URL}/compare/{prev}...{current}"


def generate(version: str, prev_tag: str, stats_path: Path, coverage_path: Path,
             comparison_ref: str) -> str:
    if not SEMVER.fullmatch(version):
        raise ValueError(f"invalid SemVer version: {version}")
    match = re.match(r"^v?(\d+)\.(\d+)\.(\d+)", prev_tag)
    section = release_section(CHANGELOG.read_text(encoding="utf-8"), version)
    if match and int(version.split(".", 1)[0]) > int(match.group(1)):
        if "### Migration from" not in section:
            raise ValueError("major release changelog entry must contain a migration section")

    passed, total = read_test_stats(stats_path)
    coverage = coverage_path.read_text(encoding="utf-8").strip()
    if not coverage:
        raise ValueError("coverage summary is empty")

    major = version.split(".", 1)[0]
    migration_doc = f"MIGRATION_{major}.0.md"
    migration_ref = quote(comparison_ref or f"v{version}", safe="/.-")
    section = section.replace(
        f"](docs/{migration_doc})",
        f"]({REPO_URL}/blob/{migration_ref}/docs/{migration_doc})",
    )
    lines = [
        f"# iolinki v{version}",
        "",
        "## Changes",
        "",
        section,
        "",
        "## CI validation for this source commit",
        "",
        f"- Host tests: {passed} passed / {total} total.",
        f"- Line coverage: {coverage}.",
    ]
    if comparison_ref:
        lines.extend(["", f"[Compare {prev_tag} to this source](" +
                      f"{compare_url(prev_tag, comparison_ref)})."])
    return "\n".join(lines) + "\n"


def main(argv: list[str]) -> int:
    if len(argv) != 6:
        print(
            "Usage: generate_release_notes.py <version> <prev_tag> "
            "<test_summary.json> <coverage_summary.txt> <comparison_ref>",
            file=sys.stderr,
        )
        return 2
    try:
        print(generate(argv[1], argv[2], Path(argv[3]), Path(argv[4]), argv[5]), end="")
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        print(f"release notes error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
