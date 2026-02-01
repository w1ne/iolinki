# Python Virtual IO-Link Master

A software-based IO-Link Master simulator for automated testing of IO-Link Device implementations without requiring physical hardware.

## Features

- **Virtual UART**: Uses pseudo-terminals (pty) for Master-Device communication
- **M-Sequence Generation**: Supports Type 0 (On-request data)
- **CRC Calculation**: Matches iolinki implementation (polynomial 0x1D, seed 0x15)
- **Master State Machine**: Implements Startup, Preoperate, Operate states
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

### 2. Run Unit Tests

```bash
pytest tests/ -v
```

## Usage

```python
from virtual_master.master import VirtualMaster

# Create Virtual Master
with VirtualMaster() as master:
    print(f"Connect Device to: {master.get_device_tty()}")
    
    # Run startup sequence
    if master.run_startup_sequence():
        # Communication established
        response = master.run_cycle()
        
        # Read ISDU
        data = master.read_isdu(index=0x10)  # Vendor Name
```

## Architecture

```
┌─────────────────────┐
│  Virtual Master     │
│  - State Machine    │
│  - M-Sequence Gen   │
│  - CRC Validation   │
└──────────┬──────────┘
           │ pty (virtual UART)
┌──────────▼──────────┐
│  iolinki Device     │
└─────────────────────┘
```

## Roadmap

- [x] Virtual UART (pty)
- [x] CRC calculation
- [x] M-sequence Type 0
- [x] Master state machine
- [x] Basic startup sequence
- [ ] M-sequence Type 1_x (PD + 8-bit OD)
- [ ] M-sequence Type 2_x (PD + ISDU)
- [ ] IODD parser
- [ ] Full ISDU services
- [ ] Event handling
- [ ] Data Storage commands
- [ ] Timing validation
- [ ] Docker integration
- [ ] CI/CD integration

## Testing

```bash
# Run all tests
pytest tests/ -v

# Run specific test
pytest tests/test_crc.py -v

# Run with coverage
pytest tests/ --cov=virtual_master
```

## License

Same as iolinki project (see root LICENSE file)
