"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.
"""

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

class ISDUControlByte:
    """IO-Link V1.1.5 ISDU Control Byte bits."""
    START = 0x80
    LAST = 0x40
    SEQ_MASK = 0x3F

    @staticmethod
    def generate(start: bool, last: bool, seq: int) -> int:
        cb = (seq & ISDUControlByte.SEQ_MASK)
        if start: cb |= ISDUControlByte.START
        if last: cb |= ISDUControlByte.LAST
        return cb


class MSequenceGenerator:
    """Generate IO-Link M-sequences (Master frames)."""
    
    def __init__(self):
        self.sequence_type = MSequenceType.TYPE_0
    
    def generate_type0(self, mc: int, ckt: int = 0x00) -> bytes:
        """
        Generate Type 0 M-sequence: MC + CK
        
        Args:
            mc: Master Command byte
            ckt: Checksum Type (usually 0x00)
            
        Returns:
            2-byte frame: [MC, CK]
        """
        ck = calculate_checksum_type0(mc, ckt)
        return bytes([mc, ck])
    
    def generate_wakeup(self) -> bytes:
        """Generate wake-up sequence."""
        return self.generate_type0(MasterCommand.MC_WAKEUP)
    
    def generate_idle(self) -> bytes:
        """Generate idle sequence."""
        return self.generate_type0(MasterCommand.MC_IDLE)
    
    def generate_isdu_read(self, index: int, subindex: int = 0) -> list[bytes]:
        """
        Generate ISDU Read request frames (Old Type 0 / Type 1 legacy).
        """
        frames = []
        frames.append(self.generate_type0(0x90))
        frames.append(self.generate_type0((index >> 8) & 0xFF))
        frames.append(self.generate_type0(index & 0xFF))
        frames.append(self.generate_type0(subindex))
        return frames

    def generate_isdu_read_v11(self, index: int, subindex: int = 0) -> list[int]:
        """
        Generate ISDU Read request BYTES (excluding M-seq framing) for V1.1.5.
        Interleaves Control Bytes.
        """
        data = [
            0x90, # Read Service, Len 0
            (index >> 8) & 0xFF,
            index & 0xFF,
            subindex
        ]
        
        interleaved = []
        for i, val in enumerate(data):
            is_start = (i == 0)
            is_last = (i == len(data) - 1)
            # Control Byte
            interleaved.append(ISDUControlByte.generate(is_start, is_last, i))
            # Data Byte
            interleaved.append(val)
            
        return interleaved
    
    def generate_type1(self, mc: int, ckt: int, pd: bytes, od: int) -> bytes:
        """
        Generate Type 1 M-sequence: MC + CKT + PD + OD + CK
        
        Args:
            mc: Master Command byte
            ckt: Command/Key/Type byte
            pd: Process Data bytes
            od: On-request Data byte
            
        Returns:
            Frame bytes
        """
        ck = calculate_checksum_type1(mc, ckt, pd, od)
        return bytes([mc, ckt]) + pd + bytes([od, ck])

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
            if len(data) == 2:
                # Type 0 Response: [OD, Checksum]
                # Status is encoded in checksum
                self.payload = data[0:1]
                self.checksum = data[1]
                self.status = 0 # Cannot extract easily without brute forcing checksum
            else:
                # Type 1/2 Response: [Status, PD_In..., OD, Checksum]
                self.status = data[0]
                self.payload = data[1:-2]
                self.od = data[-2]
                self.checksum = data[-1]
    
    def has_event(self) -> bool:
        """Check if Device has pending event."""
        return bool(self.status & 0x01) if self.valid else False
    
    def is_valid_checksum(self) -> bool:
        """Verify checksum (simplified)."""
        # TODO: Implement proper CRC verification
        return self.valid
    
    def __repr__(self) -> str:
        return f"DeviceResponse(status=0x{self.status:02X}, payload={self.payload.hex()}, ck=0x{self.checksum:02X})"
