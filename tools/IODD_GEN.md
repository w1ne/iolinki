
# IODD Generator Tool

The `tools/iodd_gen.py` script generates an IO-Link Device Description (IODD) XML file from a JSON configuration. This simplifies the creation of the compliant XML structure required for device integration tools.

## Usage

```bash
python3 tools/iodd_gen.py <config.json>
```

## Configuration Format

Create a JSON file (e.g., `iodd_config.json`) with the following structure:

```json
{
    "vendorId": 1234,
    "vendorName": "MyCompany",
    "deviceId": 5678,
    "output": "MyDevice.xml",
    "variables": [
        {
            "index": 16,
            "name": "Vendor Name",
            "id": "V_VendorName",
            "accessRights": "ro",
            "bitLength": 64
        }
    ],
    "processData": [
        {
            "id": "PD_1",
            "in": { "id": "PD_In", "bitLength": 8 },
            "out": { "id": "PD_Out", "bitLength": 8 }
        }
    ]
}
```

## Features
- Generates valid IODD V1.1 structure.
- Supports ProfileHeader and DeviceIdentity.
- Generates VariableCollection for ISDU parameters.
- Generates ProcessDataCollection for input/output data.
