# Python Virtual IO-Link Master

A software-based IO-Link Master simulator for automated testing of IO-Link Device implementations without requiring physical hardware.

## Features

- **Virtual UART**: Uses pseudo-terminals (pty) for Master-Device communication
- **M-Sequence Support**: Types 0, 1_1, 1_2, 2_1, 2_2 (and 1_V/2_V with fixed PD length)
- **Variable OD Length**: Automatic handling of 1-byte and 2-byte On-request Data
- **Variable PD Length**: Fixed PD length configuration for Type 1_V and 2_V (runtime negotiation pending)
- **CRC Calculation**: Matches iolinki implementation (polynomial 0x1D, seed 0x15)
- **Master State Machine**: Implements Startup, Preoperate, Operate states
- **ISDU Operations**: Read/Write with V1.1.5 segmentation support
- **Integration Testing**: Automated test suites for all M-sequence types
- **Automated Testing**: Run protocol tests in CI/CD

## Installation

```bash
cd tools/virtual_master
pip3 install -r requirements.txt
```

## Quick Start

### 1. Run Basic Example

```bash
# Terminal 1: Start Virtual Master
python3 examples/basic_test.py

# Terminal 2: Start Device (use TTY path from Terminal 1)
./build/examples/host_demo/host_demo /dev/pts/X
```

### 2. Run M-Sequence Type Tests

```bash
# Test all M-sequence types
python3 test_m_sequence_types.py
```

### 3. Run Unit Tests

```bash
pytest tests/ -v
```

## Usage

### Basic Example (Type 0)

```python
from virtual_master import VirtualMaster

# Create Virtual Master (Type 0 - ISDU only)
with VirtualMaster() as master:
    print(f"Connect Device to: {master.get_device_tty()}")

    # Run startup sequence
    if master.run_startup_sequence():
        # Read ISDU
        data = master.read_isdu(index=0x10)  # Vendor Name
```

### Type 1_2 Example (PD + ISDU, 1-byte OD)

```python
from virtual_master import VirtualMaster, MSequenceType

# Create Master for Type 1_2
master = VirtualMaster(
    m_seq_type=MSequenceType.TYPE_1_2,
    pd_in_len=2,
    pd_out_len=2
)

# Startup and transition to OPERATE
master.run_startup_sequence()
master.go_to_operate()

# Run cycle with PD exchange
response = master.run_cycle(
    pd_out=bytes([0x11, 0x22]),
    od_req=0x00
)
print(f"PD_In: {response.payload.hex()}")

# Read ISDU (Vendor ID)
vendor_id = master.read_isdu(0x000A)
```

### Type 2_2 Example (PD + ISDU, 2-byte OD)

```python
from virtual_master import VirtualMaster, MSequenceType

# Create Master for Type 2_2
master = VirtualMaster(
    m_seq_type=MSequenceType.TYPE_2_2,
    pd_in_len=1,
    pd_out_len=1
)

# OD length is automatically set to 2 bytes
print(f"OD Length: {master.od_len} bytes")

# Startup and operate
master.run_startup_sequence()
master.go_to_operate()

# Run cycle - response includes 2-byte OD
response = master.run_cycle(pd_out=bytes([0x33]))
print(f"OD: 0x{response.od:02X}, OD2: 0x{response.od2:02X}")
```

## Supported M-Sequence Types

| Type | Code | Description | OD Length | ISDU Support |
|------|------|-------------|-----------|--------------|
| Type 0 | 0 | ISDU only | 1 byte | ✅ |
| Type 1_1 | 11 | PD only | 1 byte | ❌ |
| Type 1_2 | 12 | PD + ISDU | 1 byte | ✅ |
| Type 1_V | 13 | Variable PD (fixed length config) | 1 byte | ❌ |
| Type 2_1 | 21 | PD only | 2 bytes | ❌ |
| Type 2_2 | 22 | PD + ISDU | 2 bytes | ✅ |
| Type 2_V | 23 | Variable PD (fixed length config) | 2 bytes | ❌ |

## Architecture

```
┌─────────────────────┐
│  Virtual Master     │
│  - State Machine    │
│  - M-Sequence Gen   │
│  - CRC Validation   │
│  - Variable OD      │
└──────────┬──────────┘
           │ pty (virtual UART)
┌──────────▼──────────┐
│  iolinki Device     │
└─────────────────────┘
```

## API Reference

### VirtualMaster

```python
VirtualMaster(
    uart: Optional[VirtualUART] = None,
    m_seq_type: int = 0,
    pd_in_len: int = 0,
    pd_out_len: int = 0
)
```

**Parameters:**
- `m_seq_type`: M-sequence type (0, 11, 12, 21, 22, etc.)
- `pd_in_len`: Process Data Input length in bytes
- `pd_out_len`: Process Data Output length in bytes

**Methods:**
- `run_startup_sequence()`: Execute startup and establish communication
- `go_to_operate()`: Transition device to OPERATE state
- `run_cycle(pd_out, od_req)`: Execute one communication cycle
- `read_isdu(index, subindex)`: Read ISDU parameter
- `write_isdu(index, subindex, data)`: Write ISDU parameter

## Testing

```bash
# Run all tests
pytest tests/ -v

# Run specific test
pytest tests/test_crc.py -v

# Run with coverage
pytest tests/ --cov=virtual_master

# Test M-sequence types
python3 test_m_sequence_types.py
```

## Roadmap

- [x] Virtual UART (pty)
- [x] CRC calculation
- [x] M-sequence Type 0
- [x] Master state machine
- [x] Basic startup sequence
- [x] M-sequence Type 1_1, 1_2 (PD + 1-byte OD)
- [x] M-sequence Type 2_1, 2_2 (PD + 2-byte OD)
- [x] Variable OD length support
- [x] ISDU V1.1.5 segmentation
- [~] M-sequence Type 1_V, 2_V (fixed-length config only; runtime negotiation pending)
- [ ] IODD parser
- [ ] Event handling
- [ ] Data Storage commands
- [ ] Timing validation
- [ ] Docker integration

## License

Same as iolinki project (see root LICENSE file)
