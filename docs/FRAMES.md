# IO-Link Frame & M-Sequence Support

This document describes the IO-Link M-Sequences and frame structures supported by the `iolinki` stack, both in the core library and the simulation tools.

## 1. Supported M-Sequences

The stack supports multiple M-Sequence types defined in the IO-Link V1.1.5 specification. The M-Sequence type determines how Process Data (PD) and On-request Data (OD) are interleaved.

| Type | Description | PD Support | OD Support | ISDU Support | Status |
| :--- | :--- | :---: | :---: | :---: | :--- |
| **Type 0** | On-request data only | No | 1 byte | Yes | Supported |
| **Type 1_1** | Fixed PD only (no ISDU) | Yes | 1 byte | No | Supported |
| **Type 1_2** | Fixed PD + Interleaved ISDU | Yes | 1 byte | Yes | Supported |
| **Type 2_1** | Fixed PD + 2-byte OD | Yes | 2 bytes | No | Supported |
| **Type 2_2** | Fixed PD + 2-byte OD + ISDU | Yes | 2 bytes | Yes | Supported |

### 1.1 Type 0 (On-request Data Only)
Used primarily during the `PREOPERATE` state for identification and parameterization.
- **Master -> Device**: `[MC] [CK]` (2 bytes)
- **Device -> Master**: `[OD] [CK]` (2 bytes)
- *Note: `MC` (Master Command) and `CK` (Checksum) are 8-bit fields.*

### 1.2 Type 1_x (PD + Interleaved ISDU)
Enables concurrent exchange of Process Data and ISDU bytes.
- **Master -> Device**: `[MC] [CKT] [PD_Out...] [OD] [CK]`
- **Device -> Master**: `[Status] [PD_In...] [OD] [CK]`
- **Status Byte**:
  - Bit 7: EventFlag (1=Pending Event)
  - Bit 5: PDValid (1=Valid)
  - Bits 6,4-0: Reserved

## 2. Simulation Frames (Virtual Master)

The Python Virtual Master (`tools/virtual_master`) uses a dedicated framing mechanism to simulate the physical layer over a Virtual UART (PTY).

### 2.1 PTY Framing
To ensure synchronization and robust parsing in software simulation, the Master-Device communication follows strict timing and boundary rules:

- **Startup Trigger**: Simulated by a dummy byte (`0x00`) to wake the Device stack.
- **Cycle Time**: Defaults to 10ms, configurable via `VirtualMaster`.
- **Sync Loss Recovery**: The Device stack automatically resets to `STARTUP` if no bytes are received for >1000ms.

## 3. Configuration & Capabilities

### 3.1 Stack Limits
- **Max Process Data**: 32 bytes (Input and Output).
- **ISDU Buffer**: 256 bytes.
- **Event Queue**: 8 entries.

### 3.2 Dynamic Configuration
The stack is configured during `iolink_init()` via the `iolink_config_t` structure:

```c
typedef struct {
    iolink_m_seq_type_t m_seq_type; /**< Type 0, 1_x, or 2_x */
    uint8_t min_cycle_time;         /**< Encoded per spec */
    uint8_t pd_in_len;              /**< 0-32 bytes */
    uint8_t pd_out_len;             /**< 0-32 bytes */
} iolink_config_t;
```
