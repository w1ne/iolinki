# Virtual Master - Supported Services

## Current Implementation

### ✅ Implemented Services

#### Physical Layer
- **Virtual UART**: Pseudo-terminal (pty) based communication
- **Baudrate**: Fixed (software-based, no actual baudrate)
- **Frame Transmission**: Byte-by-byte send/receive

#### Data Link Layer
- **M-Sequence Types**: 0, 1_1, 1_2, 2_1, 2_2
- **Variable PD Lengths**: Type 1_V / 2_V via `set_pd_length()` and PD len config (runtime negotiation pending)
- **Variable OD Length**: 1-byte (Type 1) and 2-byte (Type 2)
- **Wake-up / Idle**: Type 0 wake-up and idle sequences
- **Event Request**: Master-side event request (MC: 0xA2) - requires device support
- **CRC6 Calculation**: Polynomial 0x1D, seed 0x15 (matches iolinki)
- **Checksum Verification**: Type 1/2 response validation

#### Master State Machine
- **STARTUP**: Initial state
- **PREOPERATE**: After wake-up sequence
- **Startup Sequence**: Wake-up → Idle → Communication established

#### Services
- `send_wakeup()`: Send wake-up pulse
- `send_idle()`: Send idle frame, receive Device response
- `read_isdu()`: ISDU Read (V1.1.5 segmentation, Type 0/1/2)
- `write_isdu()`: ISDU Write (V1.1.5 segmentation, Type 0/1/2)
- `request_event()`: Request event from Device
- `run_startup_sequence()`: Automated startup
- `run_cycle()`: Single communication cycle (PD + OD)

### ❌ Not Yet Implemented

#### ISDU Services
- ISDU flow control (Busy/Retry)
- 16-bit index / subindex error injection helpers

#### Process Data
- PD consistency (toggle bit)
- PD validity flags

#### Events
- Event acknowledgment (CKT flow control)
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
wakeup = gen.generate_wakeup()      # [MC, CK]
idle = gen.generate_idle()          # [MC, CK]
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
