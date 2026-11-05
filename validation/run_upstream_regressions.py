#!/usr/bin/env python3
"""Run the dependency-free upstream regressions without substituting CMocka."""
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent.parent
os.chdir(root)
build = root / 'validation' / 'build'
build.mkdir(exist_ok=True)
core = [f'src/{name}.c' for name in (
    'crc', 'frame', 'dll', 'isdu', 'events', 'params', 'data_storage',
    'device_info', 'device', 'platform', 'phy_l6362a', 'phy_tiol112')]
clock = ['src/platform/baremetal/time_utils.c']
cases = [
    ('test_cycle_phase', ['tests/test_cycle_phase.c'] + core, []),
    ('test_deadline_rollover', ['tests/test_deadline_rollover.c'] + core, []),
    ('test_events_dll', ['tests/test_events_dll.c'] + core, []),
    ('test_params_persistence', ['tests/test_params_persistence.c'] + core + clock, []),
    ('test_vendor_isdu', ['tests/test_vendor_isdu.c'] + core + clock, []),
    ('test_phy_l6362a', ['tests/test_phy_l6362a.c'] + core + clock, []),
    ('test_linux_nvm', ['tests/test_linux_nvm.c', 'src/platform/linux/nvm_mock.c'], []),
    ('test_switching_sensor', ['tests/test_switching_sensor.c',
        'examples/switching_sensor/switching_sensor.c'], ['-Iexamples/switching_sensor']),
    ('test_rx_queue', ['examples/stm32g0_tiol112/tests/test_rx_queue.c',
        'examples/stm32g0_tiol112/rx_queue.c'], ['-Iexamples/stm32g0_tiol112']),
    ('test_esp32_tx_guard', ['examples/esp32_l6362a/test_tx_guard.c',
        'examples/esp32_l6362a/main/tx_guard.c', 'examples/esp32_l6362a/main/rx_guard.c'],
        ['-Iexamples/esp32_l6362a/main']),
]
results = []
for name, sources, flags in cases:
    command = [os.environ.get('CC', 'cc'), '-std=c99', '-O2', '-Wall', '-Wextra',
        '-Werror', '-Iinclude'] + flags + sources + ['-o', str(build / name)]
    compiled = subprocess.run(command, text=True, capture_output=True)
    if compiled.returncode:
        results.append({'name': name, 'status': 'BUILD_FAILED', 'detail': compiled.stderr})
        continue
    result = subprocess.run([str(build / name)], cwd=build, text=True, capture_output=True)
    results.append({'name': name, 'status': 'PASS' if result.returncode == 0 else 'FAIL',
                    'returncode': result.returncode, 'detail': result.stdout + result.stderr})
print(json.dumps({'scope': 'dependency-free project tests; CMocka suite not run',
                  'results': results}, indent=2))
raise SystemExit(any(item['status'] != 'PASS' for item in results))
