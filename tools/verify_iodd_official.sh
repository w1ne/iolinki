#!/usr/bin/env bash
# Official schemas stay in the caller's temporary directory, not in distributed assets.
set -euo pipefail
work="${1:?Usage: verify_iodd_official.sh TEMP_DIRECTORY}"
mkdir -p "$work"
url=https://io-link.com/fileadmin/user_upload/Downloads/Package_2025/IO-Device-Description_Specification_10.012_V1.1.5_Oct2025.zip
curl --fail --location --retry 3 --connect-timeout 15 --max-time 180 "$url" -o "$work/specification.zip"
printf '%s  %s\n' d4b3f53bfc777e45938cf7b7d14fe9b65aa4dccea6875745b3912e1cc59d4dd2 "$work/specification.zip" | sha256sum --check
unzip -q -o "$work/specification.zip" -d "$work"
export IODD_SCHEMA="$work/IO-Device-Description_Specification_10.012_V1.1.5_Oct2025/Schemas/IODD1.1.xsd"
python3 tools/test_iodd_cli.py
