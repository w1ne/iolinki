import copy
import ctypes
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
CLI = ROOT / 'tools/iodd_cli.py'
NS = {'i': 'http://www.io-link.com/IODD/2010/10'}

class IoddCLI(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.xml = Path(self.tmp.name) / 'device.xml'
        self.config = json.loads((ROOT / 'examples/switching_sensor/device.json').read_text())

    def run_cli(self, *args, success=True):
        result = subprocess.run([sys.executable, str(CLI), *map(str, args)], capture_output=True, text=True)
        self.assertEqual(result.returncode == 0, success, result.stdout + result.stderr)
        return result

    def generate(self, config=None):
        path = Path(self.tmp.name) / 'config.json'
        path.write_text(json.dumps(config or self.config))
        return self.run_cli('generate', path, '-o', self.xml)

    def test_generation_inspection_and_tamper_detection(self):
        self.generate()
        self.run_cli('validate', self.xml)
        info = json.loads(self.run_cli('inspect', self.xml).stdout)
        self.assertEqual(info['deviceId'], 5679)
        self.assertEqual([v['index'] for v in info['variables']], [256, 257, 258, 259])
        self.assertEqual(info['processData'][0]['in']['bitLength'], 24)
        tree = ET.parse(self.xml)
        self.assertEqual(tree.find('.//i:Variable/i:Datatype', NS).get('{http://www.w3.org/2001/XMLSchema-instance}type'), 'UIntegerT')
        self.xml.write_bytes(self.xml.read_bytes().replace(b'Switching sensor', b'Switching SENSOR', 1))
        self.run_cli('validate', self.xml, success=False)

    def test_duplicate_index_and_overlapping_process_bits_rejected(self):
        for mutate in ('index', 'bits'):
            cfg = copy.deepcopy(self.config)
            if mutate == 'index':
                cfg['variables'][1]['index'] = cfg['variables'][0]['index']
            else:
                cfg['processData'][0]['in']['fields'][1]['bitOffset'] = 8
            path = Path(self.tmp.name) / 'bad.json'
            path.write_text(json.dumps(cfg))
            self.run_cli('generate', path, '-o', self.xml, success=False)
            self.assertFalse(self.xml.exists())

    def test_pack_is_reproducible_and_contains_manifest(self):
        self.generate()
        a, b = [Path(self.tmp.name) / n for n in ('a.zip', 'b.zip')]
        self.run_cli('pack', self.xml, '-o', a)
        self.run_cli('pack', self.xml, '-o', b)
        self.assertEqual(a.read_bytes(), b.read_bytes())
        with zipfile.ZipFile(a) as archive:
            self.assertEqual(set(archive.namelist()), {'device.xml', 'manifest.json'})
            self.assertIn('sha256', json.loads(archive.read('manifest.json'))['files'][0])

    def test_dangling_text_reference_rejected_even_with_correct_crc(self):
        self.generate()
        self.xml.write_bytes(self.xml.read_bytes().replace(b'textId="T_DeviceName"', b'textId="T_Missing"', 1))
        subprocess.run(['node', str(ROOT / 'tools/iodd_stamp.mjs'), 'write', str(self.xml)], check=True, capture_output=True)
        self.run_cli('validate', self.xml, success=False)

    def test_pack_rejects_resource_path_traversal(self):
        self.generate()
        data = self.xml.read_bytes().replace(b'<DeviceName ', b'<VendorLogo name="../outside.png" /><DeviceName ', 1)
        self.xml.write_bytes(data)
        subprocess.run(['node', str(ROOT / 'tools/iodd_stamp.mjs'), 'write', str(self.xml)], check=True, capture_output=True)
        self.run_cli('pack', self.xml, '-o', Path(self.tmp.name) / 'bad.zip', success=False)

    def test_generator_keeps_output_on_invalid_input(self):
        self.generate()
        original = self.xml.read_bytes()
        cfg = copy.deepcopy(self.config)
        cfg['variables'][0]['bitLength'] = 128
        path = Path(self.tmp.name) / 'bad.json'
        path.write_text(json.dumps(cfg))
        self.run_cli('generate', path, '-o', self.xml, success=False)
        self.assertEqual(self.xml.read_bytes(), original)

    def test_wrong_datatype_reference_rejected(self):
        self.generate()
        tree = ET.parse(self.xml)
        variable = tree.find('.//i:Variable', NS)
        variable.remove(variable.find('i:Datatype', NS))
        variable.insert(0, ET.Element('{'+NS['i']+'}DatatypeRef', {'datatypeId': 'D_missing'}))
        tree.write(self.xml, encoding='utf-8', xml_declaration=True)
        self.run_cli('validate', self.xml, success=False)

    def test_example_maps_and_boolean_fields(self):
        for name, device_id, output_bits in [('reference_device',5678,8), ('switching_sensor',5679,0)]:
            self.run_cli('generate', ROOT / f'examples/{name}/device.json', '-o', self.xml)
            info = json.loads(self.run_cli('inspect', self.xml).stdout)
            self.assertEqual(info['deviceId'], device_id)
            self.assertEqual(info['processData'][0]['in']['fields'][0], {'bitOffset':8,'bitLength':16})
            self.assertEqual(info['processData'][0].get('out',{}).get('bitLength',0), output_bits)
            tree = ET.parse(self.xml)
            self.assertEqual(tree.find('.//i:PhysicalLayer',NS).get('mSequenceCapability'), '11')
            if name == 'switching_sensor':
                self.assertEqual([v['defaultValue'] for v in info['variables']], ['5000','200','0',None])

    def test_invalid_test_configs_rejected_before_output(self):
        for test in [{'Config2': {'index': 24, 'testValue': '0x49'}},
                     {'Config1': {'index': 24, 'testValue': ','.join(['0x49'] * 13)}},
                     {'Config3': {'index': 24, 'testValue': '0x49'}},
                     {'Config1': {'index': 24, 'testValue': 'arbitrary'}}]:
            cfg = copy.deepcopy(self.config)
            cfg['testConfig'] = test
            path = Path(self.tmp.name) / 'invalid-tests.json'
            path.write_text(json.dumps(cfg))
            self.run_cli('generate', path, '-o', self.xml, success=False)
            self.assertFalse(self.xml.exists())

    def test_custom_variables_cannot_collide_with_declared_standard_indices(self):
        for index in (18, 19, 24):
            cfg = copy.deepcopy(self.config)
            cfg['variables'][0]['index'] = index
            path = Path(self.tmp.name) / 'standard-collision.json'
            path.write_text(json.dumps(cfg))
            self.run_cli('generate', path, '-o', self.xml, success=False)
            self.assertFalse(self.xml.exists())

    def test_generated_business_rule_requirements(self):
        for name, value in [('reference_device', '0x00'), ('switching_sensor', '0x13,0x88')]:
            self.run_cli('generate', ROOT / f'examples/{name}/device.json', '-o', self.xml)
            tree = ET.parse(self.xml)
            refs = {n.get('id') for n in tree.findall('.//i:StdVariableRef', NS)}
            self.assertTrue({'V_DirectParameters_2', 'V_ProductName', 'V_ApplicationSpecificTag'} <= refs)
            self.assertEqual(tree.find('.//i:Config1', NS).attrib, {'index': '24', 'testValue': '0x49'})
            self.assertEqual(tree.find('.//i:Config2', NS).attrib, {'index': '256', 'testValue': value})
            self.assertEqual(len(tree.find('.//i:Config3', NS).get('testValue').split(',')), 13)
            if name == 'switching_sensor':
                dt = tree.find('.//i:Variable[@id="V_Teach"]/i:Datatype', NS)
                self.assertIsNone(dt.find('i:ValueRange', NS))
                self.assertEqual(dt.find('i:SingleValue', NS).get('value'), '1')

    def test_release_packages_use_canonical_names_and_exact_sources(self):
        subprocess.run([sys.executable, str(ROOT / 'tools/package_release_iodds.py'), self.tmp.name, '2.1.1'], check=True)
        for example, name in [('reference_device', 'counter'), ('switching_sensor', 'switching-sensor')]:
            with zipfile.ZipFile(Path(self.tmp.name) / f'iolinki-2.1.1-{name}-IODD.zip') as archive:
                filename = f'iolinki-{name}-20261002-IODD1.1.xml'
                self.assertEqual(set(archive.namelist()), {filename, 'manifest.json'})
                self.assertEqual(archive.read(filename), (ROOT / 'examples' / example / 'device.xml').read_bytes())
                if os.environ.get('IODD_CHECKER'):
                    staged = Path(self.tmp.name) / filename
                    staged.write_bytes(archive.read(filename))
                    result = subprocess.run([os.environ['IODD_CHECKER'], str(staged)], capture_output=True, text=True, timeout=45)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    @unittest.skipUnless(os.environ.get('IODD_CHECKER'), 'set IODD_CHECKER to an operator-owned genuine Checker wrapper')
    def test_genuine_checker_accepts_examples_and_rejects_missing_ref(self):
        checker = os.environ['IODD_CHECKER']
        for example in ('reference_device', 'switching_sensor'):
            filename = Path(self.tmp.name) / f'iolinki-{example}-20261002-IODD1.1.xml'
            self.run_cli('generate', ROOT / f'examples/{example}/device.json', '-o', filename)
            positive = subprocess.run([checker, str(filename)], capture_output=True, text=True, timeout=45)
            self.assertEqual(positive.returncode, 0, positive.stdout + positive.stderr)
            filename.write_bytes(filename.read_bytes().replace(b'        <StdVariableRef id="V_ProductName" />\n', b''))
            subprocess.run(['node', str(ROOT / 'tools/iodd_stamp.mjs'), 'write', str(filename)], check=True, capture_output=True)
            negative = subprocess.run([checker, str(filename)], capture_output=True, text=True, timeout=45)
            self.assertNotEqual(negative.returncode, 0)
            self.assertIn('V_ProductName is missing', negative.stdout + negative.stderr)

    def test_shipped_xml_matches_configuration(self):
        for name in ('reference_device', 'switching_sensor', 'simple_device'):
            base = ROOT / 'examples' / name
            stem = 'simple_device' if name == 'simple_device' else 'device'
            self.run_cli('generate', base / (stem + '.json'), '-o', self.xml)
            self.assertEqual(self.xml.read_bytes(), (base / (stem + '.xml')).read_bytes(), name)

    def test_invalid_scalar_ranges_and_process_types_rejected(self):
        for mutation in ('range', 'default', 'type', 'width'):
            cfg = copy.deepcopy(self.config)
            if mutation == 'range':
                cfg['variables'][0].update(lowerValue=100, upperValue=10)
            elif mutation == 'default':
                cfg['variables'][0]['defaultValue'] = 70000
            elif mutation == 'type':
                cfg['processData'][0]['in']['fields'][0]['datatype'] = 'UnknownT'
            else:
                cfg['variables'][0]['bitLength'] = 1
            path = Path(self.tmp.name) / 'bad.json'
            path.write_text(json.dumps(cfg))
            self.run_cli('generate', path, '-o', self.xml, success=False)

    def test_reserved_ids_and_invalid_document_metadata_rejected(self):
        for key, value in [('releaseDate','tomorrow'), ('version','1'), ('vendorId',True), ('variables',None)]:
            cfg = copy.deepcopy(self.config)
            cfg[key] = value
            path = Path(self.tmp.name) / 'bad.json'
            path.write_text(json.dumps(cfg))
            self.run_cli('generate', path, '-o', self.xml, success=False)
        cfg = copy.deepcopy(self.config)
        cfg['variables'][0]['id'] = 'V_ProductID'
        path.write_text(json.dumps(cfg))
        self.run_cli('generate', path, '-o', self.xml, success=False)

    def test_sensor_c_wire_data_and_parameter_services_match_iodd(self):
        """Decode bytes from the actual application C implementation using IODD offsets."""
        library = Path(self.tmp.name) / 'sensor.so'
        subprocess.run(['cc', '-shared', '-fPIC', '-I'+str(ROOT/'include'),
                        str(ROOT/'examples/switching_sensor/switching_sensor.c'), '-o', str(library)], check=True)
        class Sensor(ctypes.Structure):
            _fields_ = [('threshold',ctypes.c_uint16), ('hysteresis',ctypes.c_uint16),
                        ('sample',ctypes.c_uint16), ('inverted',ctypes.c_bool),
                        ('active',ctypes.c_bool), ('valid',ctypes.c_bool), ('pd',ctypes.c_uint8*3)]
        lib = ctypes.CDLL(str(library))
        lib.switching_sensor_sample.argtypes = [ctypes.POINTER(Sensor), ctypes.c_uint16, ctypes.c_bool]
        lib.switching_sensor_sample.restype = ctypes.c_bool
        lib.switching_sensor_service.argtypes = [ctypes.c_void_p,ctypes.c_uint16,ctypes.c_uint8,ctypes.c_bool,
                                                ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.POINTER(ctypes.c_size_t)]
        lib.switching_sensor_service.restype = ctypes.c_uint8
        sensor = Sensor()
        lib.switching_sensor_init(ctypes.byref(sensor))
        self.generate()
        info = json.loads(self.run_cli('inspect', self.xml).stdout)
        for sample, valid in [(5100,True),(4900,True),(4800,True),(12345,False)]:
            output = lib.switching_sensor_sample(ctypes.byref(sensor),sample,valid)
            wire = int.from_bytes(bytes(sensor.pd),'big')
            fields = info['processData'][0]['in']['fields']
            values = [(wire >> f['bitOffset']) & ((1<<f['bitLength'])-1) for f in fields]
            self.assertEqual(values,[sample,int(valid),int(output)])
        for variable in info['variables']:
            index = variable['index']
            if variable['accessRights'] != 'wo':
                data = (ctypes.c_uint8*8)()
                length = ctypes.c_size_t(8)
                error = lib.switching_sensor_service(ctypes.byref(sensor),index,0,False,None,0,data,ctypes.byref(length))
                self.assertEqual(error,0)
                expected_bits = next(v['bitLength'] for v in self.config['variables'] if v['index']==index)
                self.assertEqual(length.value,expected_bits//8)
                self.assertEqual(int.from_bytes(bytes(data[:length.value]),'big'),int(variable['defaultValue']))
        lib.switching_sensor_sample(ctypes.byref(sensor),5432,True)
        teach = (ctypes.c_uint8*1)(1)
        length = ctypes.c_size_t(0)
        self.assertEqual(lib.switching_sensor_service(ctypes.byref(sensor),259,0,True,teach,1,None,ctypes.byref(length)),0)
        self.assertEqual(sensor.threshold,5432)
        self.assertNotEqual(lib.switching_sensor_service(ctypes.byref(sensor),259,1,True,teach,1,None,ctypes.byref(length)),0)

    @unittest.skipUnless(os.environ.get('IODD_SCHEMA'), 'set IODD_SCHEMA to an externally obtained official IODD1.1.xsd')
    def test_examples_validate_against_official_schema(self):
        for name in ('reference_device', 'switching_sensor', 'simple_device'):
            self.run_cli('generate', ROOT / f'examples/{name}/' / ('simple_device.json' if name == 'simple_device' else 'device.json'), '-o', self.xml)
            self.run_cli('validate', self.xml, '--schema', os.environ['IODD_SCHEMA'])

if __name__ == '__main__':
    unittest.main()
