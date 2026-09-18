"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.
"""

import os
import sys
import pytest
from virtual_master.master import VirtualMaster
from virtual_master.protocol import MSequenceGenerator
from virtual_master.crc import checksum6

"""
Test the diagnosis-channel event memory wire (Table 58/59) byte-for-byte.
"""


sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


class ScriptedUART:
    """UART stub: records master frames and replays scripted device replies."""

    def __init__(self, replies):
        self.sent = []
        self.replies = list(replies)

    def send_bytes(self, data):
        self.sent.append(bytes(data))

    def recv_bytes(self, count, timeout_ms=1000):
        if not self.replies:
            return None
        return self.replies.pop(0)

    def flush(self):
        pass

    def get_device_tty(self):
        return "/dev/null"


def _reply(od):
    ck = checksum6(bytes([od, 0x00]))
    return bytes([od, ck])


def test_diagnosis_read_frame_bytes():
    """Type-0 READ address 0: MC 0xC0, CKT = checksum6([0xC0, 0x00]) = 0x1D."""
    gen = MSequenceGenerator()
    assert gen.generate_diagnosis_read(0x00) == bytes([0xC0, 0x1D])


def test_diagnosis_write_ack_frame_bytes():
    """Type-0 WRITE address 0, OD 0x00: 40 35 00 (Table 59 T8)."""
    gen = MSequenceGenerator()
    assert gen.generate_diagnosis_write(0x00, 0x00) == bytes([0x40, 0x35, 0x00])


def test_read_event_memory_single_slot():
    """StatusCode bit 0 -> slot 1 qualifier/code at addresses 1/2/3."""
    # StatusCode 0x81 (type 2, event 1 active), qualifier 0xF2, code 0x1801.
    uart = ScriptedUART([_reply(0x81), _reply(0xF2), _reply(0x18), _reply(0x01)])
    master = VirtualMaster(uart=uart, m_seq_type=0)

    events = master.read_event_memory()

    assert events == [(0xF2, 0x1801)]
    assert uart.sent == [
        bytes([0xC0, 0x1D]),
        bytes([0xC1, 0x0C]),
        bytes([0xC2, 0x3C]),
        bytes([0xC3, 0x2D]),
    ]


def test_read_event_memory_empty():
    """StatusCode without active slots yields no events."""
    uart = ScriptedUART([_reply(0x80)])
    master = VirtualMaster(uart=uart, m_seq_type=0)

    assert master.read_event_memory() == []
    assert uart.sent == [bytes([0xC0, 0x1D])]


def test_ack_events_sends_statuscode_write():
    """ack_events writes address 0 and expects the one-octet CKS reply."""
    uart = ScriptedUART([bytes([0x35])])
    master = VirtualMaster(uart=uart, m_seq_type=0)

    assert master.ack_events() is True
    assert uart.sent == [bytes([0x40, 0x35, 0x00])]


def test_ack_events_no_reply():
    """A missing CKS reply means the acknowledgement failed."""
    uart = ScriptedUART([])
    master = VirtualMaster(uart=uart, m_seq_type=0)

    assert master.ack_events() is False


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
