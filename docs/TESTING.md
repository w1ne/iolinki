# Testing Guide

## Overview

iolinki provides multiple testing approaches to ensure protocol compliance and robustness across different environments.

## Test Types

### 1. Unit Tests (CMocka)

Located in `tests/`, these test individual components in isolation using mocks.

**Prerequisites**:
```bash
sudo apt-get install libcmocka-dev
```

**Running**:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
cd build && ctest --output-on-failure
```

**Available Tests**:
- `test_crc` - CRC calculation verification
- `test_dll` - Data Link Layer state machine
- `test_isdu` - ISDU services
- `test_events` - Event system
- `test_ds` - Data Storage
- `test_application` - Application layer
- `test_integration_full` - Full stack lifecycle

### 2. Docker Tests (Recommended)

Run all tests in isolated Docker environment with all dependencies pre-installed.

**Running**:
```bash
./tools/run_tests_docker.sh
```

**What it tests**:
- All CMocka unit tests
- Virtual Master CRC validation
- Virtual Master unit tests
- Integration tests (if Device available)

### 3. Virtual Master Integration Tests

Python-based Virtual IO-Link Master for protocol testing without hardware.

**Manual Test** (requires 2 terminals):
```bash
# Terminal 1: Start Virtual Master
cd tools/virtual_master
python3 test_integration.py

# Terminal 2: Start Device (use TTY from Terminal 1)
./build/examples/host_demo/host_demo /dev/pts/X
```

**Automated Test** (Docker):
```bash
cd tools/virtual_master
python3 test_automated.py
```

**What it tests**:
- Startup sequence
- Communication cycles
- Event handling
- ISDU operations
- CRC validation

### 4. Platform-Specific Tests

#### Zephyr RTOS
```bash
./tools/build_zephyr_docker.sh
# Runs in QEMU or deploy to hardware
```

#### Bare Metal
```bash
cmake -B build_bare -DIOLINK_PLATFORM=BAREMETAL
cmake --build build_bare
./build_bare/examples/bare_metal_app/bare_metal_app
```

## Test Coverage

### Current Coverage

| Component | Unit Tests | Integration | Platform Tests |
|-----------|------------|-------------|----------------|
| CRC | ✅ | ✅ | ✅ |
| DLL State Machine | ✅ | ✅ | ✅ |
| ISDU | ✅ | ✅ | ⚠️ |
| Events | ✅ | ✅ | ❌ |
| Data Storage | ✅ | ✅ | ❌ |
| Process Data | ❌ | ❌ | ❌ |
| M-sequence Type 1_x | ❌ | ❌ | ❌ |

### Coverage Goals

- **Phase 1**: 80% unit test coverage for core components ✅
- **Phase 2**: 90% integration test coverage
- **Phase 3**: 100% V1.1.5 compliance test coverage

## Writing Tests

### CMocka Unit Test Example

```c
#include <stdarg.h>
#include <stddef.h>
#include <setjmp.h>
#include <cmocka.h>

#include "iolinki/dll.h"
#include "test_helpers.h"

static void test_dll_startup(void **state) {
    (void)state;
    
    // Setup
    iolink_dll_ctx_t ctx;
    iolink_dll_init(&ctx, &g_phy_mock);
    
    // Execute
    iolink_dll_process(&ctx);
    
    // Verify
    assert_int_equal(ctx.state, DLL_STATE_STARTUP);
}

int main(void) {
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_dll_startup),
    };
    return cmocka_run_group_tests(tests, NULL, NULL);
}
```

### Virtual Master Test Example

```python
from virtual_master.master import VirtualMaster

def test_startup():
    master = VirtualMaster()
    assert master.run_startup_sequence()
    master.close()
```

## Continuous Integration

Tests run automatically on every commit via GitHub Actions:

```yaml
- name: Build and Test
  run: |
    cmake -B build -DCMAKE_BUILD_TYPE=Debug
    cmake --build build
    cd build && ctest --output-on-failure
```

## Debugging Failed Tests

### CMocka Tests

```bash
# Run specific test with verbose output
cd build
./tests/test_dll --verbose

# Run with GDB
gdb ./tests/test_dll
```

### Virtual Master Tests

```bash
# Enable debug logging
cd tools/virtual_master
python3 -m pdb test_master.py
```

## Test Artifacts

Test results are stored in:
- `build/Testing/` - CMocka test results
- `build/test-results.xml` - JUnit XML format (CI)

## Known Limitations

1. **No hardware tests**: All tests use mocks or virtual UART
2. **Timing tests**: Not validated on real hardware
3. **M-sequence Type 1_x/2_x**: Not yet implemented
4. **Process Data**: No test coverage yet

## Next Steps

- Add Process Data tests
- Implement M-sequence Type 1_x tests
- Add timing validation tests
- Create hardware-in-the-loop tests
