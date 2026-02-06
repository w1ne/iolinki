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
from .protocol import MSequenceGenerator, DeviceResponse, ISDUControlByte

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

        response_data = self.uart.recv_bytes(2, timeout_ms=500)

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

    def read_isdu(self, index: int, subindex: int = 0) -> Optional[bytes]:
        """
        Read ISDU parameter from Device (supports V1.1.5 Segmented).
        """
        print(
            f"[Master] ISDU Read request: Index=0x{index:04X}, Subindex=0x{subindex:02X}"
        )

        bytes_to_send = self.generator.generate_isdu_read_v11(
            index, subindex, service_id=0x80
        )

        def send_and_recv(byte_to_send: int):
            if self.m_seq_type == 0:
                frame = self.generator.generate_type0(byte_to_send)
                self.uart.send_bytes(frame)
                resp = self.uart.recv_bytes(2, timeout_ms=100)
                return DeviceResponse(resp) if resp else None
            else:
                return self.run_cycle(od_req=byte_to_send)

        for i, val in enumerate(bytes_to_send):
            resp = send_and_recv(val)
            if not resp or not resp.valid:
                print(f"[Master] ISDU Read Req failed at byte {i}")
                return None

        data_bytes = bytearray()
        ctrl = 0

        max_retries = 50
        for i in range(max_retries):
            resp = send_and_recv(0x00)  # IDLE to clock out response
            if not resp or not resp.valid:
                continue
            ctrl = resp.od if hasattr(resp, "od") else resp.payload[0]
            if ctrl != 0:
                print(f"[Master] Read Control Byte captured: 0x{ctrl:02X} at retry {i}")
                break
            time.sleep(0.001)

        if ctrl == 0:
            print("[Master] ISDU Response error: Timeout waiting for Control Byte")
            return None

        is_start = bool(ctrl & 0x80)
        is_last = bool(ctrl & 0x40)

        if not is_start:
            print(f"[Master] ISDU Response error: Expected Start bit, got 0x{ctrl:02X}")
            return None

        while True:
            resp = send_and_recv(0x00)
            if not resp or not resp.valid:
                print(f"[Master] ISDU Read failed at data capture cycle: {resp}")
                break

            val = resp.od if hasattr(resp, "od") else resp.payload[0]
            data_bytes.append(val)
            print(
                f"[Master] Captured ISDU data byte: 0x{val:02X} (Total: {len(data_bytes)})"
            )

            if is_last:
                print(f"[Master] ISDU Read complete: {data_bytes.hex()}")
                return bytes(data_bytes)

            # Wait for NEXT Control Byte
            resp = send_and_recv(0x00)
            if not resp or not resp.valid:
                print(f"[Master] ISDU Read failed at next control cycle: {resp}")
                break
            ctrl = resp.od if hasattr(resp, "od") else resp.payload[0]
            is_start = bool(ctrl & 0x80)
            is_last = bool(ctrl & 0x40)
            print(
                f"[Master] Captured next Control Byte: 0x{ctrl:02X} (is_last={is_last})"
            )

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

        response_data = self.uart.recv_bytes(
            4, timeout_ms=100
        )  # Event: 2 bytes code + status + CK

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

        self.send_wakeup()
        time.sleep(0.1)  # Wait for Device to wake up

        for i in range(3):
            response = self.send_idle()
            if response.valid:
                print(f"[Master] Communication established (attempt {i + 1})")
                self.state = MasterState.PREOPERATE
                return True
            time.sleep(0.05)

        print("[Master] Startup failed - no valid response")
        return False

    def go_to_operate(self) -> bool:
        """Send transition command to Device."""
        print("[Master] Sending OPERATE transition command (MC=0x0F)")
        frame = self.generator.generate_type0(0x0F)  # Custom transition MC
        self.uart.send_bytes(frame)
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

            frame = self.generator.generate_type1(0x00, ckt, pd_out, od_req, od_req2)

            self.uart.send_bytes(frame)

            expected_len = 1 + self.pd_in_len + self.od_len + 1
            response_data = self.uart.recv_bytes(expected_len, timeout_ms=500)

            if response_data:
                return DeviceResponse(response_data, od_len=self.od_len)
            else:
                return DeviceResponse(b"", od_len=self.od_len)

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
            self.generator.generate_type1(0x00, ckt, pd_out, od_req, od_req2)
        )

        frame[-1] ^= 0xFF  # Flip all bits

        self.uart.send_bytes(frame)

        expected_len = 1 + self.pd_in_len + self.od_len + 1
        response_data = self.uart.recv_bytes(expected_len, timeout_ms=100)

        if response_data:
            return DeviceResponse(response_data, od_len=self.od_len)
        else:
            return DeviceResponse(b"", od_len=self.od_len)

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
        """
        Write ISDU parameter to Device (supports V1.1.5 Segmented).
        """
        print(
            f"[Master] ISDU Write request: Index=0x{index:04X}, Subindex=0x{subindex:02X}, Data={data.hex()}"
        )

        data_len = len(data)
        if data_len > 15:
            service_id = 0x9F
            request_data = [
                service_id,
                data_len,
                (index >> 8) & 0xFF,
                index & 0xFF,
                subindex,
            ] + list(data)
        else:
            request_data = [
                0x90 | (data_len & 0x0F),
                (index >> 8) & 0xFF,
                index & 0xFF,
                subindex,
            ] + list(data)

        interleaved = []
        for i, val in enumerate(request_data):
            is_start = i == 0
            is_last = i == len(request_data) - 1
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

        for j, val in enumerate(interleaved):
            resp = send_and_recv(val)
            if not resp or not resp.valid:
                print(
                    f"[Master] ISDU Write Req failed at interleaved byte {j}: 0x{val:02X}"
                )
                return False

        max_retries = 50
        ctrl = 0
        for i in range(max_retries):
            resp = send_and_recv(0x00)
            if not resp or not resp.valid:
                print(f"[Master] ISDU Write Poll failed at retry {i}")
                continue
            ctrl = resp.od if hasattr(resp, "od") else resp.payload[0]
            if ctrl != 0:
                print(f"[Master] ISDU Write captured ctrl: 0x{ctrl:02X} at retry {i}")
                break
            time.sleep(0.001)

        if ctrl == 0:
            print(
                "[Master] ISDU Write failed: Timeout waiting for Response Control Byte"
            )
            return False

        if ctrl & 0x40 and not (
            ctrl & 0x80
        ):  # This logic is a bit simple, but usually bit 6 is error if bit 7 is 0?
            pass

        print(f"[Master] ISDU Write response control: 0x{ctrl:02X}")
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
