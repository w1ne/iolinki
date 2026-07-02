#!/usr/bin/env python3
"""Generate a per-release SBOM for the iolinki device stack.

Emits CycloneDX 1.6 JSON or SPDX 2.3 JSON describing the iolinki library:
a self-contained C stack with zero third-party runtime dependencies. Build-
and test-only tooling (CMake, cmocka) is listed explicitly with a non-runtime
scope so the zero-dependency claim is auditable rather than implied.

Usage:
    python3 tools/generate_sbom.py --version 1.1.3 --format cyclonedx \
        --output iolinki-1.1.3.cdx.json

Set SOURCE_DATE_EPOCH for a reproducible timestamp.
"""

import argparse
import json
import os
import sys
import uuid
from datetime import datetime, timezone

NAME = "iolinki"
SUPPLIER = "Andrii Shylenko"
REPO_URL = "https://github.com/w1ne/iolinki"
DESCRIPTION = (
    "IO-Link device stack in portable C (IO-Link Interface and System "
    "Specification V1.1.5): DLL state machine, ISDU, Data Storage, events, "
    "process data; no third-party runtime dependencies."
)
LICENSE_EXPRESSION = "GPL-3.0-only OR LicenseRef-iolinki-Commercial"

# Build/test-time tooling only; never part of the shipped library.
BUILD_TOOLS = (
    ("cmake", "build system"),
    ("cmocka", "unit-test framework"),
)


def _purl(version):
    return f"pkg:github/w1ne/iolinki@v{version}"


def _timestamp():
    epoch = os.environ.get("SOURCE_DATE_EPOCH")
    when = (
        datetime.fromtimestamp(int(epoch), tz=timezone.utc)
        if epoch
        else datetime.now(tz=timezone.utc)
    )
    return when.strftime("%Y-%m-%dT%H:%M:%SZ")


def build_cyclonedx(version):
    root_ref = f"{NAME}@{version}"
    tool_components = [
        {
            "type": "application",
            "bom-ref": f"buildtool-{name}",
            "name": name,
            "description": f"{role} (build/test only, not shipped)",
            "scope": "excluded",
        }
        for name, role in BUILD_TOOLS
    ]
    return {
        "bomFormat": "CycloneDX",
        "specVersion": "1.6",
        "serialNumber": f"urn:uuid:{uuid.uuid4()}",
        "version": 1,
        "metadata": {
            "timestamp": _timestamp(),
            "supplier": {"name": SUPPLIER},
            "component": {
                "type": "library",
                "bom-ref": root_ref,
                "name": NAME,
                "version": version,
                "description": DESCRIPTION,
                "supplier": {"name": SUPPLIER},
                "licenses": [{"expression": LICENSE_EXPRESSION}],
                "purl": _purl(version),
                "externalReferences": [{"type": "vcs", "url": REPO_URL}],
            },
        },
        "components": tool_components,
        # The root component depends on nothing: the stack is self-contained C.
        "dependencies": [{"ref": root_ref, "dependsOn": []}],
    }


def build_spdx(version):
    root_id = "SPDXRef-Package-iolinki"
    tool_packages = []
    tool_relationships = []
    for name, role in BUILD_TOOLS:
        pkg_id = f"SPDXRef-Package-{name}"
        tool_packages.append(
            {
                "SPDXID": pkg_id,
                "name": name,
                "downloadLocation": "NOASSERTION",
                "filesAnalyzed": False,
                "licenseConcluded": "NOASSERTION",
                "licenseDeclared": "NOASSERTION",
                "comment": f"{role} (build/test only, not shipped)",
            }
        )
        tool_relationships.append(
            {
                "spdxElementId": pkg_id,
                "relationshipType": "BUILD_TOOL_OF",
                "relatedSpdxElement": root_id,
            }
        )
    return {
        "spdxVersion": "SPDX-2.3",
        "dataLicense": "CC0-1.0",
        "SPDXID": "SPDXRef-DOCUMENT",
        "name": f"{NAME}-{version}",
        "documentNamespace": f"{REPO_URL}/spdx/{version}-{uuid.uuid4()}",
        "creationInfo": {
            "created": _timestamp(),
            "creators": [
                f"Person: {SUPPLIER}",
                "Tool: iolinki-generate-sbom",
            ],
        },
        "packages": [
            {
                "SPDXID": root_id,
                "name": NAME,
                "versionInfo": version,
                "supplier": f"Person: {SUPPLIER}",
                "downloadLocation": f"git+{REPO_URL}@v{version}",
                "filesAnalyzed": False,
                "licenseConcluded": LICENSE_EXPRESSION,
                "licenseDeclared": LICENSE_EXPRESSION,
                "description": DESCRIPTION,
                "externalRefs": [
                    {
                        "referenceCategory": "PACKAGE-MANAGER",
                        "referenceType": "purl",
                        "referenceLocator": _purl(version),
                    }
                ],
            },
            *tool_packages,
        ],
        "relationships": [
            {
                "spdxElementId": "SPDXRef-DOCUMENT",
                "relationshipType": "DESCRIBES",
                "relatedSpdxElement": root_id,
            },
            *tool_relationships,
        ],
    }


BUILDERS = {"cyclonedx": build_cyclonedx, "spdx": build_spdx}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--version", required=True, help="release version, no v prefix")
    parser.add_argument("--format", required=True, choices=sorted(BUILDERS))
    parser.add_argument("--output", required=True, help="output JSON path")
    args = parser.parse_args(argv)

    doc = BUILDERS[args.format](args.version)
    with open(args.output, "w", encoding="utf-8") as fh:
        json.dump(doc, fh, indent=2, sort_keys=False)
        fh.write("\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
