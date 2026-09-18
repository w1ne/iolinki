"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.
"""

from enum import IntEnum
from .crc import (
    CHECKSUM_MASK,
    M_SEQUENCE_TYPE_MASK,
    checksum6,
)

"""
IO-Link protocol implementation - M-sequence generation and parsing.
"""


class MSequenceType(IntEnum):
    """M-sequence types as defined in IO-Link specification."""

    TYPE_0 = 0  # On-request data only
    TYPE_1_1 = 1  # PD only, 1-byte OD
    TYPE_1_2 = 2  # PD + ISDU, 1-byte OD
    TYPE_1_V = 3  # Variable PD, 1-byte OD
    TYPE_2_1 = 4  # PD only, 2-byte OD
    TYPE_2_2 = 5  # PD + ISDU, 2-byte OD
    TYPE_2_V = 6  # Variable PD, 2-byte OD

    @staticmethod
    def get_od_len(m_type: int) -> int:
        """Get OD length for M-sequence type."""
        if m_type >= 4:  # Type 2_x
            return 2
        return 1  # Type 0, 1_x


class MasterCommand:
    """Master Command (MC) byte definitions."""

    MC_WAKEUP = 0x95
    MC_IDLE = 0x00
    MC_ISDU_READ = 0xA0
    MC_ISDU_WRITE = 0xA1

    MC_EVENT_REQ = 0xA2

    # Spec-conformant startup probe (transition T1): Type-0 READ (RW=1, 0x80)
    # on the page communication channel (0x20) of Direct Parameter address
    # 0x02 = MinCycleTime. 0x80 | 0x20 | 0x02 = 0xA2.
    MC_STARTUP_PROBE = 0xA2

    # Spec DeviceOperate: page-channel WRITE (RW=0) at Direct Parameter
    # address 0x00 (0x20 | 0x00) carrying MasterCommand 0x99 (DeviceOperate).
    MC_DEVICE_OPERATE = 0x20
    OD_DEVICE_OPERATE = 0x99

    @staticmethod
    def is_isdu_command(mc: int) -> bool:
        """Check if MC is an ISDU command."""
        return mc in [MasterCommand.MC_ISDU_READ, MasterCommand.MC_ISDU_WRITE]


class IOChannel:
    """Communication channels encoded in the MC bits 5-6 (A.1.2, Table A.1)."""

    PROCESS = 0x00
    PAGE = 0x20
    DIAGNOSIS = 0x40
    ISDU = 0x60
    MASK = 0x60


class FlowCtrl:
    """ISDU FlowCTRL values carried in the MC address bits (Table 52)."""

    START = 0x10
    IDLE = 0x11
    ABORT = 0x1F
    # FlowCTRL lives in the MC address field (5 bits), Table A.1/52.
    MASK = 0x1F
    COUNT_MASK = 0x0F


def isdu_chkpdu(octets: bytes) -> int:
    """CHKPDU = XOR of all ISDU octets with the checksum octet read as 0 (A.5.6)."""
    value = 0
    for octet in octets:
        value ^= octet
    return value


class MSequenceGenerator:
    """Generate IO-Link M-sequences (Master frames)."""

    def __init__(self, od_len: int = 1):
        """
        Initialize generator.

        Args:
            od_len: OD length in bytes (1 or 2)
        """
        self.sequence_type = MSequenceType.TYPE_0
        self.od_len = od_len

    def generate_type0(self, mc: int, ckt: int = 0x00) -> bytes:
        """
        Generate Type 0 M-sequence: MC + CK

        Args:
            mc: Master Command byte
            ckt: Checksum Type (usually 0x00)

        Returns:
            2-byte frame: [MC, CK]
        """
        ckt_value = (ckt & M_SEQUENCE_TYPE_MASK) | checksum6(bytes([mc, ckt & M_SEQUENCE_TYPE_MASK]))
        return bytes([mc, ckt_value])

    def generate_wakeup(self) -> bytes:
        """Generate wake-up sequence."""
        return self.generate_type0(MasterCommand.MC_WAKEUP)

    def generate_idle(self) -> bytes:
        """Generate the spec startup probe frame.

        The device establishes communication on a Type-0 READ of the page
        communication channel (MC=0xA2, MinCycleTime). It replies with the
        MinCycleTime octet, which the master uses to confirm the link.
        """
        return self.generate_type0(MasterCommand.MC_STARTUP_PROBE)

    def generate_device_operate(self) -> bytes:
        """Generate the spec DeviceOperate transition frame.

        Type-0 WRITE: MC 0x20 (page channel, address 0x00) + OD 0x99
        (MasterCommand DeviceOperate) + 6-bit CRC over [MC, OD].
        """
        return self.generate_type0_write(
            MasterCommand.MC_DEVICE_OPERATE, MasterCommand.OD_DEVICE_OPERATE
        )

    def generate_type0_write(self, mc: int, od: int) -> bytes:
        """Generate a 3-octet Type-0 WRITE frame: [MC, CKT, OD].

        The A.1.6 checksum lives in the CKT octet (bits 0-5); per A.1.5 the
        message ends with the OD octet. Mirrors the device request parser.
        """
        ckt = (mc & M_SEQUENCE_TYPE_MASK) | checksum6(bytes([mc, 0x00, od]))
        return bytes([mc, ckt, od])

    def build_isdu_read_request(self, index: int, subindex: int = 0) -> bytes:
        """Build the ISDU octet stream for a read (C3, Table A.12/A.13).

        16-bit index use I-Service 0xB; the Length nibble counts every ISDU
        octet including the I-Service/Length octet and the CHKPDU (A.5.3).
        """
        body = bytes([(index >> 8) & 0xFF, index & 0xFF, subindex])
        total = len(body) + 2  # I-Service/Length + body + CHKPDU
        isdu = bytes([0xB0 | total]) + body
        return isdu + bytes([isdu_chkpdu(isdu + bytes([0x00]))])

    def build_isdu_write_request(
        self, index: int, subindex: int, data: bytes
    ) -> bytes:
        """Build the ISDU octet stream for a write (C3, Table A.12/A.13).

        Length counts every ISDU octet including I-Service/Length and CHKPDU
        (A.5.3): 16-bit index + subindex + data + 2 overhead octets.
        """
        direct_total = len(data) + 5
        if direct_total <= 15:
            head = bytes(
                [0x30 | direct_total, (index >> 8) & 0xFF, index & 0xFF, subindex]
            )
        else:
            ext_total = len(data) + 6  # + ExtLength octet
            head = bytes(
                [0x31, ext_total & 0xFF, (index >> 8) & 0xFF, index & 0xFF, subindex]
            )
        isdu = head + data
        return isdu + bytes([isdu_chkpdu(isdu + bytes([0x00]))])

    def generate_isdu_channel(self, rw: int, flowctrl: int, od: bytes = b"") -> bytes:
        """Generate an M-sequence on the ISDU channel (Table 52).

        TYPE_0 M-sequence: MC, CKT, OD... with the A.1.6 checksum in CKT.
        The request is carried by the OD octets; FlowCTRL lives in the MC
        address bits.
        """
        mc = (rw & 0x80) | IOChannel.ISDU | (flowctrl & FlowCtrl.MASK)
        frame = bytearray([mc, 0x00]) + bytearray(od)
        frame[1] = (frame[1] & M_SEQUENCE_TYPE_MASK) | checksum6(bytes(frame))
        return bytes(frame)

    def generate_type1(
        self, mc: int, ckt: int, pd: bytes, od: int, od2: int = 0x00
    ) -> bytes:
        """
        Generate Type 1/2 M-sequence: MC + CKT + PD + OD(1 or 2 bytes) + CK

        Args:
            mc: Master Command byte
            ckt: Command/Key/Type byte
            pd: Process Data bytes
            od: On-request Data byte (first byte)
            od2: Second OD byte (for Type 2 only)

        Returns:
            Frame bytes
        """
        type_bits = ckt & M_SEQUENCE_TYPE_MASK
        if self.od_len == 2:
            body = bytes([mc, type_bits]) + pd + bytes([od, od2])
        else:
            body = bytes([mc, type_bits]) + pd + bytes([od])
        ckt_value = type_bits | checksum6(body)
        return bytes([mc, ckt_value]) + body[2:]

    def generate_event_request(self) -> bytes:
        """Generate event request sequence."""
        return self.generate_type0(MasterCommand.MC_EVENT_REQ)


class DeviceResponse:
    """Parse a Device reply: [PD-in octets][OD octets] CKS (A.1.5).

    There is no leading status octet: the Event flag (bit 7) and the PD
    validity flag (bit 6, 1 = invalid) live in the trailing CKS octet together
    with the 6-bit message checksum.
    """

    def __init__(self, data: bytes, od_len: int = 1, pd_in_len: int = 0):
        self.raw = data
        self.od_len = od_len
        self.pd_in_len = pd_in_len
        self.valid = len(data) >= 1

        self.checksum_ok = None
        self.pd_valid = False
        self.od = 0
        self.od2 = None
        self.payload = b""
        self.pd = b""

        if not self.valid:
            self.checksum = 0
            return

        self.checksum = data[-1]
        body = data[:-1]

        expected = pd_in_len + od_len
        if len(body) > expected and od_len > 0:
            print(f"[DEBUG] Resp raw={data.hex()} expected body={expected}")

        self.pd = body[:pd_in_len]
        self.payload = body[pd_in_len:]
        if len(body) >= (pd_in_len + 1):
            self.od = body[pd_in_len]
            if od_len >= 2 and len(body) >= (pd_in_len + 2):
                self.od2 = body[pd_in_len + 1]

        frame = bytearray(data)
        frame[-1] &= M_SEQUENCE_TYPE_MASK  # checksum bits zeroed, flags kept
        self.checksum_ok = checksum6(bytes(frame)) == (self.checksum & CHECKSUM_MASK)
        self.pd_valid = not bool(self.checksum & 0x40)

    def has_event(self) -> bool:
        """True when the Device set the Event flag in CKS bit 7."""
        return bool(self.checksum & 0x80) if self.valid else False

    def is_valid_checksum(self) -> bool:
        if self.checksum_ok is None:
            return self.valid
        return self.checksum_ok

    def __repr__(self) -> str:
        ck_str = f"ck=0x{self.checksum:02X}"
        if self.valid:
            ck_str += (
                f",event={self.has_event()},pd_valid={self.pd_valid},"
                f"ck_ok={self.checksum_ok}"
            )
        return (
            f"DeviceResponse(pd={self.pd.hex()}, payload={self.payload.hex()}, "
            f"od=0x{self.od:02X}, {ck_str})"
        )
