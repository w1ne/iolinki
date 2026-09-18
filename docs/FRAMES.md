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

### 1.1 Message checksum (A.1.6)

The checksum is an XOR of every message octet seeded with `0x52`, compressed
from 8 to 6 bits by the equations in (A.1). It lives in the **CKT** octet of a
master message and in the **CKS** octet of a device reply; those octets' bits
0-5 are zero before the checksum is computed. There is no trailing checksum
octet.

### 1.2 Type 0 (On-request Data Only)
Used during STARTUP and PREOPERATE for identification and parameterization, and
in OPERATE for On-request Data access.
- **Master -> Device (read)**: `[MC] [CKT]` (2 bytes)
- **Master -> Device (write)**: `[MC] [CKT] [OD]`
- **Device -> Master (read reply)**: `[OD] [CKS]`
- **Device -> Master (write reply)**: `[CKS]` (1 byte)

A Type-0 request on the page, diagnosis or ISDU communication channel carries
one OD octet on a write. FlowCTRL for ISDU lives in `MC & 0x1F`.

### 1.3 Type 1_x / 2_x (PD + OD)

- **Master -> Device**: `[MC] [CKT] [PD_Out...] [OD...]`
- **Device -> Master**: `[PD_In...] [OD...] [CKS]`

The CKT carries the M-sequence type in bits 6-7 (`01` for Type 1_x, `10` for
Type 2_x) plus the 6-bit checksum. There is **no leading status octet and no
toggle bit**: the Event flag is CKS bit 7 and the PD-valid flag is CKS bit 6
(1 = invalid), per A.1.5.

## 2. Simulation Frames (Virtual Master)

The Python Virtual Master (`tools/virtual_master`) uses a dedicated framing mechanism to simulate the physical layer over a Virtual UART (PTY).

### 2.1 PTY Framing
To ensure synchronization and robust parsing in software simulation, the Master-Device communication follows strict timing and boundary rules:

- **Startup Trigger**: Simulated by a wake-up marker byte (`0x55`) that the virtual PHY's `detect_wakeup` scans for; the first M-sequence must follow within T_DSIO (300 ms).
- **Cycle Time**: Defaults to 10ms, configurable via `VirtualMaster`.
- **Sync Loss Recovery**: The Device stack automatically resets to `STARTUP` if no bytes are received for >1000ms.

## 3. Configuration & Capabilities

### 3.1 Stack Limits
- **Max Process Data**: 32 bytes (Input and Output).
- **ISDU Buffer**: 256 bytes.
- **Event Queue**: 4 entries (default `IOLINK_EVENT_QUEUE_SIZE`).

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
