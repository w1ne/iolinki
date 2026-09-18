"""
Copyright (C) 2026 Andrii Shylenko
SPDX-License-Identifier: GPL-3.0-or-later

This file is part of iolinki.
See LICENSE for details.
"""

from enum import Enum
import time
from typing import Optional
from .uart import VirtualUART
from .protocol import (
    MSequenceGenerator,
    DeviceResponse,
    FlowCtrl,
    IOChannel,
    MSequenceType,
)

"""
IO-Link Master state machine.
"""


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

    @property
    def m_seq_type(self) -> int:
        return self._m_seq_type

    @m_seq_type.setter
    def m_seq_type(self, value: int) -> None:
        self._m_seq_type = value
        from .protocol import MSequenceType

        self.od_len = MSequenceType.get_od_len(value)
        if hasattr(self, "generator"):
            self.generator.od_len = self.od_len

    def __init__(
        self,
        uart: Optional[VirtualUART] = None,
        m_seq_type: int = 0,
        pd_in_len: int = 0,
        pd_out_len: int = 0,
    ):
        """
        Initialize Virtual Master.

        Args:
            uart: Virtual UART instance (creates new one if None)
            m_seq_type: M-sequence type (0, 1, 2, 4, 5, etc.)
            pd_in_len: Process Data Input length (bytes)
            pd_out_len: Process Data Output length (bytes)
        """
        self.uart = uart or VirtualUART()
        self.m_seq_type = m_seq_type
        self.pd_in_len = pd_in_len
        self.pd_out_len = pd_out_len

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
        print(f"[Master] Baudrate set to {baudrate}")
        return True

    def send_wakeup(self) -> None:
        """Send wake-up pulse (simulated by dummy byte) to Device."""
        self.uart.send_bytes(bytes([0x55]))
        print("[Master] Sent WAKEUP (dummy byte)")

    def send_idle(self) -> DeviceResponse:
        """
        Send idle frame and receive Device response.

        Returns:
            Device response
        """
        frame = self.generator.generate_idle()
        self.uart.send_bytes(frame)

        response_data = self.uart.recv_bytes(2, timeout_ms=1500)

        if response_data:
            response = DeviceResponse(response_data)
            print(f"[Master] Sent IDLE, Received: {response}")
            return response
        else:
            print("[Master] Sent IDLE, No response (timeout)")
            return DeviceResponse(b"")

    def send_bad_crc_type0(self, mc: int = 0x00) -> None:
        """Send Type 0 frame with bad CRC."""
        frame = bytearray(self.generator.generate_type0(mc))
        frame[-1] ^= 0xFF
        self.uart.send_bytes(frame)
        print(f"[Master] Sent Type 0 frame (MC=0x{mc:02X}) with BAD CRC")

    def _type_bits(self) -> int:
        """CKT type bits for the configured M-sequence type (A.1.2)."""
        if self.m_seq_type >= MSequenceType.TYPE_2_1:
            return 0x80
        if self.m_seq_type >= MSequenceType.TYPE_1_1:
            return 0x40
        return 0x00

    def _isdu_od_exchange(
        self, read: bool, flowctrl: int, od: bytes = b""
    ) -> Optional[DeviceResponse]:
        """Send one OD message on the ISDU channel and return the reply (C3)."""
        rw = 0x80 if read else 0x00
        mc = rw | IOChannel.ISDU | (flowctrl & FlowCtrl.MASK)

        if self.m_seq_type == MSequenceType.TYPE_0:
            frame = self.generator.generate_isdu_channel(rw, flowctrl, od)
            self.uart.send_bytes(frame)
            reply_len = (len(od) if od else self.od_len) + 1
            if read:
                reply_len = self.od_len + 1
            else:
                reply_len = 1
            data = self.uart.recv_bytes(reply_len, timeout_ms=300)
            if not data:
                return None
            return DeviceResponse(data, od_len=self.od_len, pd_in_len=0)

        pd = bytes([0] * self.pd_out_len)
        od0 = od[0] if len(od) > 0 else 0
        od1 = od[1] if len(od) > 1 else 0
        frame = self.generator.generate_type1(mc, self._type_bits(), pd, od0, od1)
        self.uart.send_bytes(frame)
        # A multi-type reply is always [PD-in][OD] CKS (A.1.5).
        reply_len = self.pd_in_len + self.od_len + 1
        data = self.uart.recv_bytes(reply_len, timeout_ms=300)
        if not data:
            return None
        return DeviceResponse(
            data, od_len=self.od_len if read else 0, pd_in_len=self.pd_in_len
        )

    def _isdu_send_request(self, request: bytes) -> bool:
        """Send the ISDU request octets over consecutive ISDU-channel writes (C3)."""
        width = self.od_len
        chunks = [request[i : i + width] for i in range(0, len(request), width)]
        for i, chunk in enumerate(chunks):
            flowctrl = FlowCtrl.START if i == 0 else (i & FlowCtrl.COUNT_MASK)
            resp = self._isdu_od_exchange(False, flowctrl, chunk)
            if resp is None or not resp.valid:
                print(f"[Master] ISDU request write failed at chunk {i}")
                return False
        return True

    def _isdu_read_response(self) -> Optional[bytes]:
        """Poll the framed ISDU response over ISDU-channel reads (C3, Table A.14)."""
        octets = bytearray()
        need = 0
        flowctrl = FlowCtrl.START
        busy_retries = 0

        while need == 0 or len(octets) < need:
            resp = self._isdu_od_exchange(True, flowctrl, b"")
            if resp is None or not resp.valid:
                busy_retries += 1
                if busy_retries > 100:
                    print("[Master] ISDU response read timeout")
                    return None
                time.sleep(0.001)
                continue

            chunk = resp.payload
            if not octets and chunk and chunk[0] == 0x01:
                busy_retries += 1
                if busy_retries > 100:
                    print("[Master] ISDU stayed Busy")
                    return None
                time.sleep(0.001)
                continue

            octets += chunk
            if need == 0 and len(octets) >= 1:
                nibble = octets[0] & 0x0F
                if nibble == 1:
                    if len(octets) >= 2:
                        need = octets[1]
                    else:
                        continue
                elif nibble == 0:
                    need = 1
                else:
                    need = nibble
            flowctrl = (flowctrl + 1) & FlowCtrl.COUNT_MASK

        return bytes(octets[:need])

    def read_isdu(self, index: int, subindex: int = 0) -> Optional[bytes]:
        """Read an ISDU parameter over the spec ISDU channel (C3)."""
        print(
            f"[Master] ISDU Read request: Index=0x{index:04X}, Subindex=0x{subindex:02X}"
        )
        request = self.generator.build_isdu_read_request(index, subindex)
        if not self._isdu_send_request(request):
            return None

        framed = self._isdu_read_response()
        if framed is None or len(framed) < 2:
            print("[Master] ISDU read: no framed response")
            return None

        service = framed[0] >> 4
        if service == 0xC:
            print(
                f"[Master] ISDU negative response: "
                f"code=0x{framed[1]:02X} add=0x{framed[2]:02X}"
            )
            return None
        if service != 0xD:
            print(f"[Master] ISDU unexpected response service 0x{service:X}")
            return None

        nibble = framed[0] & 0x0F
        if nibble == 1:
            total = framed[1]
            data = framed[3:total]
        else:
            total = nibble
            data = framed[1 : total - 1]

        print(f"[Master] ISDU Read complete: {data.hex()}")
        return bytes(data)

    def request_event(self) -> Optional[int]:
        """
        Request event from Device.

        Returns:
            Event code or None if no event
        """
        frame = self.generator.generate_event_request()
        self.uart.send_bytes(frame)

        response_data = self.uart.recv_bytes(
            4, timeout_ms=100
        )  # Event: 2 bytes code + status + CK

        if response_data and len(response_data) >= 3:
            event_code = (response_data[0] << 8) | response_data[1]
            print(f"[Master] Event received: 0x{event_code:04X}")
            return event_code
        return None

    def run_startup_sequence(self, send_wakeup: bool = True) -> bool:
        """
        Run complete startup sequence.

        Args:
            send_wakeup: Send the 0x55 wake-up byte before the IDLE frames.
                This simulates the electrical C/Q wake-up pulse for PHYs that
                detect it from the byte stream (e.g. the virtual PHY). On an
                already-established-COM link such as a plain UART behind a
                transceiver front-end, no wake-up byte exists on the wire; it
                would be a spurious data byte that desynchronises framing, so
                pass send_wakeup=False there.

        Returns:
            True if startup successful
        """
        print("[Master] === Starting Startup Sequence ===")

        if send_wakeup:
            self.send_wakeup()
            # The first message must follow within T_DSIO (60..300 ms, Table 42)
            # or the Device falls back to SIO before it can answer.
            time.sleep(0.1)
        elif hasattr(self.uart, "flush"):
            # Established-COM link: discard boot noise / partial frames before
            # establishing comms (no wake-up byte delimits the start here).
            self.uart.flush()

        for i in range(10):  # Increased retries for CI stability
            response = self.send_idle()
            if response.valid:
                print(f"[Master] Communication established (attempt {i + 1})")
                self.state = MasterState.PREOPERATE
                return True
            time.sleep(0.2)

        print("[Master] Startup failed - no valid response")
        return False

    def go_to_operate(self) -> bool:
        """Send the spec DeviceOperate transition to the Device.

        Type-0 WRITE: MC 0x20 (page channel, addr 0x00) + OD 0x99
        (DeviceOperate) + CRC6. No response per spec.
        """
        print("[Master] Sending DeviceOperate transition (MC=0x20, OD=0x99)")
        frame = self.generator.generate_device_operate()
        self.uart.send_bytes(frame)
        # A Type-0 write is answered by the CKS only (Figure A.5); consume it.
        self.uart.recv_bytes(1, timeout_ms=300)
        time.sleep(0.05)  # Give device time to switch
        self.state = MasterState.OPERATE
        return True

    def run_cycle(
        self,
        pd_out: bytes = None,
        od_req: int = 0,
        od_req2: int = 0x00,
        ckt: int = 0x00,
    ) -> DeviceResponse:
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
            if pd_out is None:
                pd_out = bytes([0] * self.pd_out_len)

            if len(pd_out) != self.pd_out_len:
                pd_out = pd_out[: self.pd_out_len].ljust(self.pd_out_len, b"\x00")

            frame = self.generator.generate_type1(
                0x00, (ckt & 0x3F) | self._type_bits(), pd_out, od_req, od_req2
            )

            self.uart.send_bytes(frame)

            expected_len = self.pd_in_len + self.od_len + 1
            response_data = self.uart.recv_bytes(expected_len, timeout_ms=1500)

            if response_data:
                return DeviceResponse(
                    response_data, od_len=self.od_len, pd_in_len=self.pd_in_len
                )
            else:
                return DeviceResponse(b"", od_len=self.od_len, pd_in_len=self.pd_in_len)

    def run_cycle_bad_crc(
        self,
        pd_out: bytes = None,
        od_req: int = 0,
        od_req2: int = 0x00,
        ckt: int = 0x00,
    ) -> DeviceResponse:
        """
        Run one communication cycle with CORRUPTED CRC (for testing).
        """
        if self.m_seq_type == 0:
            return self.send_idle()  # Not implemented for Type 0 yet

        if pd_out is None:
            pd_out = bytes([0] * self.pd_out_len)

        frame = bytearray(
            self.generator.generate_type1(
                0x00, (ckt & 0x3F) | self._type_bits(), pd_out, od_req, od_req2
            )
        )

        frame[1] ^= 0x3F  # Corrupt the A.1.6 checksum in the CKT octet

        self.uart.send_bytes(frame)

        expected_len = self.pd_in_len + self.od_len + 1
        response_data = self.uart.recv_bytes(expected_len, timeout_ms=100)

        if response_data:
            return DeviceResponse(
                response_data, od_len=self.od_len, pd_in_len=self.pd_in_len
            )
        else:
            return DeviceResponse(b"", od_len=self.od_len, pd_in_len=self.pd_in_len)

    def inject_sio_fallback(self, count: int = 3) -> None:
        """
        Inject multiple errors to force the Device into SIO fallback.

        Args:
            count: Number of consecutive errors to inject (default 3)
        """
        print(f"[Master] Injecting SIO Fallback ({count} errors)...")
        for i in range(count):
            if self.m_seq_type == 0:
                self.send_bad_crc_type0()
            else:
                self.run_cycle_bad_crc()
            time.sleep(0.05)
        print("[Master] SIO Fallback injection complete. Device should be in SIO mode.")

    def write_isdu(self, index: int, subindex: int, data: bytes) -> bool:
        """Write an ISDU parameter over the spec ISDU channel (C3)."""
        print(
            f"[Master] ISDU Write request: Index=0x{index:04X}, Subindex=0x{subindex:02X}, Data={data.hex()}"
        )
        request = self.generator.build_isdu_write_request(index, subindex, data)
        if not self._isdu_send_request(request):
            return False

        framed = self._isdu_read_response()
        if framed is None or len(framed) < 2:
            print("[Master] ISDU write: no framed response")
            return False

        service = framed[0] >> 4
        if service == 0x4:
            print(
                f"[Master] ISDU write negative response: "
                f"code=0x{framed[1]:02X} add=0x{framed[2]:02X}"
            )
            return False
        if service != 0x5:
            print(f"[Master] ISDU write unexpected response service 0x{service:X}")
            return False

        print("[Master] ISDU write confirmed")
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
