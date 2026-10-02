#!/usr/bin/env python3
"""Stage canonical example IODD names without changing the general CLI pack API."""
from pathlib import Path
import sys
import tempfile
import xml.etree.ElementTree as ET

from iodd_cli import pack

ROOT = Path(__file__).resolve().parents[1]


def package_examples(output, version):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='iolinki-release-iodds-') as temporary:
        for example, name in [('reference_device', 'counter'), ('switching_sensor', 'switching-sensor')]:
            data = (ROOT / 'examples' / example / 'device.xml').read_bytes()
            date = ET.fromstring(data).find('{http://www.io-link.com/IODD/2010/10}DocumentInfo').get('releaseDate').replace('-', '')
            staged = Path(temporary) / f'iolinki-{name}-{date}-IODD1.1.xml'
            staged.write_bytes(data)
            pack(staged, output / f'iolinki-{version}-{name}-IODD.zip')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('Usage: package_release_iodds.py OUTPUT_DIRECTORY VERSION')
    package_examples(sys.argv[1], sys.argv[2])
