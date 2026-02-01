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
    
    def __init__(self, uart: Optional[VirtualUART] = None):
        """
        Initialize Virtual Master.
        
        Args:
            uart: Virtual UART instance (creates new one if None)
        """
        self.uart = uart or VirtualUART()
        self.generator = MSequenceGenerator()
        self.state = MasterState.STARTUP
        self.cycle_time_ms = 10  # Default cycle time
        
    def get_device_tty(self) -> str:
        """Get the TTY path for connecting the Device."""
        return self.uart.get_device_tty()
    
    def send_wakeup(self) -> None:
        """Send wake-up pulse to Device."""
        frame = self.generator.generate_wakeup()
        self.uart.send_bytes(frame)
        print(f"[Master] Sent WAKEUP: {frame.hex()}")
    
    def send_idle(self) -> DeviceResponse:
        """
        Send idle frame and receive Device response.
        
        Returns:
            Device response
        """
        frame = self.generator.generate_idle()
        self.uart.send_bytes(frame)
        
        # Wait for response (2 bytes minimum: status + CK)
        response_data = self.uart.recv_bytes(2, timeout_ms=100)
        
        if response_data:
            response = DeviceResponse(response_data)
            print(f"[Master] Sent IDLE, Received: {response}")
            return response
        else:
            print(f"[Master] Sent IDLE, No response (timeout)")
            return DeviceResponse(b'')
    
    def read_isdu(self, index: int, subindex: int = 0) -> Optional[bytes]:
        """
        Read ISDU parameter from Device.
        
        Args:
            index: ISDU index
            subindex: ISDU subindex
            
        Returns:
            Parameter data or None if failed
        """
        frame = self.generator.generate_isdu_read(index, subindex)
        self.uart.send_bytes(frame)
        print(f"[Master] ISDU Read request: Index=0x{index:02X}, Subindex=0x{subindex:02X}")
        
        # Wait for response
        response_data = self.uart.recv_bytes(2, timeout_ms=100)
        
        if response_data:
            response = DeviceResponse(response_data)
            print(f"[Master] ISDU Read response: {response}")
            return response.payload
        return None
    
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
    
    def run_cycle(self) -> DeviceResponse:
        """
        Run one communication cycle.
        
        Returns:
            Device response
        """
        return self.send_idle()
    
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
