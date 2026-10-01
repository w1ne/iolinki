#!/usr/bin/env python3
"""Generate the supported IODD 1.1 subset from a device JSON description."""
import json
import datetime
from pathlib import Path
import re
import subprocess
import sys
import xml.etree.ElementTree as ET

NS = 'http://www.io-link.com/IODD/2010/10'
XSI = 'http://www.w3.org/2001/XMLSchema-instance'
ET.register_namespace('', NS)
ET.register_namespace('xsi', XSI)


def element(parent, tag, attrs=None, text=None):
    node = ET.SubElement(parent, '{' + NS + '}' + tag, attrs or {})
    if text is not None:
        node.text = str(text)
    return node


def integer(value, label, low, high):
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f'{label} must be an integer from {low} to {high}')
    return value


def validate_config(config):
    if not isinstance(config, dict):
        raise ValueError('Configuration must be a JSON object')
    for key in ('variables', 'processData'):
        if not isinstance(config.get(key, []), list):
            raise ValueError(key + ' must be an array')
    try:
        datetime.date.fromisoformat(config.get('releaseDate', '2026-10-02'))
    except (TypeError, ValueError):
        raise ValueError('releaseDate must be an ISO YYYY-MM-DD date') from None
    if not re.fullmatch(r'V[0-9]+(?:\.[0-9]+){1,7}', config.get('version', 'V1.0')):
        raise ValueError('version must start with V and contain decimal components')
    integer(config.get('vendorId'), 'vendorId', 1, 65535)
    integer(config.get('deviceId'), 'deviceId', 1, 16777215)
    if not config.get('vendorName') or not config.get('productName', 'Example device'):
        raise ValueError('vendorName and productName must be nonempty')
    ids = {'V_DirectParameters_1', 'V_ProductID', 'M_Identification', 'M_Parameter'}
    indexes = set()
    def unique_id(value):
        if not isinstance(value, str) or not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_.-]*', value) or value in ids:
            raise ValueError(f'Invalid or duplicate id: {value}')
        ids.add(value)
    for variable in config.get('variables', []):
        unique_id(variable['id'])
        index = integer(variable['index'], 'index', 16, 65535)
        if index in indexes:
            raise ValueError(f'Duplicate variable index {index}')
        indexes.add(index)
        kind = variable.get('datatype', 'UIntegerT')
        bits = integer(variable.get('bitLength', 8), 'bitLength', 1, 1856)
        if kind not in ('UIntegerT', 'IntegerT', 'StringT', 'OctetStringT'):
            raise ValueError(f'Unsupported datatype {kind}')
        if kind in ('UIntegerT', 'IntegerT') and bits > 64:
            raise ValueError('Integer variables cannot exceed 64 bits; use StringT or OctetStringT')
        if kind in ('UIntegerT', 'IntegerT'):
            if bits < 2:
                raise ValueError('Integer variables require at least two bits')
            minimum = -(1 << (bits - 1)) if kind == 'IntegerT' else 0
            maximum = (1 << (bits - (kind == 'IntegerT'))) - 1
            lower = integer(variable.get('lowerValue', minimum), 'lowerValue', minimum, maximum)
            upper = integer(variable.get('upperValue', maximum), 'upperValue', minimum, maximum)
            if lower > upper:
                raise ValueError('lowerValue exceeds upperValue')
            if 'defaultValue' in variable:
                integer(variable['defaultValue'], 'defaultValue', lower, upper)
        if kind in ('StringT', 'OctetStringT') and bits % 8:
            raise ValueError('String/octet length must be whole bytes')
        if variable.get('accessRights', 'rw') not in ('ro', 'rw', 'wo'):
            raise ValueError('Invalid accessRights')
    data = config.get('processData', [])
    if len(data) != 1:
        raise ValueError('Exactly one unconditional processData entry is supported')
    for pd in data:
        unique_id(pd['id'])
        if not ('in' in pd or 'out' in pd):
            raise ValueError('Process data must have an input or output')
        for direction in ('in', 'out'):
            if direction not in pd:
                continue
            item = pd[direction]
            unique_id(item['id'])
            bits = integer(item['bitLength'], 'process bitLength', 1, 256)
            occupied = set()
            for field in item.get('fields', []):
                offset = integer(field['bitOffset'], 'bitOffset', 0, bits - 1)
                width = integer(field['bitLength'], 'field bitLength', 1, min(64, bits))
                kind = field.get('datatype', 'UIntegerT')
                if kind not in ('UIntegerT', 'IntegerT', 'BooleanT') or (kind == 'BooleanT' and width != 1):
                    raise ValueError('Unsupported process field datatype or width')
                positions = set(range(offset, offset + width))
                if offset + width > bits or positions & occupied:
                    raise ValueError('Process data fields overlap or exceed record size')
                occupied |= positions
    physical = config.get('physical', {})
    if physical.get('bitrate', 'COM2') not in ('COM1', 'COM2', 'COM3'):
        raise ValueError('Invalid bitrate')
    integer(physical.get('minCycleTime', 1000), 'minCycleTime', 1, 132800)
    integer(physical.get('mSequenceCapability', 0), 'mSequenceCapability', 0, 255)


