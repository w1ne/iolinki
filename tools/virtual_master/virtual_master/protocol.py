"""
IO-Link protocol implementation - M-sequence generation and parsing.
"""

from enum import IntEnum
from typing import Optional, Tuple
from .crc import calculate_checksum_type0, calculate_checksum_type1


class MSequenceType(IntEnum):
    """M-sequence types as defined in IO-Link specification."""
    TYPE_0 = 0      # On-request data only
    TYPE_1_1 = 1    # PD only, 8-bit OD
    TYPE_1_2 = 2    # PD + ISDU, 8-bit OD
    TYPE_1_V = 3    # PD only, variable OD
    TYPE_2_1 = 4    # PD only, 16/32-bit OD
    TYPE_2_2 = 5    # PD + ISDU, 16/32-bit OD
    TYPE_2_V = 6    # PD only, variable OD + ISDU


class MasterCommand:
    """Master Command (MC) byte definitions."""
    
    # Type 0 commands
    MC_WAKEUP = 0x95
    MC_IDLE = 0x00
    MC_ISDU_READ = 0xA0
    MC_ISDU_WRITE = 0xA1
    
    # Event request
    MC_EVENT_REQ = 0xA2
    
    @staticmethod
    def is_isdu_command(mc: int) -> bool:
        """Check if MC is an ISDU command."""
        return mc in [MasterCommand.MC_ISDU_READ, MasterCommand.MC_ISDU_WRITE]


class MSequenceGenerator:
    """Generate IO-Link M-sequences (Master frames)."""
    
    def __init__(self):
        self.sequence_type = MSequenceType.TYPE_0
    
    def generate_type0(self, mc: int, ckt: int = 0x00) -> bytes:
        """
        Generate Type 0 M-sequence: MC + CKT + CK
        
        Args:
            mc: Master Command byte
            ckt: Checksum Type (usually 0x00)
            
        Returns:
            3-byte frame: [MC, CKT, CK]
        """
        ck = calculate_checksum_type0(mc, ckt)
        return bytes([mc, ckt, ck])
    
    def generate_wakeup(self) -> bytes:
        """Generate wake-up sequence."""
        return self.generate_type0(MasterCommand.MC_WAKEUP)
    
    def generate_idle(self) -> bytes:
        """Generate idle sequence."""
        return self.generate_type0(MasterCommand.MC_IDLE)
    
    def generate_isdu_read(self, index: int, subindex: int = 0) -> bytes:
        """
        Generate ISDU Read request.
        
        Args:
            index: ISDU index (0-255)
            subindex: ISDU subindex (0-255)
            
        Returns:
            M-sequence for ISDU read
        """
        # For Type 0, we encode index in MC byte
        # Full implementation would use Type 1_2 or 2_2
        mc = MasterCommand.MC_ISDU_READ
        return self.generate_type0(mc)
    
    def generate_event_request(self) -> bytes:
        """Generate event request sequence."""
        return self.generate_type0(MasterCommand.MC_EVENT_REQ)


class DeviceResponse:
    """Parse Device response frames."""
    
    def __init__(self, data: bytes):
        """
        Parse Device response.
        
        Args:
            data: Raw response bytes
        """
        self.raw = data
        self.valid = len(data) >= 2
        
        if self.valid:
            self.status = data[0] if len(data) > 0 else 0
            self.checksum = data[-1] if len(data) > 1 else 0
            self.payload = data[1:-1] if len(data) > 2 else b''
    
    def has_event(self) -> bool:
        """Check if Device has pending event."""
        return bool(self.status & 0x01) if self.valid else False
    
    def is_valid_checksum(self) -> bool:
        """Verify checksum (simplified)."""
        # TODO: Implement proper CRC verification
        return self.valid
    
    def __repr__(self) -> str:
        return f"DeviceResponse(status=0x{self.status:02X}, payload={self.payload.hex()}, ck=0x{self.checksum:02X})"
