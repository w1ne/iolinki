# Virtual Master - Supported Services

## Current Implementation (Phase 1)

### ✅ Implemented Services

#### Physical Layer
- **Virtual UART**: Pseudo-terminal (pty) based communication
- **Baudrate**: Fixed (software-based, no actual baudrate)
- **Frame Transmission**: Byte-by-byte send/receive

#### Data Link Layer
- **M-Sequence Type 0**: On-request data only
  - Wake-up pulse (MC: 0x95)
  - Idle frame (MC: 0x00)
  - ISDU Read request (MC: 0xA0) - basic
  - Event request (MC: 0xA2)
- **CRC6 Calculation**: Polynomial 0x1D, seed 0x15 (matches iolinki)
- **Checksum Verification**: Type 0 frames

#### Master State Machine
- **STARTUP**: Initial state
- **PREOPERATE**: After wake-up sequence
- **Startup Sequence**: Wake-up → Idle → Communication established

#### Services
- `send_wakeup()`: Send wake-up pulse
- `send_idle()`: Send idle frame, receive Device response
- `read_isdu()`: Request ISDU read (Type 0 only)
- `request_event()`: Request event from Device
- `run_startup_sequence()`: Automated startup
- `run_cycle()`: Single communication cycle

### ❌ Not Yet Implemented

#### M-Sequences
- M-sequence Type 1_1 (PD only, 8-bit OD)
- M-sequence Type 1_2 (PD + ISDU, 8-bit OD)
- M-sequence Type 1_V (PD only, variable OD)
- M-sequence Type 2_1 (PD only, 16/32-bit OD)
- M-sequence Type 2_2 (PD + ISDU, 16/32-bit OD)
- M-sequence Type 2_V (PD only, variable OD + ISDU)

#### ISDU Services
- Full ISDU Read with segmentation
- ISDU Write service
- ISDU flow control (Busy/Retry)
- 16-bit index support
- Subindex addressing

#### Process Data
- PD Input (Device → Master)
- PD Output (Master → Device)
- PD consistency (toggle bit)
- PD validity flags

#### Events
- Event queue management
- Event acknowledgment
- Event qualifier parsing

#### Data Storage
- DS Upload/Download commands
- DS checksum validation
- Parameter server

#### Advanced Features
- IODD parsing
- Device capability detection
- Timing validation (t_A, t_ren, t_cycle)
- Baudrate negotiation
- SIO mode support

## API Reference

### VirtualMaster Class

```python
from virtual_master.master import VirtualMaster

# Create Virtual Master
master = VirtualMaster()

# Get Device TTY path
tty = master.get_device_tty()  # e.g., '/dev/pts/5'

# Send wake-up
master.send_wakeup()

# Send idle and get response
response = master.send_idle()
if response.valid:
    print(f"Device status: 0x{response.status:02X}")
    if response.has_event():
        print("Device has pending event")

# Read ISDU (basic Type 0)
data = master.read_isdu(index=0x10)  # Vendor Name

# Request event
event_code = master.request_event()

# Run startup sequence
if master.run_startup_sequence():
    print("Startup successful")

# Run communication cycle
response = master.run_cycle()

# Close
master.close()
```

### MSequenceGenerator Class

```python
from virtual_master.protocol import MSequenceGenerator

gen = MSequenceGenerator()

# Generate frames
wakeup = gen.generate_wakeup()      # [0x95, 0x00, CK]
idle = gen.generate_idle()          # [0x00, 0x00, CK]
isdu_read = gen.generate_isdu_read(0x10)  # ISDU read
event_req = gen.generate_event_request()  # Event request
```

### DeviceResponse Class

```python
from virtual_master.protocol import DeviceResponse

response = DeviceResponse(data)

# Check validity
if response.valid:
    # Access fields
    status = response.status      # Status byte
    payload = response.payload    # Payload bytes
    checksum = response.checksum  # CK byte
    
    # Check for event
    if response.has_event():
        print("Event pending")
```

### CRC Functions

```python
from virtual_master.crc import calculate_crc6, calculate_checksum_type0

# Calculate CRC for arbitrary data
crc = calculate_crc6(b'\xAB\xCD\xEF')

# Calculate Type 0 checksum
ck = calculate_checksum_type0(mc=0x95, ckt=0x00)
```

## Test Coverage

### Unit Tests
- ✅ CRC calculation correctness
- ✅ CRC consistency
- ✅ 6-bit range validation
- ✅ M-sequence generation
- ✅ Virtual UART creation
- ✅ Frame transmission

### Integration Tests
- ⏳ Startup sequence with real Device
- ⏳ ISDU Read/Write operations
- ⏳ Event handling
- ⏳ Process Data exchange
- ⏳ Timing validation

## Roadmap

### Phase 2: Protocol Completion (Next)
- M-sequence Type 1_x implementation
- Full ISDU Read/Write services
- ISDU segmentation
- Process Data exchange

### Phase 3: Advanced Features
- M-sequence Type 2_x
- Event handling
- Data Storage commands
- Error injection

### Phase 4: IODD Integration
- IODD XML parser
- Device capability detection
- Automated test generation

### Phase 5: Automation
- Docker integration
- CI/CD pipeline
- Test report generation
- Performance benchmarks
