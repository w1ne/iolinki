# IODD command-line tools

Generate an IODD 1.1 XML description from JSON, inspect its process-data and
parameter map, validate it, and create a reproducible ZIP with a SHA-256 manifest.
Python 3.9+ and Node.js 12.22+ are required; a maintained Node.js release is
recommended. There are no pip/npm dependencies. Tests also require a C compiler.

```sh
python3 tools/iodd_cli.py generate examples/reference_device/device.json
python3 tools/iodd_cli.py generate examples/switching_sensor/device.json
python3 tools/iodd_cli.py inspect examples/switching_sensor/device.xml
python3 tools/iodd_cli.py validate examples/switching_sensor/device.xml
python3 tools/iodd_cli.py generate examples/switching_sensor/device.json \
  -o /tmp/iolinki-SwitchingSensor-20261002-IODD1.1.xml
python3 tools/iodd_cli.py pack /tmp/iolinki-SwitchingSensor-20261002-IODD1.1.xml \
  -o /tmp/switching-sensor-iodd.zip
python3 tools/test_iodd_cli.py
```

`python3 tools/iodd_gen.py config.json` remains compatible with the older entry
point. `generate -o` overrides the JSON `output` path. Use the IODD filename
convention `Vendor-Device-YYYYMMDD-IODD1.1.xml` when exporting for engineering tools.
Generation is deterministic: dates come from JSON or the fixed release default,
not the wall clock. The shipped `device.xml` files are reproducible from JSON.

## Included descriptions

- `examples/reference_device/device.json` and `.xml`: counter/button/LED application;
  input 24 bits (counter unsigned 16 bits at offset 8; button byte at offset 0),
  output one byte (LED bit 0). Product ID `reference-device`.
- `examples/switching_sensor/device.json` and `.xml`: unsigned measurement in 0.01
  units at offset 8, valid bit 1, switching-output bit 0. Index 256: threshold
  U16 RW, default 5000; 257: hysteresis U16 RW, default 200; 258: inversion U8
  RW, range 0..1, default 0; 259: teach U8 WO, write 1. Subindex 0; threshold
  must be at least hysteresis. Parameters are volatile. Product ID
  `switching-sensor`.
- `examples/simple_device/simple_device.json` and `.xml`: illustrative legacy
  generator input, not a description of a validated board/application integration.

Process bit offset 0 is the least significant bit of the last byte of the
big-endian process-data record. For example `12 34 03` decodes as measurement
4660, valid true, switching output true. Both application examples use
illustrative vendor ID 1234; device IDs 5678/5679. Replace these with identifiers
assigned to your product before distribution. The reference connection is M12
four-pin: L+, NC, L-, C/Q; set the JSON communication fields for your actual board.

## JSON input

```json
{
  "vendorId": 1234,
  "deviceId": 5679,
  "vendorName": "MyCompany",
  "productName": "Switching sensor",
  "productId": "switching-sensor",
  "releaseDate": "2026-10-02",
  "version": "V1.0",
  "output": "device.xml",
  "physical": {
    "bitrate": "COM2", "minCycleTime": 1000,
    "sioSupported": false, "mSequenceCapability": 11
  },
  "variables": [{
    "id": "V_Threshold", "index": 256, "name": "Threshold",
    "accessRights": "rw", "datatype": "UIntegerT", "bitLength": 16,
    "defaultValue": 5000, "lowerValue": 0, "upperValue": 65535
  }],
  "processData": [{
    "id": "PD_Example",
    "in": { "id": "PD_IN", "bitLength": 24, "fields": [
      {"name": "Measurement", "bitOffset": 8, "bitLength": 16},
      {"name": "Valid", "bitOffset": 1, "bitLength": 1}
    ]}
  }]
}
```

Supported variable datatypes: `UIntegerT`, `IntegerT`, `StringT` (UTF-8), and
`OctetStringT`; strings/octet lengths use `bitLength`, a multiple of eight.
Process data supports one unconditional input/output record, unsigned/signed
integer fields and one-bit booleans. Conditional layouts, events, translations,
custom connection types and complete IO-Link profiles require an extended IODD.
The generated features declare no block parameterization or data storage.

## Official schema validation

`validate` checks the supported semantic subset and CRC. For full XML Schema
validation, supply an externally obtained official `IODD1.1.xsd`; it requires
`xmllint` (`libxml2-utils`). The tool uses `--nonet` and local schema includes.

```sh
curl --fail --location \
  https://io-link.com/fileadmin/user_upload/Downloads/Package_2025/IO-Device-Description_Specification_10.012_V1.1.5_Oct2025.zip \
  -o /tmp/iodd-spec.zip
printf '%s  %s\n' \
  d4b3f53bfc777e45938cf7b7d14fe9b65aa4dccea6875745b3912e1cc59d4dd2 \
  /tmp/iodd-spec.zip | sha256sum --check
unzip /tmp/iodd-spec.zip -d /tmp/iodd-spec
export IODD_SCHEMA=/tmp/iodd-spec/IO-Device-Description_Specification_10.012_V1.1.5_Oct2025/Schemas/IODD1.1.xsd
python3 tools/iodd_cli.py validate examples/switching_sensor/device.xml --schema "$IODD_SCHEMA"
python3 tools/iodd_cli.py pack examples/switching_sensor/device.xml \
  --schema "$IODD_SCHEMA" -o /tmp/sensor.zip
python3 tools/test_iodd_cli.py
```

Expected schema SHA-256:
`53e0e491725b8c744026b1ec7e10b96d0132bae2a03edaccbd222be42e2882d5`.
The specification/schema package retains the IO-Link Community's terms and is
not redistributed in this repository.

## Validation evidence and checksum reuse

Tests exercise generation, shipped-XML drift, index/range/type errors, process
bit overlap, unresolved references, tampered CRCs, reproducible ZIP output and
resource-path traversal. They compile the actual switching-sensor C application
and decode its emitted bytes with IODD offsets; they also check default parameter
read lengths/values and teach/subindex behavior. With `IODD_SCHEMA`, all three
shipped descriptions are checked against the official October 2025 schema.

CRC stamping reuses Calum Knott's MIT-licensed
[IODDForge_Checker](https://github.com/calumk/IODDForge_Checker), pinned to
`f0ad3cf9ba4ceb0d81f0d5cf5bb649b86e4fe99f`. Its unchanged `src/crc.js` is vendored
as `tools/vendor/ioddforge/crc.mjs` with license and provenance. The stamp names
our tool `iolinki-iodd-cli`, not the official IODD Checker. CRC/schema validation
is not official IODD Checker approval or IO-Link device conformance testing.
Run the applicable official checker and product conformance tests before shipping.
