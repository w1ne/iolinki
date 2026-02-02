"""
IO-Link Master state machine.
"""

from enum import Enum
import time
from typing import Optional
from .uart import VirtualUART
from .protocol import MSequenceGenerator, DeviceResponse


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
            m_seq_type: M-sequence type (0, 1, 2)
            pd_in_len: Process Data Input length (bytes)
            pd_out_len: Process Data Output length (bytes)
        """
        self.uart = uart or VirtualUART()
        self.generator = MSequenceGenerator()
        self.state = MasterState.STARTUP
        self.cycle_time_ms = 10  # Default cycle time
        
        self.m_seq_type = m_seq_type
        self.pd_in_len = pd_in_len
        self.pd_out_len = pd_out_len
        
    def get_device_tty(self) -> str:
        """Get the TTY path for connecting the Device."""
        return self.uart.get_device_tty()
    
    def send_wakeup(self) -> None:
        """Send wake-up pulse (simulated by dummy byte) to Device."""
        self.uart.send_bytes(bytes([0x00]))
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
        Read ISDU parameter from Device (supports Type 0 and Type 1).
        
        Args:
            index: ISDU index
            subindex: ISDU subindex
            
        Returns:
            Parameter data or None if failed
        """
        print(f"[Master] ISDU Read request: Index=0x{index:02X}, Subindex=0x{subindex:02X}")
        frames = self.generator.generate_isdu_read(index, subindex)
        
        data_bytes = bytearray()
        
        # Helper to send frame based on M-seq type
        def send_and_recv(byte_to_send: int):
            if self.m_seq_type == 0:
                # Type 0: MC = byte_to_send
                frame = self.generator.generate_type0(byte_to_send)
                self.uart.send_bytes(frame)
                resp = self.uart.recv_bytes(2, timeout_ms=100)
                return DeviceResponse(resp) if resp else None
            else:
                # Type 1: Send byte in OD channel, with dummy PD
                # MC = 0x00 (IDLE)
                pd_dummy = bytes([0] * self.pd_out_len)
                return self.run_cycle(pd_out=pd_dummy, od_req=byte_to_send)

        # 1. Send Request Frames
        for i, frame_bytes in enumerate(frames):
            # Extract 'MC' from the generated Type 0 frame
            # frame_bytes is [MC, CK]. We only need MC for Type 1 OD.
            mc_byte = frame_bytes[0]
            
            resp = send_and_recv(mc_byte)
            if not resp or not resp.valid:
                print(f"[Master] ISDU Req failed at frame {i}")
                return None
            
            # Last request frame (Subindex) triggers first response byte in OD
            if i == len(frames) - 1:
                # For Type 1, the response OD is the byte
                if self.m_seq_type == 0:
                     if resp.payload: data_bytes.extend(resp.payload)
                else:
                     # Type 1: Payload is PD, OD is separate
                     # But our DeviceResponse parser puts OD in .od
                     if hasattr(resp, 'od'):
                         byte_val = resp.od
                         # 0x00 might be valid data, or idle. 
                         # ISDU protocol needs flow control bits to be robust.
                         # Simplified: Just collect it.
                         data_bytes.append(byte_val)

        # 2. Clock out data
        # We need to send IDLE commands (0x00) to clock out the rest
        # In Type 1, that means sending 0x00 in OD channel
        
        # Read up to 32 bytes or until we get 0x00 (simplified)
        for _ in range(32):
            resp = send_and_recv(0x00) # MC_IDLE / OD_IDLE
            if not resp or not resp.valid: break
            
            val = 0
            if self.m_seq_type == 0:
                if resp.payload: val = resp.payload[0]
            else:
                if hasattr(resp, 'od'): val = resp.od
            
            # Simple termination check for strings (or if 0 is received?)
            # Ideally we check 'ISDU Service' state.
            # Host demo returns 0 when no data.
            # But 0 is valid for binary.
            # Let's assume string for now (Vendor Name)
            if val == 0:
                break
            
            data_bytes.append(val)
            
        print(f"[Master] ISDU Read collected {len(data_bytes)} bytes: {data_bytes.hex()}")
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
        return True
    
    def run_cycle(self, pd_out: bytes = None, od_req: int = 0) -> DeviceResponse:
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
            
            # Use fixed CKT for now: 0x00 (Event flow control bits are here usually)
            ckt = 0x00 
            
            # Generate frame
            # MC is usually OPERATE mode command or IDLE?
            # In Operate, MC is usually interleaved or IDLE?
            # Spec says: MC byte controls logic. 
            # If we just want cyclic exchange, we can use valid MC or keep using IDLE (0) if supported.
            # But standard Operate usually implies specific MC flow.
            # Let's use MC_IDLE (0x00) for basic cyclic exchange.
            # Note: MC byte logic differs in Operate.
            frame = self.generator.generate_type1(0x00, ckt, pd_out, od_req)
            
            self.uart.send_bytes(frame)
            
            # Receive response: Status(1) + PD_In(n) + OD(1) + CK(1)
            expected_len = 1 + self.pd_in_len + 1 + 1
            response_data = self.uart.recv_bytes(expected_len, timeout_ms=500)
            
            if response_data:
                return DeviceResponse(response_data)
            else:
                return DeviceResponse(b'')

    def run_cycle_bad_crc(self, pd_out: bytes = None, od_req: int = 0) -> DeviceResponse:
        """
        Run one communication cycle with CORRUPTED CRC (for testing).
        """
        if self.m_seq_type == 0:
             return self.send_idle() # Not implemented for Type 0 yet
             
        if pd_out is None:
             pd_out = bytes([0] * self.pd_out_len)
        
        ckt = 0x00
        # Generate valid frame first
        frame = bytearray(self.generator.generate_type1(0x00, ckt, pd_out, od_req))
        
        # Corrupt the Checksum (last byte)
        frame[-1] ^= 0xFF # Flip all bits
        
        self.uart.send_bytes(frame)
        
        # Expect NO response (Device should drop it)
        expected_len = 1 + self.pd_in_len + 1 + 1
        response_data = self.uart.recv_bytes(expected_len, timeout_ms=100)
        
        if response_data:
             return DeviceResponse(response_data)
        else:
             return DeviceResponse(b'')
    
    def write_isdu(self, index: int, subindex: int, data: bytes) -> bool:
        """
        Write ISDU parameter to Device.
        
        Args:
            index: ISDU index
            subindex: ISDU subindex
            data: Data to write (max 15 bytes for now)
            
        Returns:
            True if successful
        """
        print(f"[Master] ISDU Write request: Index=0x{index:02X}, Subindex=0x{subindex:02X}, Data={data.hex()}")
        
        if len(data) > 15:
            print("[Master] Error: Data length > 15 not supported yet")
            return False
            
        # Helper to send frame based on M-seq type
        def send_frame(byte_to_send: int):
            if self.m_seq_type == 0:
                frame = self.generator.generate_type0(byte_to_send)
                self.uart.send_bytes(frame)
                self.uart.recv_bytes(2, timeout_ms=100)
            else:
                pd_dummy = bytes([0] * self.pd_out_len)
                self.run_cycle(pd_out=pd_dummy, od_req=byte_to_send)
        
        # 1. Start Write (Identifier=0xA0 | Length)
        ident = 0xA0 | (len(data) & 0x0F)
        send_frame(ident)
        
        # 2. Index High
        send_frame((index >> 8) & 0xFF)
        
        # 3. Index Low
        send_frame(index & 0xFF)
        
        # 4. Subindex
        send_frame(subindex)
        
        # 5. Data Bytes
        for b in data:
            send_frame(b)
            
        # 6. Wait for response/ACK?
        # Device assumes Write is pushed.
        # Flow control in real stack would indicate Busy/Ready.
        # Here we just assume it worked.
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
