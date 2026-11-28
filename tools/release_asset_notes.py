#!/usr/bin/env python3
"""Describe only files packaged by the release workflow."""
import re
import sys

REPO_URL = "https://github.com/w1ne/iolinki"
VERSION_COMPONENT = r"(?:0|[1-9]\d*)"
PRERELEASE_ID = r"(?:0|[1-9]\d*|\d*[A-Za-z-][0-9A-Za-z-]*)"
SEMVER = re.compile(
    rf"^{VERSION_COMPONENT}\.{VERSION_COMPONENT}\.{VERSION_COMPONENT}"
    rf"(?:-{PRERELEASE_ID}(?:\.{PRERELEASE_ID})*)?$"
)


def assets(version: str) -> list[tuple[str, str]]:
    prefix = f"iolinki-{version}"
    return [
        (f"{prefix}-source.zip", "Complete source tree and tests."),
        (f"{prefix}-examples.zip", "Example applications, projects and documentation."),
        (f"{prefix}-firmware.tar.gz", "ESP32-C3, STM32G0 and STM32U5 firmware outputs plus G0/C3 model logs."),
        (f"{prefix}-host-demos.tar.gz", "Linux reference, switching-sensor and simple-device demos plus test_init."),
        (f"{prefix}-stm32g0-tiol112-IAR-project.zip", "Native STM32G0B1RE/TIOL112 IAR project and pinned headers."),
        (f"{prefix}-counter-IODD.zip", "Counter IODD package."),
        (f"{prefix}-switching-sensor-IODD.zip", "Switching-sensor IODD package."),
        (f"{prefix}-labwired-recipes.zip", "LabWired recipes for pinned G0/C3 engines."),
        (f"{prefix}.cdx.json", "CycloneDX SBOM."),
        (f"{prefix}.spdx.json", "SPDX SBOM."),
        ("test-results.xml", "JUnit host-test results."),
        ("test_output.log", "Host-test log."),
        ("test_stats.json", "Machine-readable host-test summary."),
        ("coverage_summary.txt", "Host line-coverage summary."),
        ("iodd-cli-test.log", "IODD schema/CLI validation log."),
        ("host-compiler.txt", "Host compiler version."),
        ("SOURCE_COMMIT", "Source commit used for this package."),
        ("LABWIRED_COMMITS", "Pinned LabWired engine commits."),
        ("SHA256SUMS", "SHA-256 checksums for packaged files."),
    ]


def render(version: str, mode: str, source_ref: str) -> str:
    if not SEMVER.fullmatch(version):
        raise ValueError(f"invalid SemVer version: {version}")
    if mode not in {"candidate", "release"}:
        raise ValueError("mode must be 'candidate' or 'release'")

    lines = ["## Package artifacts", ""]
    if mode == "candidate":
        lines.extend([
            "The PR workflow attaches this package to the `release-candidate-package`",
            "workflow artifact for the tested PR merge commit. The artifact has three-day",
            "retention; release-download links are created only after a tag publishes a release.",
            "",
        ])
        lines.extend(f"- `{name}` — {description}" for name, description in assets(version))
    else:
        base = f"{REPO_URL}/releases/download/v{version}"
        lines.extend(
            f"- [{name}]({base}/{name}) — {description}"
            for name, description in assets(version)
        )

    lines.extend([
        "",
        "## Validation limits",
        "",
        "Software tests, firmware builds and LabWired runs are not IO-Link certification or",
        "third-party physical-Master validation. Synthetic timing tests verify software",
        "boundary handling only; estimated PHY timestamps are not wire qualification.",
        "Complete analog transceiver fault behavior and IO-Link Safety conformance are not",
        "claimed. IODD schema/CLI validation is not official IODD Checker approval, and the",
        "switching-sensor map does not claim standardized Smart Sensor Profile conformance.",
        "Embedded examples do not claim retained flash parameters without a validated NVM",
        "backend. The G0 IAR project paths are checked, but EWARM compilation is not verified.",
        "The STM32U5 item is a Zephyr firmware build.",
        "",
        "The public archive is provided under LICENSE and the individual file notices. It does",
        "not extend proprietary rights to GPL-only contributions or third-party material.",
        f"Commercial source delivery requires final rights review and agreed paths; see [{REPO_URL}/blob/{source_ref}/docs/COMMERCIAL_SOURCE_SCOPE.md]"
        f"({REPO_URL}/blob/{source_ref}/docs/COMMERCIAL_SOURCE_SCOPE.md).",
    ])
    return "\n".join(lines) + "\n"


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print(
            "Usage: release_asset_notes.py <version> <candidate|release> <source_ref>",
            file=sys.stderr,
        )
        return 2
    try:
        print(render(argv[1], argv[2], argv[3]), end="")
    except ValueError as exc:
        print(f"release asset notes error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
