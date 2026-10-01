#!/usr/bin/env python3
"""IODD generation, subset semantic/CRC/XSD validation, inspection and ZIP packaging."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile
from iodd_gen import NS, XSI, generate_iodd

N = {'i': NS}
STAMP_TOOL = Path(__file__).with_name('iodd_stamp.mjs')


def load_xml(path):
    data = Path(path).read_bytes()
    if len(data) > 4 * 1024 * 1024:
        raise ValueError('IODD exceeds 4 MiB limit')
    if b'<!DOCTYPE' in data.upper() or b'<!ENTITY' in data.upper():
        raise ValueError('DTD/entity declarations are not supported')
    root = ET.fromstring(data)
    if root.tag != '{' + NS + '}IODevice':
        raise ValueError('Expected IODD 1.1 IODevice namespace 2010/10')
    return root


def validate(path, schema=None):
    root = load_xml(path)
    for tag in ('DocumentInfo', 'ProfileHeader', 'ProfileBody', 'CommNetworkProfile', 'ExternalTextCollection', 'Stamp'):
        if root.find('i:' + tag, N) is None:
            raise ValueError('Missing ' + tag)
    ids = {}
    for node in root.iter():
        id_ = node.get('id')
        if id_:
            if id_ in ids:
                raise ValueError('Duplicate id ' + id_)
            ids[id_] = node
    for node in root.iter():
        for attribute in ('textId', 'variableId', 'datatypeId', 'menuId', 'processDataId'):
            if node.get(attribute) and node.get(attribute) not in ids:
                raise ValueError('Unresolved ' + attribute + ': ' + node.get(attribute))
    indexes = set()
    for node in root.findall('.//i:Variable', N):
        index = int(node.get('index', '-1'))
        if not 16 <= index <= 65535 or index in indexes:
            raise ValueError('Invalid or duplicate variable index')
        indexes.add(index)
    for tag in ('ProcessDataIn', 'ProcessDataOut'):
        for node in root.findall('.//i:' + tag, N):
            bits = int(node.get('bitLength', '0'))
            if not 1 <= bits <= 256:
                raise ValueError('Invalid process bitLength')
            used = set()
            for field in node.findall('i:Datatype/i:RecordItem', N):
                datatype = field.find('i:SimpleDatatype', N)
                if datatype is None:
                    continue  # DatatypeRef is resolved above; full validation needs --schema.
                offset = int(field.get('bitOffset', '-1'))
                width = 1 if datatype.get('{' + XSI + '}type') == 'BooleanT' else int(datatype.get('bitLength', '0'))
                positions = set(range(offset, offset + width))
                if offset < 0 or width < 1 or offset + width > bits or positions & used:
                    raise ValueError('Overlapping or out-of-range process field')
                used |= positions
    result = subprocess.run(['node', str(STAMP_TOOL), 'verify', str(path)], capture_output=True, text=True)
    if result.returncode:
        raise ValueError('CRC validation failed: ' + (result.stdout + result.stderr).strip())
    if schema:
        checked = subprocess.run(['xmllint', '--nonet', '--noout', '--schema', str(schema), str(path)], capture_output=True, text=True)
        if checked.returncode:
            raise ValueError('XSD validation failed: ' + checked.stderr.strip())
    return root


def inspect(root):
    identity = root.find('i:ProfileBody/i:DeviceIdentity', N)
    if identity is None:
        raise ValueError('Missing DeviceIdentity')
    result = {key: int(identity.get(key)) for key in ('vendorId', 'deviceId')}
    result['vendorName'] = identity.get('vendorName')
    result['variables'] = []
    for node in root.findall('.//i:Variable', N):
        dt = node.find('i:Datatype', N)
        result['variables'].append({'id': node.get('id'), 'index': int(node.get('index')), 'accessRights': node.get('accessRights'), 'datatype': None if dt is None else dt.get('{' + XSI + '}type'), 'defaultValue': node.get('defaultValue')})
    result['processData'] = []
    for pd in root.findall('.//i:ProcessData', N):
        item = {'id': pd.get('id')}
        for direction, tag in [('in', 'ProcessDataIn'), ('out', 'ProcessDataOut')]:
            node = pd.find('i:' + tag, N)
            if node is not None:
                item[direction] = {'id': node.get('id'), 'bitLength': int(node.get('bitLength')), 'fields': [{'bitOffset': int(field.get('bitOffset')), 'bitLength': (1 if field.find('i:SimpleDatatype', N).get('{' + XSI + '}type') == 'BooleanT' else int(field.find('i:SimpleDatatype', N).get('bitLength')))} for field in node.findall('i:Datatype/i:RecordItem', N) if field.find('i:SimpleDatatype', N) is not None]}
        result['processData'].append(item)
    result['checker'] = root.find('i:Stamp/i:Checker', N).attrib
    return result


def pack(path, output, schema=None):
    root = validate(path, schema)
    files = {Path(path).name: Path(path).read_bytes()}
    base = Path(path).resolve().parent
    for node in root.iter():
        if node.tag.rsplit('}', 1)[-1] in ('VendorLogo', 'DeviceVariant', 'Connection'):
            for attr in ('name', 'deviceSymbol', 'deviceIcon', 'connectionSymbol'):
                name = node.get(attr)
                if name:
                    if Path(name).name != name or '/' in name or '\\' in name or name in ('.', '..'):
                        raise ValueError('Resource must be a plain filename')
                    resource = (base / name).resolve()
                    if resource.parent != base or not resource.is_file():
                        raise ValueError('Missing or unsafe resource ' + name)
                    files[name] = resource.read_bytes()
    manifest = {'tool': 'iolinki-iodd-cli V1.0', 'validation': 'subset semantic and CRC' + ('; external XSD' if schema else ''), 'files': [{'name': name, 'sha256': hashlib.sha256(data).hexdigest()} for name, data in sorted(files.items())]}
    files['manifest.json'] = (json.dumps(manifest, indent=2, sort_keys=True) + '\n').encode()
    with zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in sorted(files.items()):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    for name in ('generate', 'validate', 'inspect', 'pack'):
        command = commands.add_parser(name)
        command.add_argument('input', type=Path)
        if name in ('generate', 'pack'):
            command.add_argument('-o', '--output', type=Path, required=name == 'pack')
        if name in ('validate', 'pack'):
            command.add_argument('--schema', type=Path, help='Externally obtained IODD1.1.xsd; requires xmllint')
    args = parser.parse_args()
    try:
        if args.command == 'generate':
            print(generate_iodd(json.loads(args.input.read_text()), args.output))
        elif args.command == 'validate':
            validate(args.input, args.schema)
            print('PASS: semantic subset and CRC' + (' and external XSD' if args.schema else ''))
        elif args.command == 'inspect':
            print(json.dumps(inspect(validate(args.input)), indent=2))
        else:
            pack(args.input, args.output, args.schema)
            print(args.output)
    except (ValueError, KeyError, OSError, ET.ParseError, subprocess.CalledProcessError) as error:
        parser.exit(1, 'error: ' + str(error) + '\n')

if __name__ == '__main__':
    main()
