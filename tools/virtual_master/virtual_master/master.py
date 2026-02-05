"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.
"""

"""
IO-Link Master state machine.
"""

from enum import Enum
import time
from typing import Optional
from .uart import VirtualUART
from .protocol import MSequenceGenerator, DeviceResponse, ISDUControlByte


class MasterState(Enum):
    """Master state machine states."""
    STARTUP = "STARTUP"
    ESTAB_COM = "ESTAB_COM"
    PREOPERATE = "PREOPERATE"
    OPERATE = "OPERATE"


class VirtualMaster:
    """
    Virtual IO-Link Master implementation.
    
    Simulates a complete IO-Link Master for testing Device implementations.
    """
    
    def __init__(self, uart: Optional[VirtualUART] = None, m_seq_type: int = 0, pd_in_len: int = 0, pd_out_len: int = 0):
        """
        Initialize Virtual Master.
        
        Args:
            uart: Virtual UART instance (creates new one if None)
            m_seq_type: M-sequence type (0, 11, 12, 21, 22, etc.)
            pd_in_len: Process Data Input length (bytes)
            pd_out_len: Process Data Output length (bytes)
        """
        self.uart = uart or VirtualUART()
        self.m_seq_type = m_seq_type
        self.pd_in_len = pd_in_len
        self.pd_out_len = pd_out_len
        
        # Calculate OD length based on M-sequence type
        from .protocol import MSequenceType
        self.od_len = MSequenceType.get_od_len(m_seq_type)
        
        # Initialize generator with correct OD length
        self.generator = MSequenceGenerator(od_len=self.od_len)
        self.state = MasterState.STARTUP
        self.cycle_time_ms = 10  # Default cycle time
        self.phy_mode = "SDCI"  # Default PHY mode
        self.baudrate = "COM2"  # Default baudrate (38.4 kbit/s)
        
    def get_device_tty(self) -> str:
        """Get the TTY path for connecting the Device."""
        return self.uart.get_device_tty()
    
    def set_pd_length(self, pd_in_len: int, pd_out_len: int) -> None:
        """
        Change PD lengths for variable types (1_V, 2_V).
        
        Args:
            pd_in_len: New PD Input length (2-32 bytes)
            pd_out_len: New PD Output length (2-32 bytes)
            
        Raises:
            ValueError: If not a variable type or invalid length
        """
        from .protocol import MSequenceType
        
        if self.m_seq_type not in (MSequenceType.TYPE_1_V, MSequenceType.TYPE_2_V):
            raise ValueError("PD length change only supported for Type 1_V and 2_V")
        
        if not (2 <= pd_in_len <= 32) or not (2 <= pd_out_len <= 32):
            raise ValueError("PD length must be 2-32 bytes")
        
        self.pd_in_len = pd_in_len
        self.pd_out_len = pd_out_len
        print(f"[Master] PD length changed: PD_In={pd_in_len}, PD_Out={pd_out_len}")
    
    def set_sio_mode(self) -> bool:
        """
        Switch to SIO mode (single-wire communication).
        
        Returns:
            True if successful, False if not in OPERATE state
        """
        if self.state != MasterState.OPERATE:
            return False
        
        self.phy_mode = "SIO"
        print("[Master] Switched to SIO mode")
        return True
    
    def set_sdci_mode(self) -> bool:
        """
        Switch to SDCI mode (separate TX/RX).
        
        Returns:
            True if successful
        """
        self.phy_mode = "SDCI"
        print("[Master] Switched to SDCI mode")
        return True
    
    def set_baudrate(self, baudrate: str) -> bool:
        """
        Set communication baudrate (COM1, COM2, COM3).
        
        Args:
            baudrate: Baudrate string ("COM1", "COM2", "COM3")
            
        Returns:
            True if successful
        """
        if baudrate not in ("COM1", "COM2", "COM3"):
            print(f"[Master] Invalid baudrate: {baudrate}")
            return False
            
        self.baudrate = baudrate
        # In Virtual Master, this is mostly for metadata/timing if implemented
        print(f"[Master] Baudrate set to {baudrate}")
        return True
    
    def send_wakeup(self) -> None:
        """Send wake-up pulse (simulated by dummy byte) to Device."""
        self.uart.send_bytes(bytes([0x55]))
        print(f"[Master] Sent WAKEUP (dummy byte)")
    
    def send_idle(self) -> DeviceResponse:
        """
        Send idle frame and receive Device response.
        
        Returns:
            Device response
        """
        frame = self.generator.generate_idle()
        self.uart.send_bytes(frame)
        
        # Wait for response (2 bytes minimum: status + CK)
        response_data = self.uart.recv_bytes(2, timeout_ms=500)
        
        if response_data:
            response = DeviceResponse(response_data)
            print(f"[Master] Sent IDLE, Received: {response}")
            return response
        else:
            print(f"[Master] Sent IDLE, No response (timeout)")
            return DeviceResponse(b'')
    
    def read_isdu(self, index: int, subindex: int = 0) -> Optional[bytes]:
        """
        Read ISDU parameter from Device (supports V1.1.5 Segmented).
        """
        print(f"[Master] ISDU Read request: Index=0x{index:04X}, Subindex=0x{subindex:02X}")
        
        # Use V1.1.5 logic
        bytes_to_send = self.generator.generate_isdu_read_v11(index, subindex)
        
        def send_and_recv(byte_to_send: int):
            if self.m_seq_type == 0:
                frame = self.generator.generate_type0(byte_to_send)
                self.uart.send_bytes(frame)
                resp = self.uart.recv_bytes(2, timeout_ms=100)
                return DeviceResponse(resp) if resp else None
            else:
                return self.run_cycle(od_req=byte_to_send)

        # 1. Send Request
        for val in bytes_to_send:
            resp = send_and_recv(val)
            if not resp or not resp.valid:
                print("[Master] ISDU Req failed")
                return None

        # 2. Receive Response
        data_bytes = bytearray()
        
        # Check if the first response byte (Control Byte) arrived in the last request cycle
        ctrl = 0
        if resp:
            if hasattr(resp, 'od') and resp.od != 0:
                ctrl = resp.od
            elif resp.payload and resp.payload[0] != 0:
                ctrl = resp.payload[0]
        
        if ctrl != 0:
            print(f"[Master] Captured initial Control Byte from request cycle: 0x{ctrl:02X}")
        
        if ctrl == 0:
            # Read Response Control Byte first
            resp = send_and_recv(0x00) # IDLE to clock out response
            if not resp or not resp.valid: return None
            ctrl = resp.od if hasattr(resp, 'od') else resp.payload[0]
            print(f"[Master] Read Control Byte from first IDLE cycle: 0x{ctrl:02X}")
        # V1.1.5 response framing: [Ctrl] [Data] [Ctrl] [Data] ...
        is_start = bool(ctrl & 0x80)
        is_last = bool(ctrl & 0x40)
        
        if not is_start:
             print(f"[Master] ISDU Response error: Expected Start bit, got 0x{ctrl:02X}")
        
        while True:
            # Data Byte
            resp = send_and_recv(0x00)
            if not resp or not resp.valid: break
            
            val = resp.od if hasattr(resp, 'od') else resp.payload[0]
            data_bytes.append(val)
            
            if is_last:
                break
                
            # Next Control Byte
            resp = send_and_recv(0x00)
            if not resp or not resp.valid: break
            ctrl = resp.od if hasattr(resp, 'od') else resp.payload[0]
            is_last = bool(ctrl & 0x40)

        print(f"[Master] ISDU Read collected {len(data_bytes)} bytes")
        return bytes(data_bytes)
    
    def request_event(self) -> Optional[int]:
        """
        Request event from Device.
        
        Returns:
            Event code or None if no event
        """
        frame = self.generator.generate_event_request()
        self.uart.send_bytes(frame)
        
        response_data = self.uart.recv_bytes(4, timeout_ms=100)  # Event: 2 bytes code + status + CK
        
        if response_data and len(response_data) >= 3:
            event_code = (response_data[0] << 8) | response_data[1]
            print(f"[Master] Event received: 0x{event_code:04X}")
            return event_code
        return None
    
    def run_startup_sequence(self) -> bool:
        """
        Run complete startup sequence.
        
        Returns:
            True if startup successful
        """
        print("[Master] === Starting Startup Sequence ===")
        
        # Send wake-up
        self.send_wakeup()
        time.sleep(0.1)  # Wait for Device to wake up
        
        # Send idle frames to establish communication
        for i in range(3):
            response = self.send_idle()
            if response.valid:
                print(f"[Master] Communication established (attempt {i+1})")
                self.state = MasterState.PREOPERATE
                return True
            time.sleep(0.05)
        
        print("[Master] Startup failed - no valid response")
        return False
    
    def go_to_operate(self) -> bool:
        """Send transition command to Device."""
        print("[Master] Sending OPERATE transition command (MC=0x0F)")
        frame = self.generator.generate_type0(0x0F) # Custom transition MC
        self.uart.send_bytes(frame)
        time.sleep(0.05) # Give device time to switch
        self.state = MasterState.OPERATE
        return True
    
    def run_cycle(self, pd_out: bytes = None, od_req: int = 0, od_req2: int = 0x00, ckt: int = 0x00) -> DeviceResponse:
        """
        Run one communication cycle.
        
        Args:
            pd_out: Process Data Output (for Type 1/2)
            od_req: On-request Data byte (for Type 1/2)

        Returns:
            Device response
        """
        if self.m_seq_type == 0:
            return self.send_idle()
        else:
            # Type 1/2
            if pd_out is None:
                pd_out = bytes([0] * self.pd_out_len)
            
            # Ensure PD length matches
            if len(pd_out) != self.pd_out_len:
                 # Resize or warn? For now truncate/pad
                 pd_out = pd_out[:self.pd_out_len].ljust(self.pd_out_len, b'\x00')
            
            # Generate frame (generator knows od_len)
            frame = self.generator.generate_type1(0x00, ckt, pd_out, od_req, od_req2)
            
            self.uart.send_bytes(frame)
            
            # Receive response: Status(1) + PD_In(n) + OD(od_len) + CK(1)
            expected_len = 1 + self.pd_in_len + self.od_len + 1
            response_data = self.uart.recv_bytes(expected_len, timeout_ms=500)
            
            if response_data:
                return DeviceResponse(response_data, od_len=self.od_len)
            else:
                return DeviceResponse(b'', od_len=self.od_len)

    def run_cycle_bad_crc(self, pd_out: bytes = None, od_req: int = 0, od_req2: int = 0x00, ckt: int = 0x00) -> DeviceResponse:
        """
        Run one communication cycle with CORRUPTED CRC (for testing).
        """
        if self.m_seq_type == 0:
             return self.send_idle() # Not implemented for Type 0 yet
             
        if pd_out is None:
             pd_out = bytes([0] * self.pd_out_len)
        
        # Generate valid frame first
        frame = bytearray(self.generator.generate_type1(0x00, ckt, pd_out, od_req, od_req2))
        
        # Corrupt the Checksum (last byte)
        frame[-1] ^= 0xFF # Flip all bits
        
        self.uart.send_bytes(frame)
        
        # Expect NO response (Device should drop it)
        expected_len = 1 + self.pd_in_len + self.od_len + 1
        response_data = self.uart.recv_bytes(expected_len, timeout_ms=100)
        
        if response_data:
             return DeviceResponse(response_data, od_len=self.od_len)
        else:
             return DeviceResponse(b'', od_len=self.od_len)
    
    def write_isdu(self, index: int, subindex: int, data: bytes) -> bool:
        """
        Write ISDU parameter to Device (supports V1.1.5 Segmented).
        """
        print(f"[Master] ISDU Write request: Index=0x{index:04X}, Subindex=0x{subindex:02X}, Data={data.hex()}")
        
        # Identifier: [CB] [Ident] [CB] [IndexH] [CB] [IndexL] [CB] [Subindex] [CB] [Data1] ...
        data_len = len(data)
        if data_len > 15:
            # Use Extended Length: [Write+Len0(0xA0)] [ExtLen] [IndexH] ...
            service_id = 0xA0
            request_data = [
                service_id,
                data_len, 
                (index >> 8) & 0xFF,
                index & 0xFF,
                subindex
            ] + list(data)
        else:
            # Use Nibble Length: [Write+Len] [IndexH] ...
            request_data = [
                0xA0 | (data_len & 0x0F), 
                (index >> 8) & 0xFF,
                index & 0xFF,
                subindex
            ] + list(data)
        
        interleaved = []
        for i, val in enumerate(request_data):
            is_start = (i == 0)
            is_last = (i == len(request_data) - 1)
            interleaved.append(ISDUControlByte.generate(is_start, is_last, i % 64))
            interleaved.append(val)
            
        def send_and_recv(byte_to_send: int):
            if self.m_seq_type == 0:
                frame = self.generator.generate_type0(byte_to_send)
                self.uart.send_bytes(frame)
                resp = self.uart.recv_bytes(2, timeout_ms=100)
                return DeviceResponse(resp) if resp else None
            else:
                return self.run_cycle(od_req=byte_to_send)

        for val in interleaved:
            resp = send_and_recv(val)
            if not resp or not resp.valid:
                print("[Master] ISDU Write Req failed")
                return False
                
        # Wait for Response Control Byte
        resp = send_and_recv(0x00)
        if not resp or not resp.valid: return False
        
        return True

    def close(self) -> None:
        """Close the virtual Master."""
        if self.uart:
            self.uart.close()
    
    def __enter__(self):
        """Context manager entry."""
        return self
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        """Context manager exit."""
        self.close()