def generate_bytes(config):
    validate_config(config)
    root = ET.Element('{' + NS + '}IODevice', {'{' + XSI + '}schemaLocation': NS + ' IODD1.1.xsd'})
    element(root, 'DocumentInfo', {'copyright': config.get('copyright', 'iolinki-project'), 'releaseDate': config.get('releaseDate', '2026-10-02'), 'version': config.get('version', 'V1.0')})
    header = element(root, 'ProfileHeader')
    for tag, text in [('ProfileIdentification', 'IO Device Profile'), ('ProfileRevision', '1.1'), ('ProfileName', 'Device Profile for IO Devices'), ('ProfileSource', 'IO-Link Consortium'), ('ProfileClassID', 'Device')]:
        element(header, tag, text=text)
    reference = element(header, 'ISO15745Reference')
    for tag, text in [('ISO15745Part', '1'), ('ISO15745Edition', '1'), ('ProfileTechnology', 'IODD')]:
        element(reference, tag, text=text)
    body = element(root, 'ProfileBody')
    identity = element(body, 'DeviceIdentity', {key: str(config[key]) for key in ('vendorId', 'deviceId', 'vendorName')})
    texts = {}
    def textref(parent, tag, id_, value):
        if id_ in texts and texts[id_] != str(value):
            raise ValueError(f'Duplicate text id {id_}')
        texts[id_] = str(value)
        return element(parent, tag, {'textId': id_})
    product = config.get('productName', 'Example device')
    product_id = config.get('productId', 'iolinki-example-' + str(config['deviceId']))
    for tag, value in [('VendorText', config['vendorName']), ('VendorUrl', config.get('vendorUrl', 'https://iolinki.com')), ('DeviceName', product), ('DeviceFamily', config.get('deviceFamily', 'iolinki reference examples'))]:
        textref(identity, tag, 'T_' + tag, value)
    variant = element(element(identity, 'DeviceVariantCollection'), 'DeviceVariant', {'productId': product_id})
    textref(variant, 'Name', 'T_ProductName', product)
    textref(variant, 'Description', 'T_Description', config.get('description', 'Example application; replace illustrative IDs with assigned production identifiers.'))
    function = element(body, 'DeviceFunction')
    element(function, 'Features', {'blockParameter': 'false', 'dataStorage': 'false'})
    variables = element(function, 'VariableCollection')
    # DirectParameters_1 is the mandatory communication-parameter block.
    element(variables, 'StdVariableRef', {'id': 'V_DirectParameters_1'})
    element(variables, 'StdVariableRef', {'id': 'V_ProductID', 'defaultValue': product_id})
    for variable in config.get('variables', []):
        attrs = {key: str(variable[key]) for key in ('id', 'index')}
        attrs['accessRights'] = variable.get('accessRights', 'rw')
        if 'defaultValue' in variable:
            attrs['defaultValue'] = str(variable['defaultValue'])
        node = element(variables, 'Variable', attrs)
        kind = variable.get('datatype', 'UIntegerT')
        bits = variable.get('bitLength', 8)
        dtattrs = {'{' + XSI + '}type': kind}
        if kind in ('StringT', 'OctetStringT'):
            dtattrs['fixedLength'] = str(bits // 8)
            if kind == 'StringT':
                dtattrs['encoding'] = 'UTF-8'
        else:
            dtattrs['bitLength'] = str(bits)
        datatype = element(node, 'Datatype', dtattrs)
        if 'lowerValue' in variable or 'upperValue' in variable:
            element(datatype, 'ValueRange', {'lowerValue': str(variable.get('lowerValue', 0)), 'upperValue': str(variable.get('upperValue', (1 << bits) - 1))})
        textref(node, 'Name', 'T_' + variable['id'], variable['name'])
        if variable.get('description'):
            textref(node, 'Description', 'TD_' + variable['id'], variable['description'])
    collection = element(function, 'ProcessDataCollection')
    for pd in config['processData']:
        pdnode = element(collection, 'ProcessData', {'id': pd['id']})
        for direction, tag in [('in', 'ProcessDataIn'), ('out', 'ProcessDataOut')]:
            if direction not in pd:
                continue
            item = pd[direction]
            node = element(pdnode, tag, {'id': item['id'], 'bitLength': str(item['bitLength'])})
            fields = item.get('fields', [])
            if fields:
                record = element(node, 'Datatype', {'{' + XSI + '}type': 'RecordT', 'bitLength': str(item['bitLength'])})
                for index, field in enumerate(fields, 1):
                    child = element(record, 'RecordItem', {'subindex': str(index), 'bitOffset': str(field['bitOffset'])})
                    dtattrs = {'{' + XSI + '}type': 'BooleanT'} if field['bitLength'] == 1 else {'{' + XSI + '}type': field.get('datatype', 'UIntegerT'), 'bitLength': str(field['bitLength'])}
                    element(child, 'SimpleDatatype', dtattrs)
                    textref(child, 'Name', f'T_{item["id"]}_{index}', field['name'])
            else:
                element(node, 'Datatype', {'{' + XSI + '}type': 'OctetStringT', 'fixedLength': str((item['bitLength'] + 7) // 8)})
            textref(node, 'Name', 'T_' + item['id'], 'Process data ' + direction)
    ui = element(function, 'UserInterface')
    menus = element(ui, 'MenuCollection')
    for role in ('Identification', 'Parameter'):
        menu = element(menus, 'Menu', {'id': 'M_' + role})
        textref(menu, 'Name', 'T_Menu_' + role, role)
        refs = ['V_DirectParameters_1'] if role == 'Identification' or not config.get('variables') else [v['id'] for v in config['variables']]
        for id_ in refs:
            element(menu, 'VariableRef', {'variableId': id_})
    for role in ('Observer', 'Maintenance', 'Specialist'):
        role_node = element(ui, role + 'RoleMenuSet')
        element(role_node, 'IdentificationMenu', {'menuId': 'M_Identification'})
        element(role_node, 'ParameterMenu', {'menuId': 'M_Parameter'})
    comm = element(root, 'CommNetworkProfile', {'{' + XSI + '}type': 'IOLinkCommNetworkProfileT', 'iolinkRevision': 'V1.1'})
    physical = config.get('physical', {})
    layer = element(element(comm, 'TransportLayers'), 'PhysicalLayer', {'bitrate': physical.get('bitrate', 'COM2'), 'minCycleTime': str(physical.get('minCycleTime', 1000)), 'sioSupported': str(physical.get('sioSupported', False)).lower(), 'mSequenceCapability': str(physical.get('mSequenceCapability', 0))})
    connection = element(layer, 'Connection', {'{' + XSI + '}type': 'M12-4ConnectionT'})
    element(connection, 'ProductRef', {'productId': product_id})
    for wire, function_name in [(1, 'L+'), (2, 'NC'), (3, 'L-'), (4, 'C/Q')]:
        element(connection, 'Wire' + str(wire), {'function': function_name})
    element(comm, 'Test')
    language = element(element(root, 'ExternalTextCollection'), 'PrimaryLanguage', {'{http://www.w3.org/XML/1998/namespace}lang': 'en'})
    for id_, value in texts.items():
        element(language, 'Text', {'id': id_, 'value': value})
    stamp = element(root, 'Stamp', {'crc': '0'})
    element(stamp, 'Checker', {'name': 'iolinki-iodd-cli', 'version': 'V1.0'})
    ET.indent(root, space='  ')
    return ET.tostring(root, encoding='utf-8', xml_declaration=True) + b'\n'


def generate_iodd(config, output=None):
    data = generate_bytes(config)
    path = Path(output or config.get('output', 'device.xml'))
    path.write_bytes(data)
    subprocess.run(['node', str(Path(__file__).with_name('iodd_stamp.mjs')), 'write', str(path)], check=True)
    return path


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit('Usage: iodd_gen.py config.json (or use iodd_cli.py --help)')
    print(generate_iodd(json.loads(Path(sys.argv[1]).read_text())))
